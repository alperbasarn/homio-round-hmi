"""Transport abstractions and helpers shared by TCP / BLE implementations."""

from __future__ import annotations

import asyncio
import logging
from abc import ABC, abstractmethod
from collections.abc import AsyncIterator
from enum import Enum
from typing import Any, Generic, TypeVar

from qnob_companion.protocol import Notification, Response

log = logging.getLogger(__name__)


class TransportState(str, Enum):
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    RECONNECTING = "reconnecting"


T = TypeVar("T")


class AsyncSignal(Generic[T]):
    """Lightweight pub-sub for state change events.

    Subscribers receive an ``asyncio.Queue`` they can ``await q.get()`` on.
    Emit is fire-and-forget (uses ``put_nowait``); slow subscribers may drop
    if their queue fills, but state changes are infrequent enough that the
    default unlimited queue size is fine.
    """

    def __init__(self) -> None:
        self._listeners: list[asyncio.Queue[T]] = []

    def subscribe(self) -> asyncio.Queue[T]:
        queue: asyncio.Queue[T] = asyncio.Queue()
        self._listeners.append(queue)
        return queue

    def unsubscribe(self, queue: asyncio.Queue[T]) -> None:
        if queue in self._listeners:
            self._listeners.remove(queue)

    def emit(self, value: T) -> None:
        for queue in self._listeners:
            queue.put_nowait(value)

    async def stream(self) -> AsyncIterator[T]:
        queue = self.subscribe()
        try:
            while True:
                yield await queue.get()
        finally:
            self.unsubscribe(queue)


class IdGenerator:
    """Monotonic uint32 envelope-id generator. Skips 0; rolls over at 0xFFFFFFFF."""

    _MAX = 0xFFFFFFFF

    def __init__(self) -> None:
        self._next = 1

    def next(self) -> int:
        value = self._next
        self._next += 1
        if self._next > self._MAX:
            self._next = 1
        return value


class TransportError(Exception):
    """Raised when a transport operation fails (timeout, disconnect, etc.)."""


class Transport(ABC):
    """Abstract base class for TCP and BLE transports.

    Implementations own envelope id generation and request/response correlation.
    Callers send a command name + params; transport returns the parsed Response.
    """

    def __init__(self) -> None:
        self.state: TransportState = TransportState.DISCONNECTED
        self.state_changed: AsyncSignal[TransportState] = AsyncSignal()
        self.notifications: asyncio.Queue[Notification] = asyncio.Queue()
        self._id_gen = IdGenerator()

    @abstractmethod
    async def connect(self) -> None:
        """Open the underlying connection (or schedule reconnect)."""

    @abstractmethod
    async def disconnect(self) -> None:
        """Cleanly tear down the connection."""

    @abstractmethod
    async def send(
        self,
        cmd: str,
        params: dict[str, Any] | None = None,
        auth: str | None = None,
        timeout: float = 10.0,
    ) -> Response:
        """Send an envelope and await the correlated response.

        Raises ``TransportError`` on disconnect, ``asyncio.TimeoutError`` on
        per-call timeout.
        """

    # ------- internal helpers shared by impls -------

    def _set_state(self, new_state: TransportState) -> None:
        if new_state == self.state:
            return
        log.debug("Transport state %s -> %s", self.state.value, new_state.value)
        self.state = new_state
        self.state_changed.emit(new_state)
