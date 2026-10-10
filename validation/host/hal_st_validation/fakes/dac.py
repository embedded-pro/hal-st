"""Fake of the `dac` group (validation/firmware/DacGroup.cpp): `open`, `set` and `close` on the DAC outputs of the
board (`_Family.dac_outputs`). The fake models no output voltage."""

from __future__ import annotations

from .base import FakeGroup, _fail, _number, _shape

__all__ = ["FakeDac"]

# `dac.set` accepts 16 bits and writes the 12 bits of the converter.
_VALUE_MAX = 0xFFFF
_RESOLUTION_MASK = 0xFFF


class FakeDac(FakeGroup):
    prefix = "dac"

    def boot(self) -> None:
        self.outputs: dict[str, int] = {}

    def _output(self, text: str) -> str:
        pin = self.fw.pin(text)
        assert pin is not None
        if pin not in self.fw.spec.dac_outputs:
            _fail("pin")
        return pin

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        pin = self._output(args[0])
        if pin in self.outputs:
            _fail("busy")
        owner = (self.prefix, pin)
        self.fw.check_pins(owner, [pin])
        self.fw.claim(owner, [pin])
        self.outputs[pin] = 0
        return f"OK dac={self.fw.spec.dac_outputs[pin]}"

    def cmd_set(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2)
        value = _number(args[1], 0, _VALUE_MAX)
        pin = self._output(args[0])
        if pin not in self.outputs:
            _fail("notopen")
        self.outputs[pin] = min(value, _RESOLUTION_MASK)
        return f"OK value={self.outputs[pin]}"

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        pin = self._output(args[0])
        if pin not in self.outputs:
            _fail("notopen")
        del self.outputs[pin]
        self.fw.release((self.prefix, pin))
        return "OK"
