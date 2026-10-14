"""`opamp` group of validation/PROTOCOL.md: one operational amplifier of the STM32G474 (`hal::OpampStm`) at a time,
as a follower, a PGA or standalone, fed by a pin or the internal DAC, with its output on the VOUT pin or internal;
`opamp.read` converts the output with ADC1. `opamp_volts` is the output model the fake and the bench tests share."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from .base import Group, Pin

__all__ = ["GROUPS", "Opamp", "OpampTrim", "opamp_volts"]

Mode = Literal["follower", "pga", "standalone"]
# The output swing of the STM32G474 OPAMP, rail to rail within about 30 mV (datasheet VOHSAT/VOLSAT, typical).
RAIL_MARGIN_V = 0.03


def opamp_volts(vin: float, mode: Mode = "follower", gain: int = 2, vdda: float = 3.3) -> float:
    """The output of a follower or a non-inverting PGA for `vin`, clamped to the output swing."""
    out = vin * gain if mode == "pga" else vin
    return min(max(out, RAIL_MARGIN_V), vdda - RAIL_MARGIN_V)


@dataclass(frozen=True)
class OpampTrim:
    p: int
    n: int


class Opamp(Group):
    prefix = "opamp"

    def open(
        self,
        index: int,
        *,
        mode: Mode | None = None,
        inp: Pin | Literal["dac"] | None = None,
        inm: Pin | None = None,
        pga: Literal["none", "filter", "bias"] | None = None,
        gain: int | None = None,
        out: Literal["internal", "pin"] | None = None,
        speed: Literal["normal", "high"] | None = None,
        cal: bool | None = None,
        dac: int | None = None,
    ) -> OpampTrim:
        response = self._cmd(
            "open",
            index,
            mode=mode,
            inp=inp if inp in (None, "dac") else self._pin(inp),
            inm=self._pin(inm),
            pga=pga,
            gain=gain,
            out=out,
            speed=speed,
            cal=cal,
            dac=dac,
        )
        self._fw.track(("opamp", index), "opamp.close", index)
        return OpampTrim(p=response.as_int("trimp"), n=response.as_int("trimn"))

    def dac(self, index: int, code: int) -> int:
        return self._cmd("dac", index, code).as_int("value")

    def read(self, index: int, sampling: str | None = None) -> int:
        return self._cmd("read", index, sampling=sampling).as_int("code")

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack(("opamp", index))


GROUPS: dict[str, type[Group]] = {"opamp": Opamp}
