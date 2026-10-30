"""The other node on the CAN bus: a USB-CAN adapter (CANable) behind the transceiver on the Nucleo's FDCAN1 pins.

`--can-peer` selects it:

- `<interface>:<channel>` opens a python-can bus, e.g. `slcan:COM7`, `slcan:/dev/ttyACM1`,
  `slcan:socket://host.docker.internal:5002` (the adapter's COM port forwarded by port-bridge), `gs_usb:0`,
  `candle:0`, `socketcan:can0` or `virtual:<name>` (the `--fake` bus). The bit rate follows each test,
  except for `socketcan`, where `ip link` sets it.
- `port-bridge:<host>:<port>` connects to the CAN bridge of port-bridge (16-byte SocketCAN frames). The adapter
  runs at the `--can-bitrate` port-bridge was started with.

Only `slcan` has a listen-only mode, and only peers whose bit rate follows the test can drive the controller
to bus off with a mismatched bit rate.
"""

from __future__ import annotations

import socket
import struct
import time
from abc import ABC, abstractmethod
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

__all__ = [
    "FIXED_BITRATE_INTERFACES",
    "LISTEN_ONLY_INTERFACES",
    "SLCAN_BITRATES",
    "BusFrame",
    "CanPeer",
    "CanPeerError",
    "PortBridgeCanPeer",
    "PythonCanPeer",
    "decode_frame",
    "encode_frame",
    "open_peer",
]

LISTEN_ONLY_INTERFACES = frozenset({"slcan"})
FIXED_BITRATE_INTERFACES = frozenset({"socketcan"})
SLCAN_BITRATES = frozenset({10000, 20000, 50000, 83300, 100000, 125000, 250000, 500000, 750000, 1000000})

_FRAME = struct.Struct("<IBxxx8s")
_EFF_FLAG = 0x80000000
_RTR_FLAG = 0x40000000
_ERR_FLAG = 0x20000000
_EFF_MASK = 0x1FFFFFFF


class CanPeerError(Exception):
    pass


@dataclass(frozen=True)
class BusFrame:
    id: int
    ext: bool
    data: bytes


class CanPeer(ABC):
    """A CAN node the tests configure, send from and receive on; error and remote frames are dropped."""

    fixed_bitrate: int | None = None
    listen_only_capable: bool = False

    def __init__(self) -> None:
        self.bitrate: int | None = None
        self.listen_only = False

    def supports(self, bitrate: int, listen_only: bool = False) -> bool:
        if listen_only and not self.listen_only_capable:
            return False
        return self.fixed_bitrate is None or self.fixed_bitrate == bitrate

    @property
    def adjustable(self) -> bool:
        """Whether the bit rate can be set per test (needed to put a wrong bit rate on the bus)."""
        return self.fixed_bitrate is None

    def configure(self, bitrate: int, listen_only: bool = False) -> None:
        if not self.supports(bitrate, listen_only):
            raise CanPeerError(f"{self} cannot run at {bitrate} bit/s{' listen-only' if listen_only else ''}")
        if (bitrate, listen_only) != (self.bitrate, self.listen_only):
            self._configure(bitrate, listen_only)
            self.bitrate, self.listen_only = bitrate, listen_only
        self.flush()

    def receive(self, timeout: float = 1.0, predicate: Callable[[BusFrame], bool] | None = None) -> BusFrame | None:
        deadline = time.monotonic() + timeout
        while True:
            frame = self._receive(max(deadline - time.monotonic(), 0.0))
            if frame is not None and (predicate is None or predicate(frame)):
                return frame
            if time.monotonic() >= deadline:
                return None

    def flush(self) -> None:
        while self._receive(0.0) is not None:
            pass

    @abstractmethod
    def _configure(self, bitrate: int, listen_only: bool) -> None: ...

    @abstractmethod
    def send(self, id: int, data: bytes, ext: bool = False) -> None: ...

    @abstractmethod
    def _receive(self, timeout: float) -> BusFrame | None:
        """The next data frame within `timeout`, or None (also when only error or remote frames arrived)."""

    @abstractmethod
    def close(self) -> None: ...


