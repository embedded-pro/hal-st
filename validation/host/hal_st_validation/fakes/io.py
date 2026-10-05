"""Fake `sgpio` and `clock` groups: the argument checks of validation/firmware/SyncGpioGroup.cpp and ClockGroup.cpp in
their order, the pins `sgpio` holds in the pin pool (four outputs, four alternate-function pins and one `multi` set),
and the default clock tree of each board.

The fake firmware's unique device ID follows the real layout here (lot number bytes in ASCII), so `test_uid.py` runs
against it; a UID passed to `FakeFirmware` explicitly is kept."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

from ..groups.io import MCO_DIVIDERS, MCO_SOURCES, SPEEDS
from .base import FakeGroup, _choice, _fail, _flag, _number, _shape, _tokens

if TYPE_CHECKING:
    from ..fake_firmware import FakeFirmware

_OUTPUT_SLOTS = 4
_PERIPHERAL_SLOTS = 4
_MULTI_PINS = 4
_CHANNELS = 4
_ALTERNATE_FUNCTION_MAX = 15
# `hal::peripheralTimer` holds TIM1 .. TIM17.
_TIMER_TABLE_SIZE = 17
# The timer channel alternate functions of the generated pinout tables (GPIO_AF1_TIM1, GPIO_AF1_TIM2, GPIO_AF2_TIM3,
# GPIO_AF14_TIM16, GPIO_AF14_TIM17), the same on both boards.
_TIMER_AF = {1: 1, 2: 1, 3: 2, 16: 14, 17: 14}
# X 0x0024, Y 0x0051, wafer 7, lot "HALSTFK".
FAKE_UID = "2400510007" + b"HALSTFK".hex()

_CLOCK_TREES: dict[str, dict[str, Any]] = {
    "stm32wb55": {"buses": ("sysclk", "hclk", "pclk1", "pclk2"), "flags": {"hse": 1, "lse": 1, "hsi": 1}, "rngsel": "clk48"},
    "stm32wba55": {"buses": ("sysclk", "hclk", "pclk1", "pclk2", "pclk7"), "flags": {"hse": 1, "lse": 0, "hsi": 1}, "rngsel": "hsi"},
}


class FakeSyncGpio(FakeGroup):
    prefix = "sgpio"

    def __init__(self, fw: FakeFirmware) -> None:
        super().__init__(fw)
        self.boot()

    def boot(self) -> None:
        self.outputs: dict[str, dict[str, Any]] = {}
        self.peripherals: dict[str, int] = {}
        self.multi: tuple[str, ...] = ()

    def cmd_out(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2, ("od", "speed"))
        value = _number(args[1], 0, 1)
        od = _flag(options, "od")
        speed = _choice(options, "speed", SPEEDS, "low")
        pin = self.fw.pin(args[0])
        assert pin is not None
        output = self.outputs.get(pin)
        if output is None:
            owner = (self.prefix, f"out:{pin}")
            self.fw.check_pins(owner, [pin])
            if len(self.outputs) >= _OUTPUT_SLOTS:
                _fail("busy")
            self.fw.claim(owner, [pin])
            output = {"od": od, "speed": speed}
        else:
            output["od"] = od if "od" in options else output["od"]
            output["speed"] = speed if "speed" in options else output["speed"]
        output["latch"] = value
        self.outputs[pin] = output
        return "OK"

    def cmd_latch(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        pin = self.fw.pin(args[0])
        assert pin is not None
        output = self.outputs.get(pin)
        if output is None:
            _fail("notopen")
        assert output is not None
        return f"OK value={output['latch']}"

    def _timer(self, options: dict[str, str]) -> int | None:
        return _number(options["timer"], 1, _TIMER_TABLE_SIZE) if "timer" in options else None

    def cmd_af(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("timer", "ch", "af"))
        timer = self._timer(options)
        channel = _number(options["ch"], 1, _CHANNELS) if "ch" in options else 1
        af = _number(options["af"], 0, _ALTERNATE_FUNCTION_MAX) if "af" in options else 0
        if (timer is not None) == ("af" in options) or ("ch" in options and timer is None):
            _fail("usage")
        if timer is not None and not self.fw.timer_exists(timer):
            _fail("range")
        pin = self.fw.pin(args[0])
        assert pin is not None
        if timer is not None:
            if not self.fw.supports(f"timerChannel{channel}", timer, pin):
                _fail("pin")
            af = _TIMER_AF[timer]
        owner = (self.prefix, f"af:{pin}")
        self.fw.check_pins(owner, [pin])
        if pin in self.peripherals or len(self.peripherals) >= _PERIPHERAL_SLOTS:
            _fail("busy")
        self.fw.claim(owner, [pin])
        self.peripherals[pin] = af
        return f"OK af={af}"

    def cmd_multi(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("timer", "ch"))
        timer = self._timer(options)
        channel = _number(options["ch"], 1, _CHANNELS) if "ch" in options else 1
        if timer is None:
            _fail("usage")
        assert timer is not None
        if not self.fw.timer_exists(timer):
            _fail("range")
        tokens = _tokens(args[0])
        if not 1 <= len(tokens) <= _MULTI_PINS:
            _fail("usage")
        pins: list[str] = []
        for token in tokens:
            pin = self.fw.pin(token)
            assert pin is not None
            if pin in pins:
                _fail("usage")
            pins.append(pin)
        if any(not self.fw.supports(f"timerChannel{channel}", timer, pin) for pin in pins):
            _fail("pin")
        if self.multi:
            _fail("busy")
        owner = (self.prefix, "multi")
        self.fw.check_pins(owner, pins)
        self.fw.claim(owner, pins)
        self.multi = tuple(pins)
        return "OK"

    def cmd_release(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        pin = self.fw.pin(args[0])
        assert pin is not None
        if pin in self.outputs:
            del self.outputs[pin]
            self.fw.release((self.prefix, f"out:{pin}"))
        elif pin in self.peripherals:
            del self.peripherals[pin]
            self.fw.release((self.prefix, f"af:{pin}"))
        elif pin in self.multi:
            self.multi = ()
            self.fw.release((self.prefix, "multi"))
        else:
            _fail("notopen")
        return "OK"


class FakeClock(FakeGroup):
    """`clock.mco` and `clock.hsi48` exist on STM32WB55 only; on STM32WBA55 the unsupported names answer first."""

    prefix = "clock"

    def __init__(self, fw: FakeFirmware) -> None:
        super().__init__(fw)
        if fw.uid == type(fw).uid:
            fw.uid = FAKE_UID
        self.boot()

    def boot(self) -> None:
        self.mco = ("off", 1)
        self.hsi48 = 1

    def cmd_info(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 0, 0)
        tree = _CLOCK_TREES[self.fw.family]
        parts = [f"{bus}={self.fw.kernel_clock}" for bus in tree["buses"]]
        flags = dict(tree["flags"])
        if self.fw.family == "stm32wb55":
            flags["hsi48"] = self.hsi48
        flags["pll"] = 1
        parts += [f"{flag}={value}" for flag, value in flags.items()]
        parts.append(f"rngsel={tree['rngsel']}")
        if self.fw.family == "stm32wb55":
            parts.append("clk48=hsi48")
        return "OK " + " ".join(parts)

    def cmd_mco(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("div",))
        source = _choice({"source": args[0]}, "source", MCO_SOURCES, "off")
        divider = _choice(options, "div", tuple(str(value) for value in MCO_DIVIDERS), "1")
        self.mco = (source, int(divider))
        return "OK"

    def cmd_hsi48(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        self.hsi48 = _number(args[0], 0, 1)
        return "OK"
