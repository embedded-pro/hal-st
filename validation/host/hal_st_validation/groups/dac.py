"""`dac` group of validation/PROTOCOL.md: `hal::DigitalToAnalogPinImplStm` on the DAC outputs of the board (STM32G474: DAC1
OUT1 on PA4 and DAC2 OUT1 on PA6). A pin belongs to the group from `open` to `close`; `open` registers its undo with
`close_all`.
"""

from __future__ import annotations

from .base import Group, Pin

__all__ = ["DAC_FULL_SCALE", "DAC_RESOLUTION_BITS", "GROUPS", "Dac", "dac_code", "dac_volts"]

DAC_RESOLUTION_BITS = 12
# DAC_DOR = 4095 gives the reference voltage (RM0440 DAC output voltage = VREF+ x DOR / 4095).
DAC_FULL_SCALE = (1 << DAC_RESOLUTION_BITS) - 1


def dac_volts(code: int, vref: float = 3.3) -> float:
    """Output voltage of the unloaded, buffered DAC for `code` (clamped to the resolution like `dac.set`)."""
    return vref * min(code, DAC_FULL_SCALE) / DAC_FULL_SCALE


def dac_code(volts: float, vref: float = 3.3) -> int:
    """Code that gives `volts` (nearest)."""
    return max(0, min(DAC_FULL_SCALE, round(volts / vref * DAC_FULL_SCALE)))


class Dac(Group):
    prefix = "dac"

    def open(self, pin: Pin) -> int:
        """Claim the pin and start its DAC channel (buffer on, the pin only); returns the DAC number."""
        pin = self._fw.pin(pin)
        dac = self._cmd("open", pin).as_int("dac")
        self._fw.track(("dac", pin), "dac.close", pin)
        return dac

    def set(self, pin: Pin, value: int) -> int:
        """Write `value`; the firmware clamps it to 12 bits and returns the value it wrote."""
        return self._cmd("set", self._fw.pin(pin), value).as_int("value")

    def close(self, pin: Pin) -> None:
        pin = self._fw.pin(pin)
        self._cmd("close", pin)
        self._fw.untrack(("dac", pin))


GROUPS: dict[str, type[Group]] = {"dac": Dac}
