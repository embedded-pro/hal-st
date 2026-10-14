"""Fake `hrtim` group: the argument checks of validation/firmware/HrtimGroup.cpp in their order, one timing unit open
at a time on the pins of the generated pinout table, the timing and compares of `groups.hrtim`, `hrtim.duty` starting
the unit as `pwm.duty` does, no fault (the fake drives no fault input), and `hrtim.adc` answering the time of the
conversions at the PWM frequency while the unit runs."""

from __future__ import annotations

from typing import Any

from ..groups.hrtim import (
    ADC1_TRIGGERS,
    ADC_TRIGGERS,
    DUTY_FULL_SCALE,
    FAULT_INPUTS,
    UNITS,
    adc_trigger_available,
    compare_ticks,
    dead_time,
    duty_cycle,
    high_resolution_clock,
    timing_for,
)
from .base import UINT32_MAX, FakeGroup, _choice, _fail, _number, _shape

__all__ = ["FakeHrtim"]

_OPEN_KEYS = ("out", "mode", "freq", "dead", "deadfall", "flt", "fltpol", "adc")
_MAXIMUM_DEAD_TIME_NS = 10_000
_SAMPLES = (2, 1000)
_LETTERS = "ABCDEF"


class FakeHrtim(FakeGroup):
    prefix = "hrtim"

    def _unit(self, text: str) -> int:
        return _number(text, 1, UNITS)

    def _find(self, args: list[str]) -> dict[str, Any]:
        return self.fw.find_instance(self.prefix, str(self._unit(args[0])))

    def _timing(self, state: dict[str, Any]) -> str:
        return f"OK clock={self.fw.sysclk} ratio={state['ratio']} period={state['period']} dead={state['dead']} dtprsc={state['dtprsc']}"

    @staticmethod
    def _channels(state: dict[str, Any]) -> int:
        return 2 if state["mode"] == "inphase" and state["out"] == "both" else 1

    def _compare(self, state: dict[str, Any], output: int) -> int:
        duty = state["duties"][output - 1] if state["mode"] == "inphase" else state["duties"][0]
        return compare_ticks(state["period"], state["ratio"], duty)

    def _fault_input(self, pin: str) -> int | None:
        for fault in range(1, FAULT_INPUTS + 1):
            if self.fw.supports(f"hrtimFlt{fault}", 1, pin):
                return fault
        return None

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _OPEN_KEYS)
        unit = self._unit(args[0])
        out = _choice(options, "out", ("both", "1", "2"), "both")
        mode = _choice(options, "mode", ("inphase", "comp", "shift"), "inphase")
        frequency = _number(options.get("freq", "10000"), 1, UINT32_MAX)
        rising = _number(options.get("dead", "0"), 0, _MAXIMUM_DEAD_TIME_NS)
        falling = _number(options.get("deadfall", "0"), 0, _MAXIMUM_DEAD_TIME_NS)
        fault_pin = self.fw.pin(options.get("flt"))
        _choice(options, "fltpol", ("low", "high"), "low")
        trigger = _number(options["adc"], 1, ADC_TRIGGERS) if "adc" in options else 0
        dead = "dead" in options or "deadfall" in options
        if dead and mode != "comp":
            _fail("usage")
        if "fltpol" in options and fault_pin is None:
            _fail("usage")
        fault = None
        if fault_pin is not None:
            fault = self._fault_input(fault_pin)
            if fault is None:
                _fail("pin")
        timing = timing_for(self.fw.sysclk, frequency)
        if timing is None:
            _fail("range")
        assert timing is not None
        if trigger and not adc_trigger_available(unit, trigger):
            _fail("unsupported")
        if any(group == self.prefix for group, _ in self.fw.opened):
            _fail("busy")

        functions = []
        if out != "2":
            functions.append(f"hrtimCh{_LETTERS[unit - 1]}1")
        if out != "1":
            functions.append(f"hrtimCh{_LETTERS[unit - 1]}2")
        pins = [self.fw.first_function_pin(function, 1) for function in functions]
        if None in pins:
            _fail("unsupported")
        if fault_pin is not None:
            pins.append(fault_pin)

        dtprsc, ticks, _ = dead_time(self.fw.sysclk, rising, falling if "deadfall" in options else rising) if mode == "comp" else (0, 0, 0)
        ratio, period = timing
        state = {
            "ratio": ratio,
            "period": period,
            "out": out,
            "mode": mode,
            "duties": [0, 0],
            "dead": ticks,
            "dtprsc": dtprsc,
            "fault": fault,
            "trigger": trigger,
            "running": False,
        }
        self.fw.open_instance(self.prefix, str(unit), state, pins)
        return self._timing(state)

    def cmd_duty(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 3)
        state = self._find(args)
        if len(args) - 1 not in (1, self._channels(state)):
            _fail("usage")
        duties = [duty_cycle(text) for text in args[1:]]
        if None in duties:
            _fail("usage")
        state["duties"] = duties * 2 if len(duties) == 1 else duties
        state["running"] = True
        compares = [self._compare(state, output) for output in range(1, self._channels(state) + 1)]
        return "OK compare=" + ",".join(str(compare) for compare in compares)

    def cmd_phase(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        state = self._find(args)
        phase = duty_cycle(args[1])
        if phase is None or state["mode"] != "shift":
            _fail("usage")
        if phase >= DUTY_FULL_SCALE:
            _fail("range")
        return "OK"

    def cmd_freq(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        state = self._find(args)
        timing = timing_for(self.fw.sysclk, _number(args[1], 1, UINT32_MAX))
        if timing is None:
            _fail("range")
        assert timing is not None
        state["ratio"], state["period"] = timing
        return self._timing(state)

    def cmd_stop(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        self._find(args)["running"] = False
        return "OK"

    def cmd_fault(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        if self._find(args)["fault"] is None:
            _fail("usage")
        return "OK active=0"

    def cmd_adc(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        state = self._find(args)
        samples = _number(args[1], *_SAMPLES)
        if not state["trigger"]:
            _fail("usage")
        if state["trigger"] not in ADC1_TRIGGERS:
            _fail("unsupported")
        self.fw.check_resources((self.prefix, "adc"), [("adc", 1)])
        if not state["running"]:
            _fail("timeout")
        frequency = high_resolution_clock(self.fw.sysclk, state["ratio"]) / state["period"]
        return f"OK samples={samples} us={round((samples - 1) * 1e6 / frequency)}"

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key = str(self._unit(args[0]))
        self.fw.find_instance(self.prefix, key)
        return self.fw.close_instance(self.prefix, key)
