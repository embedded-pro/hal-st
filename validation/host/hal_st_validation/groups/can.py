"""`can` group of validation/PROTOCOL.md: EMIL's `HilCanCommands` over `hal::FdCanStm` (FDCAN 1-3 of the STM32G474,
classic frames). `open` registers its undo with `close_all`; received frames and errors arrive as `EVT can` lines.

The model of the driver and of validation/firmware/CanFactory.cpp: the nominal bit timing `CalculateNominalBitTiming`
picks for a bit rate (`nominal_bit_timing`), the bit rates the factory accepts (`bitrate_achievable`), the bit rate of
an explicit timing (`timing_bitrate`) and the acceptance filter (`filter_accepts`).
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from ad3_waveforms_bench.protocol import Event
from ad3_waveforms_bench.terminal import PendingCommand

from .base import Group, Pin

__all__ = [
    "BITRATE_MAX",
    "DATA_MAX",
    "ERROR_NAMES",
    "EXTENDED_ID_MAX",
    "GROUPS",
    "QUANTA",
    "STANDARD_ID_MAX",
    "TIMING_MAX",
    "BitTiming",
    "Can",
    "CanFilter",
    "CanFrame",
    "bitrate_achievable",
    "filter_accepts",
    "nominal_bit_timing",
    "timing_bitrate",
]

STANDARD_ID_MAX = 0x7FF
EXTENDED_ID_MAX = 0x1FFFFFFF
DATA_MAX = 8
BITRATE_MAX = 1_000_000
# Time quanta per bit `CalculateNominalBitTiming` tries, most first.
QUANTA = (8, 25)
# `timing=<tseg1>,<tseg2>,<sjw>,<brp>`: the upper limits of the FDCAN nominal bit timing fields.
TIMING_MAX = (256, 128, 128, 512)
# `EVT can error=` names in the order of `hal::FdCanStm::Error`.
ERROR_NAMES = (
    "stuffError",
    "formError",
    "ackError",
    "bit1Error",
    "bit0Error",
    "crcError",
    "errorWarning",
    "errorPassive",
    "busOff",
    "messageLost",
)

# `filter=<id>,<mask>,<ext>,<match>`
CanFilter = tuple[int, int, bool, bool]


@dataclass(frozen=True)
class CanFrame:
    index: int
    id: int
    ext: bool
    data: bytes


@dataclass(frozen=True)
class BitTiming:
    prescaler: int
    tseg1: int
    tseg2: int
    sjw: int

    @property
    def quanta(self) -> int:
        return 1 + self.tseg1 + self.tseg2


def nominal_bit_timing(kernel_clock: int, bitrate: int) -> BitTiming:
    """`FdCanStm::CalculateNominalBitTiming`: the most quanta (25 down to 8) whose rounded prescaler hits `bitrate`
    exactly, else the nearest; phase segment 2 and the jump width are an eighth of the quanta (sample point near
    87.5 %)."""
    if bitrate <= 0:
        raise ValueError("bitrate must be positive")
    best_quanta, best_prescaler, best_error = QUANTA[1], 1, None
    for quanta in range(QUANTA[1], QUANTA[0] - 1, -1):
        quantum_rate = bitrate * quanta
        prescaler = min(max((kernel_clock + quantum_rate // 2) // quantum_rate, 1), TIMING_MAX[3])
        error = abs(kernel_clock // (prescaler * quanta) - bitrate)
        if best_error is None or error < best_error:
            best_quanta, best_prescaler, best_error = quanta, prescaler, error
        if error == 0:
            break
    tseg2 = max(1, best_quanta // 8)
    return BitTiming(best_prescaler, best_quanta - 1 - tseg2, tseg2, tseg2)


def bitrate_achievable(kernel_clock: int, bitrate: int) -> bool:
    """`CanFactoryStm` accepts a bit rate only when the timing of `nominal_bit_timing` reaches it exactly."""
    timing = nominal_bit_timing(kernel_clock, bitrate)
    return bitrate * timing.prescaler * timing.quanta == kernel_clock


def timing_bitrate(kernel_clock: float, tseg1: int, tseg2: int, sjw: int, brp: int) -> float:
    """Bit rate of an explicit `timing=<tseg1>,<tseg2>,<sjw>,<brp>`; `sjw` does not change it."""
    return kernel_clock / (brp * (1 + tseg1 + tseg2))


def filter_accepts(can_filter: CanFilter | None, ident: int, ext: bool) -> bool:
    """`FdCanStm`'s acceptance: every data frame without a filter; else a frame of the filter's id type whose id
    matches the filter id under the mask, and a frame of the other id type only when `match` is 0."""
    if can_filter is None:
        return True
    filter_id, mask, filter_ext, match = can_filter
    if ext != filter_ext:
        return not match
    return (ident & mask) == (filter_id & mask)


class Can(Group):
    prefix = "can"

    def open(
        self,
        index: int,
        rx: Pin | None = None,
        tx: Pin | None = None,
        bitrate: int | None = None,
        timing: tuple[int, int, int, int] | None = None,
        filter: tuple[int, int, bool] | CanFilter | None = None,
        loopback: bool | None = None,
        recover: bool | None = None,
    ) -> None:
        """`timing` is `(tseg1, tseg2, sjw, brp)`; `filter` is `(id, mask, ext[, match])`; `loopback=True` is the
        external loopback mode (the frames still leave on the TX pin)."""
        filter_value = None
        if filter is not None:
            filter_value = [f"0x{filter[0]:x}", f"0x{filter[1]:x}", int(bool(filter[2]))]
            if len(filter) > 3:
                filter_value.append(int(bool(filter[3])))
        self._cmd(
            "open",
            index,
            rx=self._pin(rx),
            tx=self._pin(tx),
            bitrate=bitrate,
            timing=None if timing is None else list(timing),
            filter=filter_value,
            loopback=loopback,
            recover=recover,
        )
        self._fw.track(("can", index), "can.close", index)

    def send(self, index: int, id: int, data: bytes, ext: bool | None = None, cmd_timeout: float | None = None) -> None:
        self._cmd("send", index, f"0x{id:x}", bytes(data), ext=ext, cmd_timeout=cmd_timeout)

    def begin_send(self, index: int, id: int, data: bytes, ext: bool | None = None, cmd_timeout: float | None = None) -> PendingCommand:
        """`can.send` without waiting for its final line (`settle()` finishes it)."""
        return self.begin("send", index, f"0x{id:x}", bytes(data), ext=ext, cmd_timeout=cmd_timeout)

    def wait_frame(self, index: int, predicate: Callable[[CanFrame], bool] | None = None, timeout: float = 1.0) -> CanFrame:
        def accept(event: Event) -> bool:
            if "error" in event or "id" not in event or event.as_int("index") != index:
                return False
            return predicate is None or predicate(self.frame(event))

        return self.frame(self._fw.terminal.wait_event("can", accept, timeout))

    def frames(self, index: int) -> list[CanFrame]:
        """The frames received so far (removed from the event queue with the errors)."""
        events = self._fw.terminal.drain_events("can")
        return [self.frame(event) for event in events if "id" in event and event.as_int("index") == index]

    def errors(self, index: int) -> list[str]:
        """The error names reported so far (removed from the event queue with the frames)."""
        events = self._fw.terminal.drain_events("can")
        return [event["error"] for event in events if "error" in event and event.as_int("index") == index]

    @staticmethod
    def frame(event: Event) -> CanFrame:
        return CanFrame(index=event.as_int("index"), id=event.as_int("id"), ext=event.as_bool("ext"), data=event.as_bytes("data"))

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack(("can", index))


GROUPS: dict[str, type[Group]] = {"can": Can}
