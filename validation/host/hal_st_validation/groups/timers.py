"""Timer groups of validation/PROTOCOL.md: `tim` (`hal::FreeRunningTimerStm`, `hal::TimerWithInterruptStm`), `tpwm`
(`hal::TimerPwmWithChannels<N>`), `lptim` (`hal::FreeRunningLowPowerTimerStm`, `hal::LowPowerTimerWithInterruptStm`)
and `lptpwm` (`hal::LpTimerPwmWithChannels<N>`, STM32WBA55 only), with the expected waveforms (pure functions).

Sources: `TimerBaseStm` (hal_st/stm32fxxx/TimerStm.cpp: PSC = prescaler, ARR = period), `PwmChannelGpio::SetDuty`
(TimerPwmStm.cpp: CCR = ARR * duty / 100 in 32 bits, PWM mode 1, so the output is high while CNT < CCR),
`LowPowerTimerBaseStm` (LpTimerStm.cpp: prescaler divider, ARR = period, repetition counter on the WBA LPTIM only).
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal

from .. import expect
from .base import Group, Pin

__all__ = [
    "GROUPS",
    "LPTIM_PERIOD_MAX",
    "LPTIM_PRESCALERS",
    "LPTIM_REPETITION_MAX",
    "TIMER_DISPATCHED_MAX_HZ",
    "TIMER_PRESCALER_MAX",
    "LpTimer",
    "LpTimerPwm",
    "Timer",
    "TimerCount",
    "TimerPwm",
    "lptim_update_rate",
    "pwm_duty_fraction",
    "pwm_high_ticks",
    "timer_period_max",
    "timer_update_rate",
]

Irq = Literal["none", "immediate", "dispatched"]
ChannelPin = Pin | None

TIMER_PRESCALER_MAX = 0xFFFF
LPTIM_PRESCALERS = (1, 2, 4, 8, 16, 32, 64, 128)
LPTIM_PERIOD_MAX = 0xFFFF
LPTIM_REPETITION_MAX = 0xFF
# Above this update rate `irq=dispatched` callbacks coalesce in the event loop (one queued callback at a time).
TIMER_DISPATCHED_MAX_HZ = 1000


def timer_period_max(timer: int) -> int:
    """`period` (ARR) of `tim.open`/`tpwm.open`: 16 bits, 32 bits on TIM2."""
    return expect.timer_counter_max(timer)


def timer_update_rate(timclk: int, prescaler: int, period: int) -> float:
    """Update events per second of a TIM: one per `period + 1` counter ticks of `timclk / (prescaler + 1)`."""
    return timclk / (prescaler + 1) / (period + 1)


def lptim_update_rate(lptimclk: int, divider: int, period: int, repetition: int = 0) -> float:
    """Update events per second of an LPTIM: one per `period + 1` ticks of `lptimclk / divider`, and on the WBA
    LPTIM only every `repetition + 1` periods."""
    return lptimclk / divider / (period + 1) / (repetition + 1)


def pwm_high_ticks(period: int, percent: int) -> int:
    """`SetDuty`: CCR = ARR * percent / 100 computed in 32 bits (it wraps above ARR 42949672 on TIM2)."""
    return ((period * percent) & 0xFFFFFFFF) // 100


def pwm_duty_fraction(period: int, percent: int) -> float:
    """High fraction of a channel: high while the counter is below CCR, out of `period + 1` ticks; 100 % keeps one
    low tick (CCR = ARR)."""
    return min(pwm_high_ticks(period, percent), period + 1) / (period + 1)


@dataclass(frozen=True)
class TimerCount:
    cnt: int
    irqs: int


class _Counter(Group):
    def start(self, index: int) -> None:
        self._cmd("start", index)

    def stop(self, index: int) -> None:
        self._cmd("stop", index)

    def count(self, index: int) -> TimerCount:
        response = self._cmd("count", index)
        return TimerCount(cnt=response.as_int("cnt"), irqs=response.as_int("irqs"))

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack((self.prefix, index))

    def _track(self, index: int) -> None:
        self._fw.track((self.prefix, index), f"{self.prefix}.close", index)


class Timer(_Counter):
    prefix = "tim"

    def open(
        self,
        timer: int,
        prescaler: int | None = None,
        period: int | None = None,
        irq: Irq | None = None,
        mode: Literal["up", "down"] | None = None,
        pin: Pin | None = None,
    ) -> int:
        """Returns `timclk`, the timer kernel clock. `pin` toggles on every update interrupt (not with `irq="none"`);
        `mode="down"` needs `irq="none"`."""
        response = self._cmd("open", timer, prescaler=prescaler, period=period, irq=irq, mode=mode, pin=self._pin(pin))
        self._track(timer)
        return response.as_int("timclk")


class LpTimer(_Counter):
    prefix = "lptim"

    def open(
        self,
        index: int,
        period: int | None = None,
        prescaler: int | None = None,
        irq: Irq | None = None,
        rep: int | None = None,
        pin: Pin | None = None,
    ) -> int:
        """Returns `lptimclk`, the LPTIM kernel clock. `prescaler` is the divider (1, 2, 4, ..., 128); `rep` needs the
        WBA LPTIM."""
        response = self._cmd("open", index, period=period, prescaler=prescaler, irq=irq, rep=rep, pin=self._pin(pin))
        self._track(index)
        return response.as_int("lptimclk")


class _ChannelPwm(Group):
    clock_key = ""

    def open(self, index: int, pins: Sequence[ChannelPin], prescaler: int | None = None, period: int | None = None) -> int:
        """`pins` in channel order, None for an unused channel (`-`); returns the kernel clock."""
        entries = ["-" if pin is None else self._fw.pin(pin) for pin in pins]
        response = self._cmd("open", index, pins=entries, prescaler=prescaler, period=period)
        self._fw.track((self.prefix, index), f"{self.prefix}.close", index)
        return response.as_int(self.clock_key)

    def duty(self, index: int, channel: int, percent: int) -> None:
        self._cmd("duty", index, channel, percent)

    def pulse(self, index: int, channel: int, on: int, period: int) -> None:
        """`SetPulse`: CCR of `channel` = `on`, ARR = `period` for every channel."""
        self._cmd("pulse", index, channel, on, period)

    def start(self, index: int, ch: int | None = None) -> None:
        self._cmd("start", index, ch=ch)

    def stop(self, index: int, ch: int | None = None) -> None:
        self._cmd("stop", index, ch=ch)

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack((self.prefix, index))


class TimerPwm(_ChannelPwm):
    prefix = "tpwm"
    clock_key = "timclk"


class LpTimerPwm(_ChannelPwm):
    prefix = "lptpwm"
    clock_key = "lptimclk"


GROUPS: dict[str, type[Group]] = {"tim": Timer, "tpwm": TimerPwm, "lptim": LpTimer, "lptpwm": LpTimerPwm}
