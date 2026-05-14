"""Tests for SecretStore implementations and PairingService (#65 B4)."""

from __future__ import annotations

from typing import Any

import pytest

from qnob_companion.pairing.secret_store import MemorySecretStore
from qnob_companion.pairing.service import PairingResult, PairingService
from qnob_companion.protocol import Response
from qnob_companion.transport import (
    DeviceClient,
    Transport,
    TransportError,
    TransportState,
)


# ===========================================================================
# Helpers
# ===========================================================================


class _MockTransport(Transport):
    """Minimal in-memory transport for pairing tests."""

    def __init__(self) -> None:
        super().__init__()
        self.sends: list[dict[str, Any]] = []
        self.canned_response: Response | None = None
        self.raise_on_send: BaseException | None = None

    async def connect(self) -> None:
        self._set_state(TransportState.CONNECTED)

    async def disconnect(self) -> None:
        self._set_state(TransportState.DISCONNECTED)

    async def send(
        self,
        cmd: str,
        params: dict[str, Any] | None = None,
        auth: str | None = None,
        timeout: float = 10.0,
    ) -> Response:
        self.sends.append({"cmd": cmd, "params": params, "auth": auth})
        if self.raise_on_send is not None:
            raise self.raise_on_send
        if self.canned_response is None:
            return Response(id=1, status="ok")
        return self.canned_response


def _make_client() -> tuple[DeviceClient, _MockTransport]:
    transport = _MockTransport()
    client = DeviceClient(tcp=transport)  # type: ignore[arg-type]
    return client, transport


_MAC = "aabbccddeeff"
_TOKEN = "A" * 44  # valid 44-char token placeholder


# ===========================================================================
# MemorySecretStore
# ===========================================================================


def test_memory_store_get_returns_none_for_unknown_mac() -> None:
    store = MemorySecretStore()
    assert store.get(_MAC) is None


def test_memory_store_set_and_get_roundtrip() -> None:
    store = MemorySecretStore()
    store.set(_MAC, _TOKEN)
    assert store.get(_MAC) == _TOKEN


def test_memory_store_delete_removes_entry() -> None:
    store = MemorySecretStore()
    store.set(_MAC, _TOKEN)
    store.delete(_MAC)
    assert store.get(_MAC) is None


def test_memory_store_delete_is_noop_when_missing() -> None:
    store = MemorySecretStore()
    store.delete(_MAC)  # must not raise


def test_memory_store_isolated_per_mac() -> None:
    store = MemorySecretStore()
    store.set("mac1", "token-A")
    store.set("mac2", "token-B")
    assert store.get("mac1") == "token-A"
    assert store.get("mac2") == "token-B"
    store.delete("mac1")
    assert store.get("mac1") is None
    assert store.get("mac2") == "token-B"


# ===========================================================================
# PairingService.load_stored_token
# ===========================================================================


def test_load_stored_token_injects_token_into_client() -> None:
    store = MemorySecretStore()
    store.set(_MAC, _TOKEN)
    client, _ = _make_client()
    svc = PairingService(client, store, _MAC)

    found = svc.load_stored_token()

    assert found is True
    assert client.has_auth_token


def test_load_stored_token_returns_false_when_not_paired() -> None:
    store = MemorySecretStore()
    client, _ = _make_client()
    svc = PairingService(client, store, _MAC)

    found = svc.load_stored_token()

    assert found is False
    assert not client.has_auth_token


# ===========================================================================
# PairingService.pair — success path
# ===========================================================================


@pytest.mark.asyncio
async def test_pair_ok_stores_token_and_injects_auth() -> None:
    store = MemorySecretStore()
    client, transport = _make_client()
    await transport.connect()
    svc = PairingService(client, store, _MAC)

    result = await svc.pair(_TOKEN)

    assert result == PairingResult.OK
    # Token stored in secret store.
    assert store.get(_MAC) == _TOKEN
    # Token injected into DeviceClient so subsequent sends carry auth.
    assert client.has_auth_token


@pytest.mark.asyncio
async def test_pair_ok_sends_correct_envelope() -> None:
    store = MemorySecretStore()
    client, transport = _make_client()
    await transport.connect()
    svc = PairingService(client, store, _MAC)

    await svc.pair(_TOKEN)

    assert len(transport.sends) == 1
    send = transport.sends[0]
    assert send["cmd"] == "pair"
    assert send["params"] == {"token": _TOKEN}


