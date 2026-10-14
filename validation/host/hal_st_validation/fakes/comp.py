"""Fake `comp` group: EMIL's `HilComparatorCommands` (one open comparator, argument order of HilSingleInstanceGroup)
over the checks of validation/firmware/ComparatorFactory.cpp, and `comp.dac`. The output compares the fake's
`adc_codes` level of the INP pin (mid-scale by default) with the inverting input; the fake models no edges, so
`comp.count` stays 0."""

from __future__ import annotations

from typing import Any

from ..groups.comp import DAC_FULL_SCALE, INPUT_MINUS_FRACTIONS, threshold_volts
from .base import FakeGroup, _choice, _fail, _flag, _number, _shape

__all__ = ["FakeComparator"]

_INSTANCES = 8
_OPEN_KEYS = ("inp", "inm", "dac", "hyst", "pol", "out", "blank", "sync", "irq")
_EDGES = ("rising", "falling", "both", "off")
_VREF = 3.3
_ADC_FULL_SCALE = 4095
# validation/firmware/boards/stm32g474/BoardProfile.hpp comparatorPins: INP IO1/IO2, INM IO1/IO2, DAC3/DAC4 channel.
_PINS: dict[int, tuple[tuple[str | None, str | None], tuple[str | None, str | None], tuple[int, int]]] = {
    1: (("PA1", "PB1"), ("PA4", "PA0"), (3, 1)),
    2: (("PA7", "PA3"), ("PA5", "PA2"), (3, 2)),
    3: (("PA0", "PC1"), ("PF1", "PC0"), (3, 1)),
    4: (("PB0", None), (None, "PB2"), (3, 2)),
    5: (("PB13", None), ("PB10", None), (4, 1)),
    6: (("PB11", None), (None, "PB15"), (4, 2)),
    7: (("PB14", None), (None, "PB12"), (4, 1)),
}
# Blanking sources per comparator (0: every comparator), as in ComparatorFactory.cpp.
_BLANKING: dict[str, tuple[int, ...]] = {
    "tim1oc5": (1, 2, 3, 4, 5, 6, 7),
    "tim2oc3": (1, 2, 5),
    "tim2oc4": (3, 6),
    "tim3oc3": (1, 2, 3, 5, 7),
    "tim3oc4": (4,),
    "tim8oc5": (1, 2, 3, 4, 5, 6, 7),
    "tim15oc1": (0,),
    "tim15oc2": (6, 7),
    "tim20oc5": (0,),
    "tim4oc3": (0,),
    "none": (0,),
}


class FakeComparator(FakeGroup):
    prefix = "comp"

    def _index(self, text: str) -> int:
        return _number(text, 0, _INSTANCES - 1)

    def _find(self, text: str) -> dict[str, Any]:
        return self.fw.find_instance(self.prefix, str(self._index(text)))

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _OPEN_KEYS)
        index = self._index(args[0])
        if index not in _PINS:
            _fail("range")
        plus, minus, dac = _PINS[index]
        inp = self.fw.pin(options["inp"]) if "inp" in options else plus[0]
        inm_text = options.get("inm", "vref/2")
        inm_pin = None
        if inm_text not in INPUT_MINUS_FRACTIONS and inm_text != "dac":
            inm_pin = self.fw.pin(inm_text)
        dac_code = _number(options.get("dac", "2048"), 0, DAC_FULL_SCALE)
        hysteresis = _number(options.get("hyst", "0"), 0, 70)
        inverted = _choice(options, "pol", ("normal", "inverted"), "normal") == "inverted"
        out = self.fw.pin(options["out"]) if "out" in options else None
        blank = options.get("blank", "none")
        if blank not in _BLANKING:
            _fail("usage")
        synchronous = _flag(options, "sync")
        _choice(options, "irq", ("dispatched", "immediate"), "dispatched")
        if "dac" in options and inm_text != "dac":
            _fail("usage")
        if hysteresis % 10:
            _fail("range")
        if inp not in plus:
            _fail("pin")
        if inm_pin is not None and inm_pin not in minus:
            _fail("pin")
        self.fw.check_function("compOutput", index, out)
        if index not in _BLANKING[blank] and 0 not in _BLANKING[blank]:
            _fail("unsupported")
        if any(opened_group == self.prefix for opened_group, _ in self.fw.opened):
            _fail("busy")

        state: dict[str, Any] = {
            "inp": inp,
            "inm": inm_text if inm_pin is None else inm_pin,
            "dac": dac_code,
            "inverted": inverted,
            "sync": synchronous,
            "count": 0,
            "irq": "off",
        }
        resources = [("dac", (dac[0] - 1) * 2 + dac[1] - 1)] if inm_text == "dac" else []
        self.fw.open_instance(self.prefix, str(index), state, [inp, inm_pin, out], resources=resources)
        return "OK"

    def _volts(self, pin: str) -> float:
        return _VREF * self.fw.adc_codes.get(pin, 2048) / _ADC_FULL_SCALE

    def cmd_read(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        state = self._find(args[0])
        inm = state["inm"]
        threshold = self._volts(inm) if inm.startswith("P") else threshold_volts(inm, _VREF, state["dac"])
        output = int(self._volts(state["inp"]) > threshold) ^ int(state["inverted"])
        return f"OK out={output}"

    def cmd_irq(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        state = self._find(args[0])
        if args[1] not in _EDGES:
            _fail("usage")
        if state["sync"]:
            _fail("unsupported")
        state["irq"] = args[1]
        return "OK"

    def cmd_count(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("clear",))
        state = self._find(args[0])
        count = state["count"]
        if _flag(options, "clear"):
            state["count"] = 0
        return f"OK count={count}"

    def cmd_dac(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        index = self._index(args[0])
        code = _number(args[1], 0, DAC_FULL_SCALE)
        state = self.fw.opened.get((self.prefix, str(index)))
        if state is None or state["inm"] != "dac":
            _fail("notopen")
        assert state is not None
        state["dac"] = code
        return f"OK value={code}"

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key = str(self._index(args[0]))
        self.fw.find_instance(self.prefix, key)
        return self.fw.close_instance(self.prefix, key)
