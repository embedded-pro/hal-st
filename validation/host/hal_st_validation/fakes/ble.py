from __future__ import annotations

import re
from dataclasses import dataclass
from typing import TYPE_CHECKING

from ..patterns import crc_text
from .base import Command, FakeGroup, _choice, _fail, _flag, _number, _payload, _shape

if TYPE_CHECKING:
    from ..fake_firmware import FakeFirmware

__all__ = ["FakeAir", "FakeBle"]

_ADDRESS_RE = re.compile(r"^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$")
_DEFAULT_ADDRESS = {"peripheral": "0a:00:00:e1:80:02", "central": "0b:00:00:e1:80:02"}
_DEFAULT_NAME = "hal-st-periph"
_NAME_MAX = 16
_BLE_FAMILIES = ("stm32wb55", "stm32wba55")
_ROLES = ("central", "peripheral")
_PHYS = ("1m", "2m", "coded")
_ADDRESS_TYPES = ("public", "random")
_ADVERTISE_INTERVAL_MS = (20, 10240)
_DEFAULT_ADVERTISE_INTERVAL_MS = 100
_SCAN_TIMEOUT_MS_MAX = 600_000
_CONNECT_TIMEOUT_MS = (100, 60_000)
_DEFAULT_CONNECT_TIMEOUT_MS = 5000
_SCAN_TABLE = 8
_RSSI = -45
_MTU_DEFAULT = 23
_MTU_MAX = 251
_ATT_HEADER = 3
_PAYLOAD_MAX = _MTU_MAX - _ATT_HEADER
_BONDS_MAX = 10
_STACK = "fake"
_DEFAULT_PARAMETERS = (24, 40, 0, 500)
_INTERVAL_RANGE = (6, 3200)
_LATENCY_MAX = 499
_TIMEOUT_RANGE = (10, 3200)
_LOCAL_TERMINATED = "22"
_REMOTE_TERMINATED = "19"
_SUPERVISION_TIMEOUT = "8"
_FAILED_TO_BE_ESTABLISHED = "62"
_DATA_LENGTH_TIME_US = {"1m": 2120, "2m": 1064, "coded": 17040}


def _address(text: str | None) -> str:
    if text is None or _ADDRESS_RE.match(text) is None:
        _fail("usage")
    assert text is not None
    return text.lower()


@dataclass
class _Link:
    central: FakeBle
    peripheral: FakeBle
    interval: int
    latency: int
    timeout: int
    tx_phy: str = "1m"
    rx_phy: str = "1m"
    tx_octets: int = 27
    rx_octets: int = 27
    central_rx_known: bool = True
    mtu: int = _MTU_DEFAULT
    uart: bool = False

    def other(self, end: FakeBle) -> FakeBle:
        return self.peripheral if end is self.central else self.central


class FakeAir:
    def __init__(self) -> None:
        self.ends: list[FakeBle] = []
        self.drop_connects = 0

    def join(self, end: FakeBle) -> None:
        if end not in self.ends:
            self.ends.append(end)

    def leave(self, end: FakeBle) -> None:
        if end in self.ends:
            self.ends.remove(end)

    def advertisers(self) -> list[FakeBle]:
        return [end for end in self.ends if end.role == "peripheral" and end.state == "advertising"]

    def advertiser(self, address: str) -> FakeBle | None:
        return next((end for end in self.advertisers() if end.address == address), None)

    def centrals(self, state: str) -> list[FakeBle]:
        return [end for end in self.ends if end.role == "central" and end.state == state]


