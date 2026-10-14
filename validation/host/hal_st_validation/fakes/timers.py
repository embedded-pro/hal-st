"""Fake `tim`, `tpwm`, `lptim` and `lptpwm` groups: the argument checks of validation/firmware/TimerGroup.cpp,
TimerPwmGroup.cpp, LpTimerGroup.cpp and LpTimerPwmGroup.cpp in their order, the timers and LPTIMs they share with
`pwm`, `qei` and `adc` (`TimerAllocation`, `ResourceAllocation` lpTimer), and counters that follow the fake clock."""

from __future__ import annotations

from typing import Any, ClassVar

from .. import expect
from ..groups.timers import (
    LPTIM_PERIOD_MAX,
    LPTIM_PRESCALERS,
    LPTIM_REPETITION_MAX,
    TIMER_PRESCALER_MAX,
    UPDATE_INTERRUPT_MAX_HZ,
    timer_period_max,
)
from .base import FakeGroup, _choice, _fail, _number, _shape, _tokens

_IRQS = ("none", "immediate", "dispatched")
_MODES = ("up", "down")
_LPTIM_INSTANCES = 3


class _Group(FakeGroup):
    """One instance at a time (`HilSingleInstanceGroup`): `<prefix>.open` claims, the other commands find it."""

    instances: ClassVar[int] = 1
    # Whether `instances` is the board's timer count (`Instances()` of the timer factories).
    board_timers: ClassVar[bool] = False

    def _index(self, text: str) -> int:
        instances = self.fw.spec.timer_instances if self.board_timers else self.instances
        return _number(text, 0, instances - 1)

    def _find(self, args: list[str], options: dict[str, str], keys: tuple[str, ...] = (), positional: int = 1) -> dict[str, Any]:
        _shape(args, options, positional, positional, keys)
        return self.fw.find_instance(self.prefix, str(self._index(args[0])))

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key = str(self._index(args[0]))
        self.fw.find_instance(self.prefix, key)
        return self.fw.close_instance(self.prefix, key)


class _Counter(_Group):
    """`TimerCounterGroup`: start/stop/count over a counter that runs at `rate` ticks per second while started and
    wraps after `period + 1` ticks; with an update interrupt every wrap counts in `irqs`."""

    def _counter_state(self, rate: float, period: int, irq: str, down: bool = False) -> dict[str, Any]:
        return {"rate": rate, "period": period, "irq": irq, "down": down, "ticks": 0.0, "since": None}

    def _ticks(self, state: dict[str, Any]) -> float:
        running = 0.0 if state["since"] is None else (self.fw.clock() - state["since"]) * state["rate"]
        return state["ticks"] + running

    def cmd_start(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options)
        if state["since"] is None:
            state["since"] = self.fw.clock()
        return "OK"

    def cmd_stop(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options)
        if state["since"] is not None:
            state["ticks"] = self._ticks(state)
            state["since"] = None
        return "OK"

    def cmd_count(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options)
        ticks = int(self._ticks(state))
        wrap = state["period"] + 1
        counter = (state["period"] - ticks % wrap) if state["down"] else ticks % wrap
        irqs = 0 if state["irq"] == "none" else ticks // wrap
        if state["pin"] is not None:
            self.fw.gpio_levels[state["pin"]] = irqs % 2
        return f"OK cnt={counter} irqs={irqs}"

    def _marker(self, irq: str, pin: str | None) -> str | None:
        """The update marker of `pin=`: none without an update interrupt (`usage`), a bonded pin (`pin`)."""
        if pin is not None and irq == "none":
            _fail("usage")
        if pin is not None and not self.fw.bonded(pin):
            _fail("pin")
        return pin


class FakeTimer(_Counter):
    prefix = "tim"
    board_timers = True

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("prescaler", "period", "irq", "mode", "pin"))
        timer = self._index(args[0])
        if not self.fw.timer_exists(timer):
            _fail("range")
        prescaler = _number(options.get("prescaler", "0"), 0, TIMER_PRESCALER_MAX)
        period = _number(options.get("period", "999"), 1, timer_period_max(timer))
        irq = _choice(options, "irq", _IRQS, "dispatched")
        mode = _choice(options, "mode", _MODES, "up")
        pin = self._marker(irq, self.fw.pin(options.get("pin")))
        if mode == "down" and (irq != "none" or not expect.timer_has_center_mode(timer)):
            _fail("unsupported")
        timclk = self.fw.kernel_clock
        if irq != "none" and timclk // ((prescaler + 1) * (period + 1)) > UPDATE_INTERRUPT_MAX_HZ:
            _fail("range")
        state = self._counter_state(timclk / (prescaler + 1), period, irq, mode == "down")
        state.update(timer=timer, prescaler=prescaler, pin=pin, timclk=timclk)
        self.fw.open_instance(self.prefix, str(timer), state, [pin], timer=timer)
        return f"OK timclk={timclk}"


class FakeLpTimer(_Counter):
    prefix = "lptim"
    instances = _LPTIM_INSTANCES

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("period", "prescaler", "irq", "rep", "pin"))
        index = self._index(args[0])
        if index not in self.fw.spec.lptims:
            _fail("range")
        period = _number(options.get("period", "999"), 1, LPTIM_PERIOD_MAX)
        divider = _number(options.get("prescaler", "1"), 1, LPTIM_PRESCALERS[-1])
        repetition = _number(options.get("rep", "0"), 0, LPTIM_REPETITION_MAX)
        irq = _choice(options, "irq", _IRQS, "dispatched")
        pin = self.fw.pin(options.get("pin"))
        if divider not in LPTIM_PRESCALERS:
            _fail("range")
        pin = self._marker(irq, pin)
        # The STM32WB LPTIM has no repetition counter (LpTimerStm.cpp).
        if "rep" in options and not self.fw.spec.lptim_repetition:
            _fail("unsupported")
        lptimclk = self.fw.kernel_clock
        # ARRM interrupts on every period, whatever `rep`.
        if irq != "none" and lptimclk // (divider * (period + 1)) > UPDATE_INTERRUPT_MAX_HZ:
            _fail("range")
        state = self._counter_state(lptimclk / divider / (repetition + 1), period, irq)
        state.update(index=index, divider=divider, repetition=repetition, pin=pin, lptimclk=lptimclk)
        self.fw.open_instance(self.prefix, str(index), state, [pin], resources=[("lpTimer", index)])
        return f"OK lptimclk={lptimclk}"


