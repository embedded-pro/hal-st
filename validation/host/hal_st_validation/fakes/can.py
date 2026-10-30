"""Fake `can` group: EMIL's `HilCanCommands` over validation/firmware/CanFactory.cpp, with the argument checks in their
order, one FDCAN controller open at a time on pins with its `canRx`/`canTx` function (FDCAN1 on `_Family.default_can`
without pins), the bit rates `groups.can.bitrate_achievable` accepts at `_Family.fdcan_clock`, and the acceptance
filter of `groups.can.filter_accepts`.

Frames: `loopback=1` receives its own frames and, as the external loopback mode drives the TX pin, still sends them on
the bus; `loopback=0` sends on the bus and receives from it. The bus is the python-can virtual channel
`FakeFirmware.can_bus` (the `--fake` CAN peer), or none. Every send succeeds at once: the fake models no
acknowledge, bit errors, error states or bus off.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

from ad3_waveforms_bench.protocol import format_hex

from ..groups.can import (
    BITRATE_MAX,
    DATA_MAX,
    EXTENDED_ID_MAX,
    STANDARD_ID_MAX,
    TIMING_MAX,
    CanFilter,
    bitrate_achievable,
    filter_accepts,
)
from .base import FakeError, FakeGroup, _fail, _flag, _hex, _number, _parse_uint, _shape, _tokens

if TYPE_CHECKING:
    from ..fake_firmware import FakeFirmware

__all__ = ["FakeCan"]

_OPEN_KEYS = ("rx", "tx", "bitrate", "timing", "filter", "loopback", "recover")
_DEFAULT_BITRATE = "500000"


def _parsed(text: str) -> int | None:
    """`HilArguments::ParseNumber`: None where the firmware gets no number."""
    try:
        return _parse_uint(text)
    except FakeError:
        return None


def _filter(text: str) -> CanFilter:
    """`ParseFilter`: three or four numbers with id type and match 0 or 1 (`usage`), ids of the id type (`range`)."""
    fields = _tokens(text)
    if not 3 <= len(fields) <= 4:
        _fail("usage")
    ident, mask, ext = (_parsed(field) for field in fields[:3])
    match = _parsed(fields[3]) if len(fields) == 4 else 1
    if ident is None or mask is None or ext is None or match is None or ext > 1 or match > 1:
        _fail("usage")
    assert ident is not None and mask is not None
    limit = EXTENDED_ID_MAX if ext else STANDARD_ID_MAX
    if ident > limit or mask > limit:
        _fail("range")
    return ident, mask, bool(ext), bool(match)


def _timing(text: str) -> tuple[int, int, int, int]:
    """`ParseTiming`: four numbers (`usage`), each from 1 to its limit and `sjw` at most `tseg2` (`range`)."""
    fields = _tokens(text)
    if len(fields) != len(TIMING_MAX):
        _fail("usage")
    values = []
    for field, maximum in zip(fields, TIMING_MAX):
        value = _parsed(field)
        if value is None:
            _fail("usage")
        assert value is not None
        if not 1 <= value <= maximum:
            _fail("range")
        values.append(value)
    tseg1, tseg2, sjw, brp = values
    if sjw > tseg2:
        _fail("range")
    return tseg1, tseg2, sjw, brp


class FakeCan(FakeGroup):
    prefix = "can"

    def __init__(self, fw: FakeFirmware) -> None:
        super().__init__(fw)
        self.buses: dict[int, Any] = {}

    def boot(self) -> None:
        for bus in self.buses.values():
            bus.shutdown()
        self.buses.clear()

    def _index(self, text: str) -> int:
        """`HilSingleInstance::Parse`: 0 to `Instances() - 1`."""
        return _number(text, 0, self.fw.spec.can_instances - 1)

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _OPEN_KEYS)
        index = self._index(args[0])
        if index == 0:
            _fail("range")
        rx, tx = self.fw.pin(options.get("rx")), self.fw.pin(options.get("tx"))
        bitrate = _number(options.get("bitrate", _DEFAULT_BITRATE), 1, BITRATE_MAX)
        loopback = _flag(options, "loopback")
        _flag(options, "recover", True)
        can_filter = _filter(options["filter"]) if "filter" in options else None
        if "timing" in options:
            if "bitrate" in options:
                _fail("usage")
            _timing(options["timing"])
        elif not bitrate_achievable(self.fw.spec.fdcan_clock, bitrate):
            _fail("range")
        if rx is None and tx is None:
            default = self.fw.spec.default_can
            if default is None or default[0] != index:
                _fail("usage")
            assert default is not None
            rx, tx = default[1], default[2]
        if rx is None or tx is None:
            _fail("usage")
        if not self.fw.supports("canRx", index, rx) or not self.fw.supports("canTx", index, tx):
            _fail("pin")
        self.fw.open_instance(self.prefix, str(index), {"loopback": loopback, "filter": can_filter}, [rx, tx])
        if self.fw.can_bus is not None:
            import can

            self.buses[index] = can.Bus(interface="virtual", channel=self.fw.can_bus)
        return "OK"

    def cmd_send(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 3, 3, ("ext",))
        index = self._index(args[0])
        state = self.fw.find_instance(self.prefix, str(index))
        ext = _flag(options, "ext")
        ident = _number(args[1], 0, EXTENDED_ID_MAX if ext else STANDARD_ID_MAX)
        data = _hex(args[2], DATA_MAX)
        if state["loopback"] and filter_accepts(state["filter"], ident, ext):
            self.fw.event(self.frame_event(index, ident, ext, data))
        bus = self.buses.get(index)
        if bus is not None:
            import can

            bus.send(can.Message(arbitration_id=ident, is_extended_id=ext, data=data))
        return "OK"

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        index = self._index(args[0])
        self.fw.find_instance(self.prefix, str(index))
        bus = self.buses.pop(index, None)
        if bus is not None:
            bus.shutdown()
        return self.fw.close_instance(self.prefix, str(index))

    @staticmethod
    def frame_event(index: int, ident: int, ext: bool, data: bytes) -> str:
        """`HilCanCommands::Received`: the id in decimal, no data as `data=`."""
        return f"EVT can index={index} id={ident} ext={int(ext)} data={format_hex(data)}"

    def poll(self) -> None:
        """Frames from the bus reach a controller in normal mode; the external loopback mode ignores the RX pin."""
        for index, bus in self.buses.items():
            state = self.fw.opened.get((self.prefix, str(index)))
            while (message := bus.recv(0)) is not None:
                if state is None or state["loopback"] or message.is_error_frame or message.is_remote_frame:
                    continue
                ext = bool(message.is_extended_id)
                if filter_accepts(state["filter"], message.arbitration_id, ext):
                    self.fw.event(self.frame_event(index, message.arbitration_id, ext, bytes(message.data)[:DATA_MAX]))
