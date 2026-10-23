"""Secret storage for pairing tokens.

``KeyringSecretStore`` uses Windows Credential Manager on Windows (keyring's
default backend) with the service name ``"Qnob"`` and the canonical MAC as the
username.  ``MemorySecretStore`` is an in-memory fake for tests.
"""

from __future__ import annotations

from abc import ABC, abstractmethod

import keyring
import keyring.errors


class SecretStore(ABC):
    """Interface for pairing-token persistence."""

    @abstractmethod
    def set(self, mac: str, token: str) -> None:
        """Store ``token`` for the device identified by ``mac``."""

    @abstractmethod
    def get(self, mac: str) -> str | None:
        """Return the stored token, or ``None`` if not paired."""

    @abstractmethod
    def delete(self, mac: str) -> None:
        """Remove the stored token. No-op if none exists."""


class KeyringSecretStore(SecretStore):
    """Credential Manager / keychain-backed store (Windows Credential Manager
    is used automatically on Windows via the ``keyring`` library).

    Key format: service ``"Qnob"``, username ``<mac>`` (12 lower-hex chars).
    Survives reinstall; scoped per OS user account.
    """

    _SERVICE = "Qnob"

    def set(self, mac: str, token: str) -> None:
        keyring.set_password(self._SERVICE, mac, token)

    def get(self, mac: str) -> str | None:
        return keyring.get_password(self._SERVICE, mac)

    def delete(self, mac: str) -> None:
        try:
            keyring.delete_password(self._SERVICE, mac)
        except keyring.errors.PasswordDeleteError:
            pass


class MemorySecretStore(SecretStore):
    """In-memory fake — no system credential storage used.

    Intended for unit tests.  Multiple instances are completely isolated.
    """

    def __init__(self) -> None:
        self._data: dict[str, str] = {}

    def set(self, mac: str, token: str) -> None:
        self._data[mac] = token

    def get(self, mac: str) -> str | None:
        return self._data.get(mac)

    def delete(self, mac: str) -> None:
        self._data.pop(mac, None)