@pytest.mark.asyncio
async def test_pair_ok_clears_needs_reauth() -> None:
    store = MemorySecretStore()
    client, transport = _make_client()
    await transport.connect()
    svc = PairingService(client, store, _MAC)

    # Simulate a prior reauth-needed state.
    store.set(_MAC, "old-token")
    svc.handle_unauth()
    assert svc.needs_reauth is True

    await svc.pair(_TOKEN)

    assert svc.needs_reauth is False


# ===========================================================================
# PairingService.pair — wrong token
# ===========================================================================


@pytest.mark.asyncio
async def test_pair_wrong_token_returns_wrong_token_result() -> None:
    store = MemorySecretStore()
    client, transport = _make_client()
    await transport.connect()
    transport.canned_response = Response(id=1, status="err", error="unauth")
    svc = PairingService(client, store, _MAC)

    result = await svc.pair(_TOKEN)

    assert result == PairingResult.WRONG_TOKEN
    # Token must NOT be stored.
    assert store.get(_MAC) is None
    assert not client.has_auth_token


# ===========================================================================
# PairingService.pair — transport error
# ===========================================================================


@pytest.mark.asyncio
async def test_pair_transport_error_returns_error_result() -> None:
    store = MemorySecretStore()
    client, transport = _make_client()
    await transport.connect()
    transport.raise_on_send = TransportError("connection lost")
    svc = PairingService(client, store, _MAC)

    result = await svc.pair(_TOKEN)

    assert result == PairingResult.ERROR
    assert store.get(_MAC) is None


# ===========================================================================
# PairingService.unpair
# ===========================================================================


@pytest.mark.asyncio
async def test_unpair_deletes_token_and_clears_client_auth() -> None:
    store = MemorySecretStore()
    store.set(_MAC, _TOKEN)
    client, transport = _make_client()
    await transport.connect()
    client.set_auth_token(_TOKEN)
    svc = PairingService(client, store, _MAC)

    await svc.unpair()

    assert store.get(_MAC) is None
    assert not client.has_auth_token


@pytest.mark.asyncio
async def test_unpair_sends_unpair_command() -> None:
    store = MemorySecretStore()
    store.set(_MAC, _TOKEN)
    client, transport = _make_client()
    await transport.connect()
    svc = PairingService(client, store, _MAC)

    await svc.unpair()

    assert any(s["cmd"] == "unpair" for s in transport.sends)


@pytest.mark.asyncio
async def test_unpair_deletes_token_even_when_transport_fails() -> None:
    """Token must be removed from store even if the unpair RPC fails."""
    store = MemorySecretStore()
    store.set(_MAC, _TOKEN)
    client, transport = _make_client()
    await transport.connect()
    transport.raise_on_send = TransportError("offline")
    svc = PairingService(client, store, _MAC)

    await svc.unpair()  # must not raise

    assert store.get(_MAC) is None


# ===========================================================================
# PairingService.handle_unauth — "needs re-pair" state
# ===========================================================================


def test_handle_unauth_sets_needs_reauth_when_paired() -> None:
    store = MemorySecretStore()
    store.set(_MAC, _TOKEN)
    client, _ = _make_client()
    client.set_auth_token(_TOKEN)
    svc = PairingService(client, store, _MAC)

    assert svc.needs_reauth is False
    svc.handle_unauth()
    assert svc.needs_reauth is True


def test_handle_unauth_noop_when_not_paired() -> None:
    """handle_unauth must not set needs_reauth for an unpaired device."""
    store = MemorySecretStore()
    client, _ = _make_client()
    svc = PairingService(client, store, _MAC)

    svc.handle_unauth()

    assert svc.needs_reauth is False


# ===========================================================================
# Auth token auto-injection via DeviceClient
# ===========================================================================


@pytest.mark.asyncio
async def test_auth_token_auto_attached_to_every_send_after_pairing() -> None:
    """After a successful pair(), the DeviceClient attaches auth on every send."""
    store = MemorySecretStore()
    client, transport = _make_client()
    await transport.connect()
    svc = PairingService(client, store, _MAC)

    await svc.pair(_TOKEN)

    # Pair send carries no auth yet (token set *after* the pair response).
    # Now subsequent sends should carry the stored token.
    await client.send_command("ping")
    assert transport.sends[-1]["auth"] == _TOKEN
