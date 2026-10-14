"""Fake `opamp` group: the argument checks of validation/firmware/OpampGroup.cpp in their order, one amplifier open at
a time, trimming values in range, and `opamp.read` from `opamp_volts` of the input level (the fake's `adc_codes` of
the VINP pin, mid-scale by default, or the DAC code)."""

from __future__ import annotations

from typing import Any

from ..groups.opamp import opamp_volts
from .base import FakeGroup, _choice, _fail, _flag, _number, _shape

__all__ = ["FakeOpamp"]

_OPEN_KEYS = ("mode", "inp", "inm", "pga", "gain", "out", "speed", "cal", "dac")
_VREF = 3.3
_FULL_SCALE = 4095
_SAMPLING = ("2.5", "6.5", "12.5", "24.5", "47.5", "92.5", "247.5", "640.5")
# validation/firmware/boards/stm32g474/BoardProfile.hpp opampPins: VINP0-2, DAC channel, VINM0/1, VOUT, internal ADC1.
_PINS: dict[int, tuple[tuple[str | None, ...], tuple[int, int] | None, tuple[str | None, ...], str, bool]] = {
    1: (("PA1", "PA3", "PA7"), (3, 1), ("PA3", "PC5"), "PA2", True),
    2: (("PA7", "PB14", "PB0"), None, ("PA5", "PC5"), "PA6", False),
    3: (("PB0", "PB13", "PA1"), (3, 2), ("PB2", "PB10"), "PB1", False),
    4: (("PB13", None, "PB11"), (4, 1), ("PB10", "PB8"), "PB12", False),
    5: (("PB14", None, "PC3"), (4, 2), ("PB15", "PA3"), "PA8", False),
    6: (("PB12", None, "PB13"), (3, 1), ("PA1", "PB1"), "PB11", False),
}


class FakeOpamp(FakeGroup):
    prefix = "opamp"

    def _index(self, text: str) -> int:
        return _number(text, 1, len(_PINS))

    def _find(self, text: str) -> dict[str, Any]:
        return self.fw.find_instance(self.prefix, str(self._index(text)))

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _OPEN_KEYS)
        index = self._index(args[0])
        mode = _choice(options, "mode", ("follower", "pga", "standalone"), "follower")
        inp_text = options.get("inp")
        inp = None if inp_text in (None, "dac") else self.fw.pin(inp_text)
        inm = self.fw.pin(options["inm"]) if "inm" in options else None
        pga = _choice(options, "pga", ("none", "filter", "bias"), "none")
        gain = _number(options.get("gain", "2"), 0, 64)
        pin_output = _choice(options, "out", ("internal", "pin"), "internal") == "pin"
        _choice(options, "speed", ("normal", "high"), "normal")
        _flag(options, "cal", True)
        dac_code = _number(options.get("dac", "0"), 0, _FULL_SCALE)
        if ("gain" in options or "pga" in options) and mode != "pga":
            _fail("usage")
        if "dac" in options and inp_text != "dac":
            _fail("usage")
        uses_inverting = mode == "standalone" or (mode == "pga" and pga != "none")
        if uses_inverting != (inm is not None):
            _fail("usage")
        if mode == "pga" and (gain < 2 or gain & (gain - 1)):
            _fail("range")

        plus, dac, minus, output, _ = _PINS[index]
        if inp is not None and inp not in plus:
            _fail("pin")
        if inm is not None and (inm not in minus or (mode == "pga" and inm != minus[0])):
            _fail("pin")
        if inp_text == "dac" and dac is None:
            _fail("unsupported")
        if any(group == self.prefix for group, _ in self.fw.opened):
            _fail("busy")

        if inp_text != "dac" and inp is None:
            inp = plus[0]
        resources = [("dac", (dac[0] - 1) * 2 + dac[1] - 1)] if inp_text == "dac" and dac is not None else []
        state = {"inp": inp, "dac": dac_code, "mode": mode, "gain": gain, "pin_output": pin_output, "output": output}
        owner = (self.prefix, str(index))
        if pin_output:
            self.fw.check_pins(owner, [output], analog=True)
        self.fw.open_instance(self.prefix, str(index), state, [inp, inm], resources=resources)
        if pin_output:
            self.fw.claim(owner, [output], analog=True)
        return "OK trimp=16 trimn=16"

    def cmd_dac(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        state = self._find(args[0])
        code = _number(args[1], 0, _FULL_SCALE)
        if state["inp"] is not None:
            _fail("notopen")
        state["dac"] = code
        return f"OK value={code}"

    def cmd_read(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("sampling",))
        state = self._find(args[0])
        _choice(options, "sampling", _SAMPLING, "640.5")
        index = self._index(args[0])
        internal = _PINS[index][4]
        if (not state["pin_output"] and not internal) or (state["pin_output"] and not self.fw.supports_analog(state["output"])):
            _fail("unsupported")
        self.fw.check_resources((self.prefix, "adc"), [("adc", 1)])
        code = self.fw.adc_codes.get(state["inp"], 2048) if state["inp"] is not None else state["dac"]
        volts = opamp_volts(_VREF * code / _FULL_SCALE, state["mode"], state["gain"], _VREF)
        return f"OK code={round(volts / _VREF * _FULL_SCALE)}"

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key = str(self._index(args[0]))
        self.fw.find_instance(self.prefix, key)
        return self.fw.close_instance(self.prefix, key)
