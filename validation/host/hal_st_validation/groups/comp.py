"""`comp` group of validation/PROTOCOL.md: EMIL's comparator commands (`comp.open`, `comp.read`, `comp.irq`,
`comp.count`, `comp.close`) over `hal::ComparatorStm` / `hal::SynchronousComparatorStm` on the STM32G474, and the
hal-st `comp.dac`, which sets the internal DAC that feeds the inverting input with `inm=dac`. One comparator is open
at a time. The threshold models (`threshold_volts`) serve the fake and the bench tests."""

from __future__ import annotations

from typing import Literal

from .base import Group, Pin

__all__ = ["GROUPS", "INPUT_MINUS_FRACTIONS", "VREFINT", "Comparator", "threshold_volts"]

Edge = Literal["rising", "falling", "both", "off"]
# VREFINT of the STM32G474 datasheet, typical; the scaler taps 1/4, 1/2 and 3/4 of it.
VREFINT = 1.212
INPUT_MINUS_FRACTIONS: dict[str, float] = {"vref/4": 0.25, "vref/2": 0.5, "vref3/4": 0.75, "vref": 1.0}
DAC_FULL_SCALE = 4095


def threshold_volts(inm: str, vref: float = 3.3, dac_code: int = 2048) -> float:
    """The voltage on the inverting input for an internal `inm` (a VREFINT fraction or the DAC code)."""
    if inm == "dac":
        return vref * dac_code / DAC_FULL_SCALE
    return VREFINT * INPUT_MINUS_FRACTIONS[inm]


class Comparator(Group):
    prefix = "comp"

    def open(
        self,
        index: int,
        *,
        inp: Pin | None = None,
        inm: str | None = None,
        dac: int | None = None,
        hyst: int | None = None,
        pol: Literal["normal", "inverted"] | None = None,
        out: Pin | None = None,
        blank: str | None = None,
        sync: bool | None = None,
        irq: Literal["dispatched", "immediate"] | None = None,
    ) -> None:
        """`inm` is a VREFINT fraction (`vref/4`, `vref/2`, `vref3/4`, `vref`), `dac` or a pin."""
        inm_value = inm if inm is None or inm in INPUT_MINUS_FRACTIONS or inm == "dac" else self._pin(inm)
        self._cmd(
            "open",
            index,
            inp=self._pin(inp),
            inm=inm_value,
            dac=dac,
            hyst=hyst,
            pol=pol,
            out=self._pin(out),
            blank=blank,
            sync=sync,
            irq=irq,
        )
        self._fw.track(("comp", index), "comp.close", index)

    def read(self, index: int) -> int:
        return self._cmd("read", index).as_int("out")

    def irq(self, index: int, edge: Edge) -> None:
        self._cmd("irq", index, edge)

    def count(self, index: int, clear: bool | None = None) -> int:
        return self._cmd("count", index, clear=clear).as_int("count")

    def dac(self, index: int, code: int) -> int:
        return self._cmd("dac", index, code).as_int("value")

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack(("comp", index))


GROUPS: dict[str, type[Group]] = {"comp": Comparator}
