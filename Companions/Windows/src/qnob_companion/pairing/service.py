"""PairingService — sends pair/unpair commands and manages token storage.

Orchestrates the exchange between the firmware's ``pair``/``unpair`` RPC
endpoints, the ``SecretStore``, and the ``DeviceClient``'s auth token slot.
"""

from __future__ import annotations

import logging
from enum import Enum

from qnob_companion.pairing.secret_store import SecretStore
from qnob_companion.transport.base import TransportError
from qnob_companion.transport.client import DeviceClient

log = logging.getLogger(__name__)


class PairingResult(str, Enum):
    """Outcome of a ``PairingService.pair()`` call."""

    OK = "ok"
    """Device accepted the token; it has been stored."""

    WRONG_TOKEN = "wrong_token"
    """Device returned ``error: unauth``.  Token not stored."""

    ERROR = "error"
    """Transport failure or unexpected error response."""


class PairingService:
    """Orchestrates one pair/unpair cycle for a single ``DeviceClient``.

    Typical usage::

        svc = PairingService(client, secret_store, mac="aabbccddeeff")
        svc.load_stored_token()          # re-inject saved token on startup
        result = await svc.pair(token)   # called from wizard page 3
        await svc.unpair()               # called from device detail page

    After pairing completes, every subsequent ``client.send_command()`` will
    automatically include the stored ``auth`` token (DeviceClient injects it).

    If a paired device later replies ``error: unauth`` during normal use, call
    ``handle_unauth()`` so the service can mark itself as needing re-pairing.
    """

    def __init__(
        self,
        client: DeviceClient,
        secret_store: SecretStore,
        mac: str,
    ) -> None:
        self._client = client
        self._store = secret_store
        self._mac = mac
        self._needs_reauth = False

    # ----- public properties -----

    @property
    def is_paired(self) -> bool:
        """True when a stored token exists for this device."""
        return self._store.get(self._mac) is not None

    @property
    def needs_reauth(self) -> bool:
        """True when the device rejected the stored token during normal use.

        Cleared automatically on a successful ``pair()`` call.
        """
        return self._needs_reauth

    # ----- lifecycle helpers -----

    def load_stored_token(self) -> bool:
        """Load any previously stored token into the client.

        Returns ``True`` if a token was found and injected, ``False`` otherwise.
        """
        token = self._store.get(self._mac)
        if token is not None:
            self._client.set_auth_token(token)
            return True
        return False

    def handle_unauth(self) -> None:
        """Call when a command from a *paired* device returns ``error: unauth``.

        Marks the service as needing re-pairing so the UI can present the
        pairing wizard again.
        """
        if self.is_paired:
            self._needs_reauth = True
            log.warning("Device %s returned unauth after pairing — needs re-pair", self._mac)

    # ----- RPC calls -----

    async def pair(self, token: str) -> PairingResult:
        """Send ``{"cmd":"pair","params":{"token":"..."}}`` to the device.

        On ``status: ok``:
            Stores the token in ``secret_store`` and injects it into the client.
        On ``error: unauth``:
            Returns ``PairingResult.WRONG_TOKEN`` without storing anything.
        On transport or other error:
            Returns ``PairingResult.ERROR``.
        """
        try:
            resp = await self._client.send_command("pair", params={"token": token})
        except TransportError as exc:
            log.warning("Pairing transport error for %s: %s", self._mac, exc)
            return PairingResult.ERROR

        if resp.is_ok:
            self._store.set(self._mac, token)
            self._client.set_auth_token(token)
            self._needs_reauth = False
            log.info("Paired device %s", self._mac)
            return PairingResult.OK

        if resp.error == "unauth":
            return PairingResult.WRONG_TOKEN

        log.warning(
            "Pairing rejected (%s %s) for %s", resp.error, resp.detail, self._mac
        )
        return PairingResult.ERROR

    async def unpair(self) -> None:
        """Send ``{"cmd":"unpair"}`` (best-effort) then delete the stored token.

        The token is always deleted from the store and cleared from the client,
        even if the command fails (e.g. device is offline).
        """
        try:
            await self._client.send_command("unpair")
        except TransportError as exc:
            log.warning("unpair command failed for %s: %s", self._mac, exc)
        finally:
            self._store.delete(self._mac)
            self._client.set_auth_token(None)
            self._needs_reauth = False
            log.info("Unpaired device %s", self._mac)
