"""OtaUploader tests — exercises the chunk protocol without real TCP or Qt."""

from __future__ import annotations

import asyncio
import base64
import hashlib
import os
import tempfile
from pathlib import Path
from typing import Any
from unittest.mock import AsyncMock, patch

import pytest

from qnob_companion.ota.uploader import CHUNK_SIZE, MAX_FIRMWARE_BYTES, OtaError, OtaUploader
from qnob_companion.protocol import Response
from qnob_companion.transport import DeviceClient, TransportState
from qnob_companion.transport.base import IdGenerator


# ---------------------------------------------------------------------------
# Fixtures / helpers
# ---------------------------------------------------------------------------

def _ok_resp(**data: Any) -> Response:
    return Response(id=1, status="ok", data=data if data else None)


def _err_resp(error: str) -> Response:
    return Response(id=1, status="err", error=error)


def _firmware(size: int = 8192) -> bytes:
    return os.urandom(size)


def _write_firmware(tmp_path: Path, data: bytes) -> Path:
    p = tmp_path / "firmware.bin"
    p.write_bytes(data)
    return p


def _make_client(send_side_effect: Any) -> DeviceClient:
    client = DeviceClient.__new__(DeviceClient)
    client._tcp = _mock_tcp()
    client._ble = None
    client._auth_token = None
    client._id_gen = IdGenerator()
    client.send_command = AsyncMock(side_effect=send_side_effect)
    return client


def _mock_tcp() -> Any:
    from unittest.mock import MagicMock
    tcp = MagicMock()
    tcp.state = TransportState.CONNECTED
    return tcp


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_happy_path_sends_begin_then_chunks(tmp_path: Path) -> None:
    data = _firmware(CHUNK_SIZE * 3)  # 3 full chunks
    sha = hashlib.sha256(data).hexdigest()
    path = _write_firmware(tmp_path, data)

    responses = [
        _ok_resp(upload_id="session-1"),                              # otaBegin
        _ok_resp(acked_offset=CHUNK_SIZE),                            # chunk 0
        _ok_resp(acked_offset=CHUNK_SIZE * 2),                        # chunk 1
        _ok_resp(acked_offset=CHUNK_SIZE * 3),                        # chunk 2
    ]
    client = _make_client(responses.__iter__().__next__)

    uploader = OtaUploader(client, path)
    await uploader.start()

    calls = client.send_command.call_args_list
    # First call: otaBegin
    assert calls[0].args[0] == "otaBegin"
    assert calls[0].args[1]["size"] == len(data)
    assert calls[0].args[1]["sha256"] == sha

    # Subsequent: otaChunk with base64 data
    assert calls[1].args[0] == "otaChunk"
    assert calls[1].args[1]["upload_id"] == "session-1"
    assert calls[1].args[1]["offset"] == 0
    decoded = base64.b64decode(calls[1].args[1]["data"])
    assert decoded == data[:CHUNK_SIZE]

    assert len(calls) == 4  # 1 begin + 3 chunks


@pytest.mark.asyncio
async def test_progress_callback_called_per_chunk(tmp_path: Path) -> None:
    data = _firmware(CHUNK_SIZE * 2)
    path = _write_firmware(tmp_path, data)

    responses = [
        _ok_resp(upload_id="s"),
        _ok_resp(acked_offset=CHUNK_SIZE),
        _ok_resp(acked_offset=CHUNK_SIZE * 2),
    ]
    client = _make_client(responses.__iter__().__next__)
    progress_calls: list[tuple[int, int]] = []

    def on_progress(acked: int, total: int, rate: float, eta: float) -> None:
        progress_calls.append((acked, total))

    uploader = OtaUploader(client, path)
    await uploader.start(on_progress=on_progress)

    assert len(progress_calls) == 2
    assert progress_calls[0] == (CHUNK_SIZE, CHUNK_SIZE * 2)
    assert progress_calls[1] == (CHUNK_SIZE * 2, CHUNK_SIZE * 2)


@pytest.mark.asyncio
async def test_file_too_large_raises_before_connecting(tmp_path: Path) -> None:
    oversized = tmp_path / "big.bin"
    oversized.write_bytes(b"\x00" * (MAX_FIRMWARE_BYTES + 1))
    client = _make_client(lambda *a, **kw: _ok_resp())

    uploader = OtaUploader(client, oversized)
    with pytest.raises(OtaError, match="4 MiB"):
        await uploader.start()

    client.send_command.assert_not_called()


@pytest.mark.asyncio
async def test_begin_rejected_raises_ota_error(tmp_path: Path) -> None:
    path = _write_firmware(tmp_path, _firmware())
    client = _make_client(lambda *a, **kw: _err_resp("busy"))

    uploader = OtaUploader(client, path)
    with pytest.raises(OtaError, match="otaBegin"):
        await uploader.start()


@pytest.mark.asyncio
async def test_chunk_failure_raises_ota_error(tmp_path: Path) -> None:
    path = _write_firmware(tmp_path, _firmware())

    call_count = 0

    def side_effect(*args: Any, **kwargs: Any) -> Response:
        nonlocal call_count
        call_count += 1
        if call_count == 1:
            return _ok_resp(upload_id="s")
        return _err_resp("internal")

    client = _make_client(side_effect)
    uploader = OtaUploader(client, path)
    with pytest.raises(OtaError, match="otaChunk"):
        await uploader.start()


@pytest.mark.asyncio
async def test_cancel_sends_ota_abort(tmp_path: Path) -> None:
    data = _firmware(CHUNK_SIZE * 10)
    path = _write_firmware(tmp_path, data)

    begin_sent = asyncio.Event()
    abort_cmd: list[str] = []

    async def side_effect(cmd: str, params: Any = None, **kw: Any) -> Response:
        if cmd == "otaBegin":
            begin_sent.set()
            return _ok_resp(upload_id="to-abort")
        if cmd == "otaAbort":
            abort_cmd.append(params["upload_id"])
            return _ok_resp()
        # Chunk: simulate slow device
        await asyncio.sleep(0.01)
        offset = params["offset"] + CHUNK_SIZE
        return _ok_resp(acked_offset=min(offset, len(data)))

    client = DeviceClient.__new__(DeviceClient)
    client._tcp = _mock_tcp()
    client._ble = None
    client._auth_token = None
    client._id_gen = IdGenerator()
    client.send_command = AsyncMock(side_effect=side_effect)

    uploader = OtaUploader(client, path)

    async def _cancel_after_begin() -> None:
        await begin_sent.wait()
        await asyncio.sleep(0.015)
        await uploader.cancel()

    with pytest.raises(OtaError, match="cancelled"):
        await asyncio.gather(uploader.start(), _cancel_after_begin())

    assert abort_cmd == ["to-abort"]


@pytest.mark.asyncio
async def test_no_tcp_transport_raises(tmp_path: Path) -> None:
    path = _write_firmware(tmp_path, _firmware())

    client = DeviceClient.__new__(DeviceClient)
    client._tcp = None  # no TCP at all
    client._ble = None
    client._auth_token = None
    client._id_gen = IdGenerator()
    client.send_command = AsyncMock()

    uploader = OtaUploader(client, path)
    with pytest.raises(OtaError, match="TCP"):
        await uploader.start()

    client.send_command.assert_not_called()
