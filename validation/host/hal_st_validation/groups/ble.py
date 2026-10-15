"""`ble` group of validation/PROTOCOL.md: the BLE commands of `hal_st.validation_ble_firmware` (NUCLEO-WB55RG and
NUCLEO-WBA55CG). One board runs as `central`, the other as `peripheral`; the role is chosen once per boot with `open`.

A command only reports that the request was accepted; what the radio does arrives as `EVT ble <name> key=value...`
lines (`ready`, `state`, `found`, `connected`, `connectfailed`, `disconnected`, `conparams`, `phy`, `datalength`,
`mtu`, `uart`, `sent`, `rx`, `security`, `paired`, `pairfailed`, `bonds`). `wait` returns the oldest queued or next
event of a name whose fields match.
"""

from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Literal

from ad3_waveforms_bench.protocol import Event
from ad3_waveforms_bench.terminal import TerminalTimeout

from ..patterns import Pattern
from .base import Group

__all__ = [
    "GROUPS",
    "AddressType",
    "Ble",
    "BleInfo",
    "BleLink",
    "BleSent",
    "Phy",
    "Role",
]

Role = Literal["central", "peripheral"]
Phy = Literal["1m", "2m", "coded"]
AddressType = Literal["public", "random"]

INTERVAL_UNIT_MS = 1.25
TIMEOUT_UNIT_MS = 10


@dataclass(frozen=True)
class BleInfo:
    ready: bool
    role: str
    address: str
    stack: str


@dataclass(frozen=True)
class BleLink:
    """`ble.link`: the GAP state (`standby`, `advertising`, `scanning`, `connecting` or `connected`) and, on a link, what
    the firmware has learnt about it from the controller's events. `rx_octets` is None on the central after a data length
    change: hal-st's observer reports the transmit side only."""

    state: str
    connected: bool
    peer: str | None = None
    interval: int = 0
    latency: int = 0
    timeout: int = 0
    tx_phy: str = "1m"
    rx_phy: str = "1m"
    tx_octets: int = 27
    rx_octets: int | None = 27
    mtu: int = 23


@dataclass(frozen=True)
class BleSent:
    length: int
    crc: int


class Ble(Group):
    prefix = "ble"

    def info(self) -> BleInfo:
        response = self._cmd("info")
        return BleInfo(
            ready=response.as_bool("ready"),
            role=response["role"],
            address=response["address"],
            stack=response.get("stack", "unknown") or "unknown",
        )

    def open(self, role: Role, address: str | None = None) -> str:
        """Builds the controller and the role; the role stays until the board resets. Returns the address in use."""
        return self._cmd("open", role=role, address=address).get("address", "") or ""

    def advertise(self, interval: int | None = None, name: str | None = None) -> None:
        """Connectable undirected advertising; `interval` in ms, `name` the complete local name."""
        self._cmd("advertise", interval=interval, name_=name)

    def standby(self) -> None:
        """Stops advertising; on a live link the peripheral disconnects instead."""
        self._cmd("standby")

    def allow_pairing(self, allow: bool) -> None:
        self._cmd("allowpairing", int(allow))

    def scan(self, addr: str | None = None, name: str | None = None, timeout: int | None = None) -> None:
        """Active scan; each address is reported once (`EVT ble found`). `timeout` in ms stops it by itself."""
        self._cmd("scan", addr=addr, name_=name, timeout=timeout)

    def stopscan(self) -> None:
        self._cmd("stopscan")

    def connect(self, address: str, address_type: AddressType | None = None, timeout: int | None = None) -> None:
        """`timeout` (ms) bounds the initiating procedure; failure arrives as `EVT ble connectfailed`."""
        self._cmd("connect", address, type=address_type, timeout=timeout)

    def cancel(self) -> None:
        self._cmd("cancel")

    def phy(self, tx: Phy, rx: Phy) -> None:
        self._cmd("phy", tx=tx, rx=rx)

    def datalength(self) -> None:
        """Requests the largest data length of LE 2M."""
        self._cmd("datalength")

    def mtu(self) -> None:
        self._cmd("mtu")

    def discover(self) -> None:
        """Discovers the Nordic UART service of the peer and subscribes to its notifications."""
        self._cmd("discover")

    def disconnect(self) -> None:
        self._cmd("disconnect")

    def conparams(self, interval_min: int, interval_max: int, latency: int, timeout: int) -> None:
        """The central updates the link; the peripheral asks for it. Raw units: 1.25 ms and 10 ms."""
        self._cmd("conparams", min=interval_min, max=interval_max, latency=latency, timeout=timeout)

    def link(self) -> BleLink:
        response = self._cmd("link")
        if not response.as_bool("connected"):
            return BleLink(state=response["state"], connected=False)
        return BleLink(
            state=response["state"],
            connected=True,
            peer=response["peer"],
            interval=response.as_int("interval"),
            latency=response.as_int("latency"),
            timeout=response.as_int("timeout"),
            tx_phy=response["txphy"],
            rx_phy=response["rxphy"],
            tx_octets=response.as_int("txoctets"),
            rx_octets=None if response["rxoctets"] == "-" else response.as_int("rxoctets"),
            mtu=response.as_int("mtu"),
        )

    def send(
        self,
        data: bytes | None = None,
        length: int | None = None,
        pattern: Pattern | None = None,
        seed: int | None = None,
    ) -> BleSent:
        """Sends over the Nordic UART service (the central writes, the peripheral notifies); `data`, or `length`
        bytes the firmware generates. Returns the length and CRC-32 of what it sent."""
        payload = data if data is not None else b""
        response = self._cmd("send", payload, len=length, pattern=pattern, seed=seed)
        return BleSent(length=response.as_int("len"), crc=int(response["crc"], 16))

    def pair(self) -> None:
        self._cmd("pair")

    def security(self, level: int) -> None:
        self._cmd("security", level=level)

    def bonds(self) -> tuple[int, int]:
        """The number of stored bonds and the capacity."""
        response = self._cmd("bonds")
        return response.as_int("count"), response.as_int("max")

    def removebonds(self) -> None:
        self._cmd("removebonds")

    def wait(self, name: str, timeout: float, **fields: object) -> Event:
        """The oldest queued or next `EVT ble <name>` whose fields equal `fields` (compared as text)."""
        wanted = {key: str(value) for key, value in fields.items()}

        def accepted(event: Event) -> bool:
            return event.args[:1] == (name,) and all(event.get(key) == value for key, value in wanted.items())

        return self._fw.terminal.wait_event("ble", accepted, timeout)

    def absent(self, name: str, window: float, **fields: object) -> bool:
        """True when no such event arrives within `window` seconds."""
        try:
            self.wait(name, window, **fields)
        except TerminalTimeout:
            return True
        return False

    def drain(self, name: str | None = None) -> list[Event]:
        events = self._fw.terminal.drain_events("ble")
        return [event for event in events if name is None or event.args[:1] == (name,)]

    def wait_link(self, predicate: Callable[[BleLink], bool], timeout: float, interval: float = 0.1) -> BleLink:
        """Polls `link` until `predicate` holds (the controller applies a change some connection events later)."""
        deadline = time.monotonic() + timeout
        while True:
            link = self.link()
            if predicate(link):
                return link
            if time.monotonic() >= deadline:
                raise TerminalTimeout(f"ble.link matching {predicate}", timeout)
            self._fw.terminal.pump(interval)


GROUPS = {"ble": Ble}
