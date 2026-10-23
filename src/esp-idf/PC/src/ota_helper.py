from __future__ import annotations

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import socket
import threading
from typing import Optional
from urllib.parse import quote


DEFAULT_OTA_PORT = 8070


class _QuietRequestHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        return


class OTAHelper:
    def __init__(self, ota_dir: Optional[Path] = None):
        default_dir = Path(__file__).resolve().parents[3] / "OTA"
        self.ota_dir = (ota_dir or default_dir).resolve()
        self._server = None
        self._thread = None
        self._port = None
        self._bind_host = None

    def ensure_ota_dir(self) -> Path:
        self.ota_dir.mkdir(parents=True, exist_ok=True)
        return self.ota_dir

    def list_firmware_images(self) -> list[str]:
        self.ensure_ota_dir()
        return sorted((path.name for path in self.ota_dir.glob("*.bin")), reverse=True)

    def auto_detect_host(self) -> str:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.connect(("8.8.8.8", 80))
                host = sock.getsockname()[0]
                if host:
                    return host
        except OSError:
            pass

        try:
            host = socket.gethostbyname(socket.gethostname())
            if host and host != "127.0.0.1":
                return host
        except OSError:
            pass

        return "127.0.0.1"

    def ensure_server_running(self, port: int = DEFAULT_OTA_PORT, bind_host: str = "0.0.0.0") -> None:
        self.ensure_ota_dir()
        if self._server is not None and self._port == port and self._bind_host == bind_host:
            return

        self.stop_server()

        handler = partial(_QuietRequestHandler, directory=str(self.ota_dir))
        self._server = ThreadingHTTPServer((bind_host, port), handler)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()
        self._port = port
        self._bind_host = bind_host

    def stop_server(self) -> None:
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
            self._server = None
        if self._thread is not None:
            self._thread.join(timeout=1.0)
            self._thread = None
        self._port = None
        self._bind_host = None

    def build_update_url(self, filename: str, host: str, port: int) -> str:
        firmware_path = self.ensure_ota_dir() / filename
        if not firmware_path.is_file():
            raise FileNotFoundError(f"OTA image not found: {firmware_path}")

        self.ensure_server_running(port=port)
        return f"http://{host}:{port}/{quote(filename)}"