class PythonCanPeer(CanPeer):
    def __init__(self, interface: str, channel: str, fixed_bitrate: int | None = None, **options: Any) -> None:
        super().__init__()
        self.interface = interface
        self.channel = channel
        self.options = options
        self.listen_only_capable = interface in LISTEN_ONLY_INTERFACES
        if interface in FIXED_BITRATE_INTERFACES or fixed_bitrate is not None:
            self.fixed_bitrate = fixed_bitrate
            if self.fixed_bitrate is None:
                raise CanPeerError(f"{interface} takes its bit rate from the system: pass --can-peer-bitrate")
        self._bus: Any = None

    def __str__(self) -> str:
        return f"{self.interface}:{self.channel}"

    def supports(self, bitrate: int, listen_only: bool = False) -> bool:
        if self.interface == "slcan" and bitrate not in SLCAN_BITRATES:
            return False
        return super().supports(bitrate, listen_only)

    def _configure(self, bitrate: int, listen_only: bool) -> None:
        import can

        if self._bus is not None and listen_only == self.listen_only and hasattr(self._bus, "set_bitrate"):
            self._bus.set_bitrate(bitrate)
            return
        self.close()
        options = dict(self.options)
        if self.interface not in FIXED_BITRATE_INTERFACES:
            options["bitrate"] = bitrate
        if listen_only:
            options["listen_only"] = True
        try:
            self._bus = can.Bus(interface=self.interface, channel=self.channel, **options)
        except (can.CanError, OSError, ValueError) as error:
            raise CanPeerError(f"cannot open {self}: {error}") from error

    def send(self, id: int, data: bytes, ext: bool = False) -> None:
        import can

        self._require()
        try:
            self._bus.send(can.Message(arbitration_id=id, is_extended_id=ext, data=bytes(data)), timeout=1.0)
        except can.CanError as error:
            raise CanPeerError(f"{self} could not send 0x{id:x}: {error}") from error

    def _receive(self, timeout: float) -> BusFrame | None:
        self._require()
        message = self._bus.recv(timeout)
        if message is None or message.is_error_frame or message.is_remote_frame:
            return None
        return BusFrame(message.arbitration_id, bool(message.is_extended_id), bytes(message.data))

    def _require(self) -> None:
        if self._bus is None:
            raise CanPeerError(f"{self} is not configured")

    def close(self) -> None:
        if self._bus is not None:
            self._bus.shutdown()
            self._bus = None
            self.bitrate = None


class PortBridgeCanPeer(CanPeer):
    def __init__(self, host: str, port: int, bitrate: int | None) -> None:
        super().__init__()
        if bitrate is None:
            raise CanPeerError("port-bridge runs the adapter at its --can-bitrate: pass the same --can-peer-bitrate")
        self.host = host
        self.port = port
        self.fixed_bitrate = bitrate
        self._socket: socket.socket | None = None
        self._buffer = bytearray()

    def __str__(self) -> str:
        return f"port-bridge:{self.host}:{self.port}"

    def _configure(self, bitrate: int, listen_only: bool) -> None:
        if self._socket is None:
            try:
                self._socket = socket.create_connection((self.host, self.port), timeout=5.0)
            except OSError as error:
                raise CanPeerError(f"cannot connect to {self}: {error}") from error

    def send(self, id: int, data: bytes, ext: bool = False) -> None:
        if len(data) > 8:
            raise CanPeerError("a classic CAN frame carries at most 8 bytes")
        sock = self._require()
        sock.sendall(encode_frame(id, data, ext))

    def _receive(self, timeout: float) -> BusFrame | None:
        sock = self._require()
        deadline = time.monotonic() + timeout
        while len(self._buffer) < _FRAME.size:
            sock.settimeout(max(deadline - time.monotonic(), 0.0) or 0.001)
            try:
                chunk = sock.recv(4096)
            except (TimeoutError, BlockingIOError):
                return None
            if not chunk:
                raise CanPeerError(f"{self} closed the connection")
            self._buffer.extend(chunk)
        raw = bytes(self._buffer[: _FRAME.size])
        del self._buffer[: _FRAME.size]
        return decode_frame(raw)

    def _require(self) -> socket.socket:
        if self._socket is None:
            raise CanPeerError(f"{self} is not configured")
        return self._socket

    def close(self) -> None:
        if self._socket is not None:
            self._socket.close()
            self._socket = None
            self._buffer.clear()
            self.bitrate = None


def encode_frame(id: int, data: bytes, ext: bool) -> bytes:
    """port-bridge / Linux `struct can_frame` layout."""
    return _FRAME.pack((id & _EFF_MASK) | (_EFF_FLAG if ext else 0), len(data), bytes(data).ljust(8, b"\x00"))


def decode_frame(raw: bytes) -> BusFrame | None:
    """The data frame in `raw`, or None for an error or remote frame."""
    can_id, dlc, data = _FRAME.unpack(raw)
    if can_id & (_ERR_FLAG | _RTR_FLAG):
        return None
    return BusFrame(can_id & _EFF_MASK, bool(can_id & _EFF_FLAG), data[: min(dlc, 8)])


def open_peer(spec: str, fixed_bitrate: int | None = None) -> CanPeer:
    """The peer `spec` names (see the module docstring); nothing is opened before `configure`."""
    interface, separator, channel = spec.partition(":")
    if not separator or not interface or not channel:
        raise CanPeerError(f"--can-peer {spec!r}: expected <interface>:<channel> or port-bridge:<host>:<port>")
    if interface == "port-bridge":
        host, separator, port = channel.rpartition(":")
        if not separator or not host or not port.isdigit():
            raise CanPeerError(f"--can-peer {spec!r}: expected port-bridge:<host>:<port>")
        return PortBridgeCanPeer(host, int(port), fixed_bitrate)
    return PythonCanPeer(interface, channel, fixed_bitrate)
