from __future__ import annotations

import warnings
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field

from ad3_waveforms_bench.protocol import Event
from ad3_waveforms_bench.terminal import FirmwareError, TerminalTimeout

from .config import BoardConfig
from .firmware import Firmware
from .groups.ble import Ble, BleLink, BleSent, Role
from .patterns import Pattern, crc_text, generate

__all__ = ["FAILED_TO_BE_ESTABLISHED", "BleEnd", "BlePair", "BleSettings"]

FAILED_TO_BE_ESTABLISHED = 0x3E


@dataclass(frozen=True)
class BleSettings:
    connect_timeout: float = 5.0
    connect_attempts: int = 3
    mtu: int = 251
    conn_pause_peripheral: float = 5.0
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
            connect_attempts=min(values("connect_attempts", defaults.connect_attempts)),
            mtu=min(values("mtu", defaults.mtu)),
            conn_pause_peripheral=max(values("conn_pause_peripheral", defaults.conn_pause_peripheral)),
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
    retries: int = 0

    @classmethod
    def open(cls, central: BleEnd, peripheral: BleEnd, settings: BleSettings, name: str = "") -> BlePair:
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
        limit = self.settings.event_timeout if timeout is None else timeout
        return self.central.ble.wait(name, limit, **fields), self.peripheral.ble.wait(name, limit, **fields)

    def connect(self, interval: int | None = None) -> tuple[Event, Event]:
        settings = self.settings
        limit = settings.connect_timeout + settings.event_timeout
        self.peripheral.ble.advertise(interval=interval)
        self.peripheral.ble.wait("state", settings.event_timeout, value="advertising")
        attempt = 1
        while True:
            self.central.ble.connect(self.peripheral.address, "public", timeout=int(settings.connect_timeout * 1000))
            central = self.central.ble.wait("connected", limit, peer=self.peripheral.address)
            try:
                return central, self.peripheral.ble.wait("connected", settings.event_timeout, peer=self.central.address)
            except TerminalTimeout:
                if attempt >= settings.connect_attempts or not self.link_not_established():
                    raise
            self.retries += 1
            warnings.warn(
                f"BLE connect attempt {attempt} of {settings.connect_attempts} ended with reason {FAILED_TO_BE_ESTABLISHED}",
                stacklevel=2,
            )
            self.central.ble.drain()
            attempt += 1

    def link_not_established(self) -> bool:
        return not self.central.ble.absent("disconnected", self.settings.event_timeout, reason=FAILED_TO_BE_ESTABLISHED)

    def disconnect(self, initiator: BleEnd) -> dict[str, Event]:
        initiator.ble.disconnect()
        other = self.other(initiator)
        return {
            initiator.role: initiator.ble.wait("disconnected", self.settings.event_timeout),
            other.role: other.ble.wait("disconnected", self.settings.event_timeout),
        }

    def idle(self, end: BleEnd) -> BleLink:
        return self.wait_link(end, lambda link: link.state == "standby" and not link.connected)

    def enable_uart_limits(self, exchange_mtu: bool = True) -> tuple[int, int]:
        if exchange_mtu:
            self.central.ble.mtu()
            self.wait_both("mtu")
        self.central.ble.discover()
        central_uart = self.central.ble.wait("uart", self.settings.event_timeout, state="open")
        peripheral_uart = self.peripheral.ble.wait("uart", self.settings.event_timeout, state="open")
        return central_uart.as_int("maxsend"), peripheral_uart.as_int("maxsend")

    def enable_uart(self, exchange_mtu: bool = True) -> int:
        return self.enable_uart_limits(exchange_mtu)[0]

    def send(self, sender: BleEnd, length: int, pattern: Pattern = "inc", seed: int = 0) -> BleSent:
        sent = sender.ble.send(length=length, pattern=pattern, seed=seed)
        expected = int(crc_text(generate(length, pattern, seed)), 16)
        assert sent.length == length, f"{sender.role} sent {sent.length} bytes instead of {length}"
        assert sent.crc == expected, f"{sender.role} generated the wrong payload: crc {sent.crc:08x}, expected {expected:08x}"
        sender.ble.wait("sent", self.settings.event_timeout)
        return sent

    def expect(self, receiver: BleEnd, sent: BleSent) -> None:
        received = receiver.ble.wait("rx", self.settings.event_timeout)
        assert received.as_int("len") == sent.length, f"{receiver.role} received {received['len']} bytes instead of {sent.length}"
        assert int(received["crc"], 16) == sent.crc, f"{receiver.role} received crc {received['crc']}, sent {sent.crc:08x}"

    def transfer(self, sender: BleEnd, length: int, pattern: Pattern = "inc", seed: int = 0) -> None:
        self.expect(self.other(sender), self.send(sender, length, pattern, seed))

    def wait_link(self, end: BleEnd, predicate: Callable[[BleLink], bool], timeout: float | None = None) -> BleLink:
        return end.ble.wait_link(predicate, self.settings.event_timeout if timeout is None else timeout)

    def cleanup(self) -> list[str]:
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