class FakeBle(FakeGroup):
    prefix = "ble"

    def __init__(self, fw: FakeFirmware) -> None:
        super().__init__(fw)
        self._reset_state()

    def _reset_state(self) -> None:
        self.role: str | None = None
        self.address = ""
        self.state = "standby"
        self.name = _DEFAULT_NAME
        self.advertise_interval = _DEFAULT_ADVERTISE_INTERVAL_MS
        self.allow = True
        self.bonded_only = False
        self.link: _Link | None = None
        self.parameters = _DEFAULT_PARAMETERS
        self.scan_filter: tuple[str | None, str | None] = (None, None)
        self.scan_seen: set[str] = set()
        self.scan_deadline: float | None = None
        self.connect_target: str | None = None
        self.connect_deadline: float | None = None
        self.security_level = 1
        self.bonds: set[str] = set()

    @property
    def air(self) -> FakeAir:
        if self.fw.air is None:
            self.fw.air = FakeAir()
        return self.fw.air

    def _line(self, kind: str, /, **fields: object) -> str:
        return " ".join(["EVT ble", kind, *[f"{key}={value}" for key, value in fields.items()]])

    def _event(self, kind: str, /, **fields: object) -> None:
        self.fw.event(self._line(kind, **fields))

    def _tell(self, end: FakeBle, kind: str, /, **fields: object) -> None:
        end.fw.event(end._line(kind, **fields))

    def _set_state(self, state: str) -> None:
        if state != self.state:
            self.state = state
            self._event("state", value=state)

    def handler(self, verb: str) -> Command | None:
        if self.fw.family not in _BLE_FAMILIES:
            return None
        return super().handler(verb)

    def boot(self) -> None:
        if self.link is not None:
            self._drop_link(self.link, local=None, remote_reason=_SUPERVISION_TIMEOUT)
        if self.role is not None:
            self.air.leave(self)
        self._reset_state()

    def poll(self) -> None:
        now = self.fw.clock()
        if self.scan_deadline is not None and now >= self.scan_deadline:
            self.scan_deadline = None
            self._set_state("standby")
        if self.connect_deadline is not None and now >= self.connect_deadline:
            self._connect_failed("timeout")

    def _need_role(self, role: str | None = None) -> None:
        if self.role is None:
            _fail("notopen")
        if role is not None and self.role != role:
            _fail("unsupported")

    def _need_link(self) -> _Link:
        if self.link is None:
            _fail("failed")
        assert self.link is not None
        return self.link

    def cmd_info(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        ready = int(self.role is not None)
        return f"OK ready={ready} role={self.role or 'none'} address={self.address or '-'} stack={_STACK}"

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("role", "address"))
        if "role" not in options:
            _fail("usage")
        role = _choice(options, "role", _ROLES, "")
        address = _address(options["address"]) if "address" in options else _DEFAULT_ADDRESS[role]
        if self.role is not None:
            _fail("busy")
        self.role = role
        self.address = address
        self.air.join(self)
        self._event("ready")
        return f"OK address={address}"

    def cmd_advertise(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("interval", "name"))
        interval = _number(options.get("interval", str(_DEFAULT_ADVERTISE_INTERVAL_MS)), *_ADVERTISE_INTERVAL_MS)
        name = options.get("name", _DEFAULT_NAME)
        if len(name) > _NAME_MAX:
            _fail("range")
        self._need_role("peripheral")
        if self.state == "connected":
            _fail("failed")
        self.name = name
        self.advertise_interval = interval
        self.bonded_only = not self.allow
        restarted = self.state == "advertising"
        self._set_state("advertising")
        if not restarted:
            self._announce()
        return "OK"

    def cmd_standby(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role("peripheral")
        if self.link is not None:
            self._drop_link(self.link, local=self, remote_reason=_REMOTE_TERMINATED)
        else:
            self._set_state("standby")
        return "OK"

    def cmd_allowpairing(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        allow = _flag({"allow": args[0]}, "allow")
        self._need_role("peripheral")
        self.allow = allow
        return "OK"

    def cmd_scan(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("addr", "name", "timeout"))
        address = _address(options["addr"]) if "addr" in options else None
        name = options.get("name")
        timeout = _number(options["timeout"], 1, _SCAN_TIMEOUT_MS_MAX) if "timeout" in options else None
        self._need_role("central")
        if self.state != "standby":
            _fail("busy")
        self.scan_filter = (address, name)
        self.scan_seen = set()
        self.scan_deadline = None if timeout is None else self.fw.clock() + timeout / 1000
        self._set_state("scanning")
        for advertiser in self.air.advertisers():
            self._report(advertiser)
        return "OK"

    def cmd_stopscan(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role("central")
        if self.state == "scanning":
            self.scan_deadline = None
            self._set_state("standby")
        return "OK"

    def cmd_connect(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("type", "timeout"))
        address = _address(args[0])
        address_type = _choice(options, "type", _ADDRESS_TYPES, "public")
        timeout = _number(options.get("timeout", str(_DEFAULT_CONNECT_TIMEOUT_MS)), *_CONNECT_TIMEOUT_MS)
        self._need_role("central")
        if self.state != "standby":
            _fail("busy")
        self.connect_target = address if address_type == "public" else f"random:{address}"
        self.connect_deadline = self.fw.clock() + timeout / 1000
        self._set_state("connecting")
        advertiser = self.air.advertiser(address) if address_type == "public" else None
        if advertiser is not None:
            self._initiate(advertiser)
        return "OK"

    def cmd_cancel(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role("central")
        if self.state == "connecting":
            self._connect_failed("cancelled")
        return "OK"

    def cmd_disconnect(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role()
        if self.link is not None:
            self._drop_link(self.link, local=self, remote_reason=_REMOTE_TERMINATED)
        return "OK"

    def cmd_phy(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("tx", "rx"))
        if "tx" not in options or "rx" not in options:
            _fail("usage")
        tx = _choice(options, "tx", _PHYS, "")
        rx = _choice(options, "rx", _PHYS, "")
        self._need_role("central")
        link = self._need_link()
        changed = (link.tx_phy, link.rx_phy) != (tx, rx)
        link.tx_phy, link.rx_phy = tx, rx
        self._event("phy", tx=tx, rx=rx)
        if changed:
            self._tell(link.peripheral, "phy", tx=rx, rx=tx)
        return "OK"

    def cmd_datalength(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role("central")
        link = self._need_link()
        link.tx_octets = link.rx_octets = _MTU_MAX
        link.central_rx_known = False
        time_us = _DATA_LENGTH_TIME_US[link.tx_phy]
        self._tell(link.central, "datalength", txoctets=_MTU_MAX, txtime=time_us)
        self._tell(link.peripheral, "datalength", txoctets=_MTU_MAX, rxoctets=_MTU_MAX, txtime=time_us, rxtime=time_us)
        return "OK"

    def cmd_mtu(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role("central")
        link = self._need_link()
        link.mtu = _MTU_MAX
        for end in (link.central, link.peripheral):
            self._tell(end, "mtu", size=link.mtu)
        return "OK"

    def cmd_discover(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role("central")
        link = self._need_link()
        if link.uart:
            _fail("busy")
        link.uart = True
        for end in (link.central, link.peripheral):
            self._tell(end, "uart", state="open", maxsend=link.mtu - _ATT_HEADER)
        return "OK"

    def cmd_conparams(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("min", "max", "latency", "timeout"))
        if any(key not in options for key in ("min", "max", "latency", "timeout")):
            _fail("usage")
        low = _number(options["min"], *_INTERVAL_RANGE)
        high = _number(options["max"], *_INTERVAL_RANGE)
        latency = _number(options["latency"], 0, _LATENCY_MAX)
        timeout = _number(options["timeout"], *_TIMEOUT_RANGE)
        if low > high or timeout * 10 <= (1 + latency) * high * 1.25 * 2:
            _fail("range")
        self._need_role()
        link = self._need_link()
        requested = (low, high, latency, timeout)
        if self.role == "central":
            self.parameters = requested
        else:
            requested = link.central.parameters
        self._apply_parameters(link, requested)
        return "OK"

    def cmd_link(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role()
        link = self.link
        if link is None:
            return f"OK state={self.state} connected=0"
        peer = link.other(self)
        tx, rx = (link.tx_phy, link.rx_phy) if self.role == "central" else (link.rx_phy, link.tx_phy)
        rx_octets = "-" if self.role == "central" and not link.central_rx_known else link.rx_octets
        return (
            f"OK state={self.state} connected=1 peer={peer.address} interval={link.interval} latency={link.latency} timeout={link.timeout} "
            f"txphy={tx} rxphy={rx} txoctets={link.tx_octets} rxoctets={rx_octets} mtu={link.mtu}"
        )

    def cmd_send(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("len", "pattern", "seed"))
        data = _payload(args, options, 0, _PAYLOAD_MAX)
        if not data:
            _fail("range")
        self._need_role()
        link = self.link
        if link is None or not link.uart:
            _fail("notopen")
        assert link is not None
        if len(data) > link.mtu - _ATT_HEADER:
            _fail("range")
        crc = crc_text(data)
        self._event("sent")
        self._tell(link.other(self), "rx", len=len(data), crc=crc)
        return f"OK len={len(data)} crc={crc}"

    def cmd_security(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0, ("level",))
        if "level" not in options:
            _fail("usage")
        level = _number(options["level"], 1, 2)
        self._need_role()
        self.security_level = level
        self._event("security", result=0)
        return "OK"

    def cmd_pair(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role()
        link = self._need_link()
        peer = link.other(self)
        for end, other in ((self, peer), (peer, self)):
            if len(end.bonds) < _BONDS_MAX:
                end.bonds.add(other.address)
            self._tell(end, "paired", secure=1, auth=0, keysize=16)
        return "OK"

    def cmd_bonds(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role()
        return f"OK count={len(self.bonds)} max={_BONDS_MAX}"

    def cmd_removebonds(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        self._need_role()
        self.bonds.clear()
        self._event("bonds", count=0)
        return "OK"

    def _announce(self) -> None:
        for scanner in self.air.centrals("scanning"):
            scanner._report(self)
        for initiator in self.air.centrals("connecting"):
            if initiator.connect_target == self.address:
                initiator._initiate(self)
                break

    def accepts(self, initiator: FakeBle) -> bool:
        return not self.bonded_only or initiator.address in self.bonds

    def _report(self, advertiser: FakeBle) -> None:
        address, name = self.scan_filter
        if advertiser.address in self.scan_seen or (address is not None and advertiser.address != address):
            return
        if name is not None and advertiser.name != name:
            return
        if len(self.scan_seen) >= _SCAN_TABLE:
            return
        self.scan_seen.add(advertiser.address)
        self._event("found", addr=advertiser.address, type="public", event="advInd", rssi=_RSSI, name=advertiser.name)

    def _initiate(self, peripheral: FakeBle) -> None:
        if self.air.drop_connects > 0:
            self.air.drop_connects -= 1
            self._not_established(peripheral)
        elif peripheral.accepts(self):
            self._connect_to(peripheral)
        else:
            self._not_established(peripheral)

    def _not_established(self, peripheral: FakeBle) -> None:
        _, interval, latency, timeout = self.parameters
        self.connect_target = self.connect_deadline = None
        self._set_state("connected")
        self._event("connected", peer=peripheral.address, interval=interval, latency=latency, timeout=timeout)
        self._set_state("standby")
        self._event("disconnected", reason=_FAILED_TO_BE_ESTABLISHED)

    def _connect_to(self, peripheral: FakeBle) -> None:
        interval = self.parameters[1]
        link = _Link(self, peripheral, interval, self.parameters[2], self.parameters[3])
        self.link = peripheral.link = link
        self.connect_target = self.connect_deadline = None
        self._set_state("connected")
        peripheral._set_state("connected")
        self._event("connected", peer=peripheral.address, interval=link.interval, latency=link.latency, timeout=link.timeout)
        self._tell(peripheral, "connected", peer=self.address, interval=link.interval, latency=link.latency, timeout=link.timeout)

    def _connect_failed(self, reason: str) -> None:
        self.connect_target = self.connect_deadline = None
        self._event("connectfailed", reason=reason)
        self._set_state("standby")

    def _apply_parameters(self, link: _Link, parameters: tuple[int, int, int, int]) -> None:
        _, high, latency, timeout = parameters
        if (link.interval, link.latency, link.timeout) == (high, latency, timeout):
            return
        link.interval, link.latency, link.timeout = high, latency, timeout
        for end in (link.central, link.peripheral):
            self._tell(end, "conparams", interval=high, latency=latency, timeout=timeout)

    def _drop_link(self, link: _Link, local: FakeBle | None, remote_reason: str) -> None:
        for end in (link.central, link.peripheral):
            end.link = None
            end.state = "standby"
        for end in (link.central, link.peripheral):
            if end is self and local is None:
                continue
            reason = _LOCAL_TERMINATED if end is local else remote_reason
            if link.uart:
                self._tell(end, "uart", state="closed")
            self._tell(end, "disconnected", reason=reason)
            self._tell(end, "state", value="standby")