class _ChannelPwm(_Group):
    """`PwmChannelGroup`: N channels in `pins` order (`-` unused), duty/pulse per channel, start/stop one or all."""

    clock_key: ClassVar[str] = ""
    functions: ClassVar[tuple[str, ...]] = ()

    def _channel_pins(self, options: dict[str, str]) -> list[str | None]:
        """`ParseChannelPins`: 1..N entries, `-` for an unused channel, at least one pin."""
        if "pins" not in options:
            _fail("usage")
        tokens = _tokens(options["pins"])
        if not 1 <= len(tokens) <= len(self.functions):
            _fail("usage")
        pins = [None if token == "-" else self.fw.pin(token) for token in tokens]
        if all(pin is None for pin in pins):
            _fail("usage")
        return pins

    def _open(self, number: int, pins: list[str | None], counter_max: int, period: int, clock: int, **extra: Any) -> str:
        """`Evaluate` after the numbers and `ParseChannelPins`: channel functions (`pin`), channels (`unsupported`)."""
        for position, pin in enumerate(pins):
            if pin is not None and not self.fw.supports(self.functions[position], number, pin):
                _fail("pin")
        if not all(self._has_channel(number, position + 1) for position in range(len(pins))):
            _fail("unsupported")
        state = {"pins": pins, "period": period, "counter_max": counter_max, "running": [False] * len(pins), **extra}
        state["ccr"] = [0] * len(pins)
        state[self.clock_key] = clock
        self.fw.open_instance(self.prefix, str(number), state, pins, **self._claims(number))
        return f"OK {self.clock_key}={clock}"

    def _has_channel(self, index: int, channel: int) -> bool:
        raise NotImplementedError

    def _claims(self, index: int) -> dict[str, Any]:
        raise NotImplementedError

    def _channel(self, state: dict[str, Any], text: str) -> int:
        return _number(text, 1, len(state["pins"]))

    def cmd_duty(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, positional=3)
        channel = self._channel(state, args[1])
        percent = _number(args[2], 0, 100)
        state["ccr"][channel - 1] = ((state["period"] * percent) & 0xFFFFFFFF) // 100
        return "OK"

    def cmd_pulse(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, positional=4)
        channel = self._channel(state, args[1])
        on = _number(args[2], 0, state["counter_max"])
        state["period"] = _number(args[3], 1, state["counter_max"])
        state["ccr"][channel - 1] = on
        return "OK"

    def _channels(self, args: list[str], options: dict[str, str]) -> tuple[dict[str, Any], list[int]]:
        state = self._find(args, options, ("ch",))
        if "ch" in options:
            return state, [self._channel(state, options["ch"]) - 1]
        return state, list(range(len(state["pins"])))

    def cmd_start(self, args: list[str], options: dict[str, str]) -> str:
        state, positions = self._channels(args, options)
        for position in positions:
            state["running"][position] = True
        return "OK"

    def cmd_stop(self, args: list[str], options: dict[str, str]) -> str:
        state, positions = self._channels(args, options)
        for position in positions:
            state["running"][position] = False
        return "OK"


class FakeTimerPwm(_ChannelPwm):
    prefix = "tpwm"
    board_timers = True
    clock_key = "timclk"
    functions = ("timerChannel1", "timerChannel2", "timerChannel3", "timerChannel4")

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("pins", "prescaler", "period"))
        timer = self._index(args[0])
        if not self.fw.timer_exists(timer):
            _fail("range")
        counter_max = timer_period_max(timer)
        prescaler = _number(options.get("prescaler", "0"), 0, TIMER_PRESCALER_MAX)
        period = _number(options.get("period", "6399"), 1, counter_max)
        pins = self._channel_pins(options)
        return self._open(timer, pins, counter_max, period, self.fw.kernel_clock, timer=timer, prescaler=prescaler)

    def _has_channel(self, index: int, channel: int) -> bool:
        return expect.timer_has_channel(index, channel)

    def _claims(self, index: int) -> dict[str, Any]:
        return {"timer": index}


class FakeLpTimerPwm(_ChannelPwm):
    prefix = "lptpwm"
    instances = _LPTIM_INSTANCES
    clock_key = "lptimclk"
    functions = ("lpTimerChannel1", "lpTimerChannel2")

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("pins", "prescaler", "period"))
        index = self._index(args[0])
        if index not in self.fw.spec.lptims:
            _fail("range")
        divider = _number(options.get("prescaler", "1"), 1, LPTIM_PRESCALERS[-1])
        period = _number(options.get("period", "6399"), 1, LPTIM_PERIOD_MAX)
        if divider not in LPTIM_PRESCALERS:
            _fail("range")
        pins = self._channel_pins(options)
        return self._open(index, pins, LPTIM_PERIOD_MAX, period, self.fw.kernel_clock, index=index, divider=divider)

    def _has_channel(self, index: int, channel: int) -> bool:
        return 1 <= channel <= 2

    def _claims(self, index: int) -> dict[str, Any]:
        return {"resources": [("lpTimer", index)]}
