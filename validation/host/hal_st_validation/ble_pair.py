"""A BLE pair: two boards running `hal_st.validation_ble_firmware`, one as central and one as peripheral.

`BleSettings` merges the `tests.ble` sections of both board files (the slower timeout wins, a capability must be on
both boards). `BlePair` holds the two ends and the flows the scenarios share: connect, enable the Nordic UART
service, send and verify a payload, and the cleanup that returns both boards to idle between tests.

Addresses are compared as the firmware prints them: lower case, colon separated.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable
from dataclasses import dataclass, field

from ad3_waveforms_bench.protocol import Event
from ad3_waveforms_bench.terminal import FirmwareError, TerminalTimeout

from .config import BoardConfig
from .firmware import Firmware
from .groups.ble import Ble, BleLink, BleSent, Role
from .patterns import Pattern, crc_text, generate

__all__ = ["BleEnd", "BlePair", "BleSettings"]


@dataclass(frozen=True)
class BleSettings:
    """Seconds for the timeouts; `rssi_min` in dBm. Defaults apply to a board file without `tests.ble`."""

    connect_timeout: float = 5.0
    scan_timeout: float = 5.0
    event_timeout: float = 3.0
    quiet_window: float = 0.5
    rssi_min: int = -90
    reconnect_cycles: int = 3
    reconnect_cycles_full: int = 20
    phys: tuple[str, ...] = ("1m", "2m")

    @classmethod
    def from_boards(cls, boards: Iterable[BoardConfig]) -> BleSettings:
        sections = [board.param("ble", {}) for board in boards]
        defaults = cls()

        def values(key: str, default: object) -> list:
            return [section.get(key, default) for section in sections]

        phys = [tuple(section.get("phys", defaults.phys)) for section in sections]
        common = tuple(phy for phy in phys[0] if all(phy in other for other in phys))
        return cls(
            connect_timeout=max(values("connect_timeout", defaults.connect_timeout)),
            scan_timeout=max(values("scan_timeout", defaults.scan_timeout)),
            event_timeout=max(values("event_timeout", defaults.event_timeout)),
            quiet_window=max(values("quiet_window", defaults.quiet_window)),
            rssi_min=min(values("rssi_min", defaults.rssi_min)),
            reconnect_cycles=min(values("reconnect_cycles", defaults.reconnect_cycles)),
            reconnect_cycles_full=min(values("reconnect_cycles_full", defaults.reconnect_cycles_full)),
            phys=common,
        )


@dataclass
class BleEnd:
    """One board of the pair."""

    fw: Firmware
    board: BoardConfig
    role: Role
    address: str = ""

    @property
    def ble(self) -> Ble:
        return self.fw.ble


@dataclass
class BlePair:
    central: BleEnd
    peripheral: BleEnd
    settings: BleSettings
    name: str = ""
    changed: set[str] = field(default_factory=set)

    @classmethod
    def open(cls, central: BleEnd, peripheral: BleEnd, settings: BleSettings, name: str = "") -> BlePair:
        """Both boards have just reset: open the roles and wait for the controllers."""
        for end in (central, peripheral):
            end.address = end.ble.open(end.role)
        pair = cls(central, peripheral, settings, name)
        for end in pair.ends:
            end.ble.wait("ready", settings.event_timeout + settings.connect_timeout)
        return pair

    @property
    def ends(self) -> tuple[BleEnd, BleEnd]:
        return self.central, self.peripheral

    def other(self, end: BleEnd) -> BleEnd:
        return self.peripheral if end is self.central else self.central

    def wait_both(self, name: str, timeout: float | None = None, **fields: object) -> tuple[Event, Event]:
        """`EVT ble <name>` on the central, then on the peripheral."""
        limit = self.settings.event_timeout if timeout is None else timeout
        return self.central.ble.wait(name, limit, **fields), self.peripheral.ble.wait(name, limit, **fields)

    def connect(self, interval: int | None = None) -> tuple[Event, Event]:
        """The peripheral advertises and the central connects to its address; returns the `connected` events of the
        central and the peripheral."""
        timeout = self.settings.connect_timeout
        self.peripheral.ble.advertise(interval=interval)
        self.peripheral.ble.wait("state", self.settings.event_timeout, value="advertising")
        self.central.ble.connect(self.peripheral.address, "public", timeout=int(timeout * 1000))
        limit = timeout + self.settings.event_timeout
        return (
            self.central.ble.wait("connected", limit, peer=self.peripheral.address),
            self.peripheral.ble.wait("connected", limit, peer=self.central.address),
        )

    def disconnect(self, initiator: BleEnd) -> dict[str, Event]:
        """`initiator` ends the link; returns the `disconnected` event of each role."""
        initiator.ble.disconnect()
        other = self.other(initiator)
        return {
            initiator.role: initiator.ble.wait("disconnected", self.settings.event_timeout),
            other.role: other.ble.wait("disconnected", self.settings.event_timeout),
        }

    def idle(self, end: BleEnd) -> BleLink:
        """Waits until `end` is in the `standby` state without a link."""
        return self.wait_link(end, lambda link: link.state == "standby" and not link.connected)

    def enable_uart(self, exchange_mtu: bool = True) -> int:
        """Optionally exchanges the MTU, then discovers and subscribes Nordic UART; returns the largest payload."""
        if exchange_mtu:
            self.central.ble.mtu()
            self.wait_both("mtu")
        self.central.ble.discover()
        central_uart = self.central.ble.wait("uart", self.settings.event_timeout, state="open")
        self.peripheral.ble.wait("uart", self.settings.event_timeout, state="open")
        return central_uart.as_int("maxsend")

    def send(self, sender: BleEnd, length: int, pattern: Pattern = "inc", seed: int = 0) -> BleSent:
        """`sender` sends `length` generated bytes; checks what the firmware generated against the host's pattern."""
        sent = sender.ble.send(length=length, pattern=pattern, seed=seed)
        expected = int(crc_text(generate(length, pattern, seed)), 16)
        assert sent.length == length, f"{sender.role} sent {sent.length} bytes instead of {length}"
        assert sent.crc == expected, f"{sender.role} generated the wrong payload: crc {sent.crc:08x}, expected {expected:08x}"
        sender.ble.wait("sent", self.settings.event_timeout)
        return sent

    def expect(self, receiver: BleEnd, sent: BleSent) -> None:
        """`receiver` got a payload of the length and CRC of `sent`."""
        received = receiver.ble.wait("rx", self.settings.event_timeout)
        assert received.as_int("len") == sent.length, f"{receiver.role} received {received['len']} bytes instead of {sent.length}"
        assert int(received["crc"], 16) == sent.crc, f"{receiver.role} received crc {received['crc']}, sent {sent.crc:08x}"

    def transfer(self, sender: BleEnd, length: int, pattern: Pattern = "inc", seed: int = 0) -> None:
        """`sender` sends `length` generated bytes and the other board receives the same length and CRC."""
        self.expect(self.other(sender), self.send(sender, length, pattern, seed))

    def wait_link(self, end: BleEnd, predicate: Callable[[BleLink], bool], timeout: float | None = None) -> BleLink:
        return end.ble.wait_link(predicate, self.settings.event_timeout if timeout is None else timeout)

    def cleanup(self) -> list[str]:
        """Idle on both boards: scanning, connecting, advertising and the link end, bonds and the security level go
        back to their defaults, and the event queues are emptied. Returns what failed."""
        failures: list[str] = []

        def attempt(label: str, action: Callable[[], object]) -> None:
            try:
                action()
            except (FirmwareError, TerminalTimeout) as error:
                failures.append(f"{label}: {error}")

        central, peripheral = self.central.ble, self.peripheral.ble
        attempt("central cancel", central.cancel)
        attempt("central stopscan", central.stopscan)
        attempt("peripheral standby", peripheral.standby)
        attempt("central disconnect", central.disconnect)
        attempt("peripheral disconnect", peripheral.disconnect)
        attempt("peripheral allowpairing", lambda: peripheral.allow_pairing(True))
        for end in self.ends:
            attempt(f"{end.role} link down", lambda end=end: self.wait_link(end, lambda link: not link.connected))
            attempt(f"{end.role} removebonds", end.ble.removebonds)
            if "security" in self.changed:
                attempt(f"{end.role} security", lambda end=end: end.ble.security(1))
        self.changed.clear()
        for end in self.ends:
            attempt(f"{end.role} quiesce", lambda end=end: end.fw.terminal.pump(0.1))
            end.ble.drain()
        return failures
