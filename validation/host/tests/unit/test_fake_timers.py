"""The fake `tim`, `tpwm`, `lptim` and `lptpwm` groups (argument order and reasons of validation/firmware/TimerGroup.cpp,
TimerPwmGroup.cpp, LpTimerGroup.cpp and LpTimerPwmGroup.cpp; PROTOCOL.md D.6-D.9), the timers and LPTIMs they share
with the other groups, the counters over a fake clock, the `groups.timers` wrappers and expectations."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups import timers


class Clock:
    def __init__(self) -> None:
        self.now = 100.0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


def make(family="stm32wb55"):
    clock = Clock()
    fake = FakeFirmware(family=family, clock=clock, sleep=clock.sleep)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    pins = WB55_PINS if family == "stm32wb55" else WBA55_PINS
    return terminal, fake, clock, Firmware(terminal, pins)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("tim.open", "usage"),
        ("tim.open 1 2", "usage"),
        ("tim.open 1 speed=1", "usage"),
        ("tim.open x", "usage"),
        ("tim.open 18", "range"),
        ("tim.open 3", "range"),
        ("tim.open 1 irq=nmi", "usage"),
        ("tim.open 1 mode=sideways", "usage"),
        ("tim.open 1 prescaler=65536", "range"),
        ("tim.open 1 period=0", "range"),
        ("tim.open 1 period=65536", "range"),
        ("tim.open 1 prescaler=65536 irq=nmi", "range"),
        ("tim.open 1 irq=none pin=gpio0", "usage"),
        ("tim.open 1 irq=none pin=PZ1", "pin"),
        ("tim.open 1 pin=PC7", "pin"),
        ("tim.open 1 mode=down", "unsupported"),
        ("tim.open 16 irq=none mode=down", "unsupported"),
        ("tim.open 1 irq=none mode=down", "ok"),
        ("tim.open 2 period=0xFFFFFFFF irq=none", "ok"),
        ("tim.open 17 pin=gpio0 irq=immediate", "ok"),
        ("tim.open 1 pin=terminaltx", "busy"),
    ],
)
def test_timer_open_reasons(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


def test_timer_open_reports_the_kernel_clock_and_tracks():
    terminal, fake, _, fw = make("stm32wba55")
    assert fw.tim.open(3, irq="immediate", pin="gpio0") == 100_000_000
    assert fake.received[-1] == "tim.open 3 irq=immediate pin=PB14"
    assert fw.open_instances == [("tim", 3)]
    assert reason(terminal, "gpio.cfg gpio0 in") == "busy"
    fw.close_all()
    assert fake.received[-1] == "tim.close 3"
    assert reason(terminal, "gpio.cfg gpio0 in") == "ok"


def test_timer_counts_over_the_clock():
    terminal, _, clock, fw = make()
    fw.tim.open(1, prescaler=63, period=999, irq="dispatched")
    assert fw.tim.count(1) == timers.TimerCount(0, 0)
    fw.tim.start(1)
    clock.sleep(0.5)
    reading = fw.tim.count(1)
    assert reading.irqs == 500
    fw.tim.stop(1)
    clock.sleep(1)
    assert fw.tim.count(1) == reading
    fw.tim.start(1)
    fw.tim.start(1)
    clock.sleep(0.03125)
    assert fw.tim.count(1) == timers.TimerCount(250, 531)
    assert reason(terminal, "tim.count 2") == "notopen"
    assert reason(terminal, "tim.count 1 ch=1") == "usage"


def test_free_running_counts_down_without_interrupts():
    _, _, clock, fw = make()
    fw.tim.open(2, prescaler=63999, period=65535, irq="none", mode="down")
    fw.tim.start(2)
    clock.sleep(0.25)
    assert fw.tim.count(2) == timers.TimerCount(65535 - 250, 0)


def test_timer_shared_with_pwm_qei_adc_and_tpwm():
    terminal, *_ = make()
    terminal.command("tim.open 2 irq=none")
    for line in ("pwm.open 2 channels=1", "qei.open 2", "adc.open 1 pins=ain1 timer=2", "tpwm.open 2 pins=tim2ch1", "tim.open 1"):
        assert reason(terminal, line) == "busy", line
    terminal.command("tim.close 2")
    terminal.command("pwm.open 2 channels=1")
    assert reason(terminal, "tim.open 2") == "busy"
    assert reason(terminal, "tim.open 1") == "ok"


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("tpwm.open 1", "usage"),
        ("tpwm.open 1 pins=-", "usage"),
        ("tpwm.open 1 pins=-,-", "usage"),
        ("tpwm.open 1 pins=,", "usage"),
        ("tpwm.open 1 pins=PA8,PA9,PA10,PA11,PA8", "usage"),
        ("tpwm.open 1 pins=PA8 prescaler=65536", "range"),
        ("tpwm.open 1 pins=PA8 period=0", "range"),
        ("tpwm.open 1 pins=PZ1 period=0", "range"),
        ("tpwm.open 1 pins=PA8 period=65536", "range"),
        ("tpwm.open 2 pins=PA15 period=0xFFFFFFFF", "ok"),
        ("tpwm.open 1 pins=PZ1", "pin"),
        ("tpwm.open 1 pins=PA9", "pin"),
        ("tpwm.open 1 pins=-,PA8", "pin"),
        ("tpwm.open 16 pins=PA6,-", "unsupported"),
        ("tpwm.open 16 pins=PA6,PA7", "pin"),
        ("tpwm.open 3 pins=PA8", "range"),
        ("tpwm.open 1 pins=-,PA9,PA10", "ok"),
        ("tpwm.open 1 pins=PA8,PA9,PA10,PA11", "ok"),
    ],
)
def test_timer_pwm_open_reasons(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


def test_timer_pwm_commands():
    terminal, fake, _, fw = make()
    assert fw.tpwm.open(1, pins=[None, "tim1ch2", "tim1ch3"], prescaler=63, period=99) == 64_000_000
    assert fake.received[-1] == "tpwm.open 1 pins=-,PA9,PA10 prescaler=63 period=99"
    assert reason(terminal, "gpio.cfg PA8 in") == "ok", "a `-` channel claims no pin"
    assert reason(terminal, "gpio.cfg PA9 in") == "busy"
    fw.tpwm.duty(1, 2, 50)
    fw.tpwm.pulse(1, 3, 10, 199)
    fw.tpwm.start(1, ch=2)
    fw.tpwm.stop(1)
    assert fake.received[-4:] == ["tpwm.duty 1 2 50", "tpwm.pulse 1 3 10 199", "tpwm.start 1 ch=2", "tpwm.stop 1"]
    state = fake.opened[("tpwm", "1")]
    assert state["ccr"] == [0, 49, 10] and state["period"] == 199
    for line, expected in (
        ("tpwm.duty 1 0 50", "range"),
        ("tpwm.duty 1 4 50", "range"),
        ("tpwm.duty 1 1 101", "range"),
        ("tpwm.duty 1 1", "usage"),
        ("tpwm.pulse 1 1 65536 2", "range"),
        ("tpwm.pulse 1 1 1 0", "range"),
        ("tpwm.start 1 ch=4", "range"),
        ("tpwm.start 1 ch=x", "usage"),
        ("tpwm.stop 1 channel=1", "usage"),
        ("tpwm.duty 2 1 50", "notopen"),
        ("tpwm.open 2 pins=PA15", "busy"),
        ("pwm.open 1 channels=1", "busy"),
        ("tim.open 1", "busy"),
    ):
        assert reason(terminal, line) == expected, line
    fw.tpwm.close(1)
    assert reason(terminal, "tpwm.duty 1 1 50") == "notopen"


@pytest.mark.parametrize(
    ("family", "line", "expected"),
    [
        ("stm32wb55", "lptim.open 0", "range"),
        ("stm32wb55", "lptim.open 3", "range"),
        ("stm32wb55", "lptim.open 1 period=0", "range"),
        ("stm32wb55", "lptim.open 1 period=65536", "range"),
        ("stm32wb55", "lptim.open 1 prescaler=0", "range"),
        ("stm32wb55", "lptim.open 1 prescaler=3", "range"),
        ("stm32wb55", "lptim.open 1 prescaler=256", "range"),
        ("stm32wb55", "lptim.open 1 prescaler=3 pin=PZ1", "pin"),
        ("stm32wb55", "lptim.open 1 prescaler=3 irq=none pin=gpio0", "range"),
        ("stm32wb55", "lptim.open 1 irq=nmi", "usage"),
        ("stm32wb55", "lptim.open 1 irq=none pin=gpio0", "usage"),
        ("stm32wb55", "lptim.open 1 pin=PC7", "pin"),
        ("stm32wb55", "lptim.open 1 rep=256", "range"),
        ("stm32wb55", "lptim.open 1 rep=0", "unsupported"),
        ("stm32wb55", "lptim.open 2 prescaler=128 pin=gpio0", "ok"),
        ("stm32wba55", "lptim.open 1 rep=255", "ok"),
        ("stm32wba55", "lptim.open 2 prescaler=64 irq=immediate pin=gpio0", "ok"),
    ],
)
def test_lptim_open_reasons(family, line, expected):
    terminal, *_ = make(family)
    assert reason(terminal, line) == expected


def test_lptim_counts_and_reports_its_clock():
    _, fake, clock, fw = make("stm32wba55")
    assert fw.lptim.open(1, prescaler=128, period=799, rep=1, irq="immediate") == 100_000_000
    assert fake.received[-1] == "lptim.open 1 period=799 prescaler=128 irq=immediate rep=1"
    fw.lptim.start(1)
    clock.sleep(1.0)
    assert fw.lptim.count(1).irqs == int(timers.lptim_update_rate(100_000_000, 128, 799, 1))


def test_lptim_shared_with_the_encoder_and_lptpwm():
    terminal, *_ = make("stm32wba55")
    terminal.command("qei.open 2 lp=1 a=lptim2in1 b=lptim2in2")
    assert reason(terminal, "lptim.open 2") == "busy"
    assert reason(terminal, "lptpwm.open 2 pins=PA11") == "busy"
    assert reason(terminal, "lptim.open 1") == "ok", "LPTIM1 is free"
    terminal.command("qei.close 2")
    terminal.command("lptim.close 1")
    terminal.command("lptpwm.open 2 pins=PA11,PA1")
    assert reason(terminal, "qei.open 2 lp=1 a=lptim2in1 b=lptim2in2") == "busy"
    assert reason(terminal, "lptim.open 2") == "busy"
    assert reason(terminal, "tim.open 2") == "ok", "TIM2 is not LPTIM2"


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("lptpwm.open 1", "usage"),
        ("lptpwm.open 1 pins=-,-", "usage"),
        ("lptpwm.open 1 pins=-,PA15,PA15", "usage"),
        ("lptpwm.open 3 pins=PA11", "range"),
        ("lptpwm.open 1 pins=-,PA15 prescaler=3", "range"),
        ("lptpwm.open 1 pins=-,PA15 period=65536", "range"),
        ("lptpwm.open 1 pins=PB11", "pin"),
        ("lptpwm.open 1 pins=PA15", "pin"),
        ("lptpwm.open 2 pins=PA1,PA11", "pin"),
        ("lptpwm.open 1 pins=-,PA8", "busy"),
        ("lptpwm.open 1 pins=-,PA15", "ok"),
        ("lptpwm.open 2 pins=PA11,PA1 prescaler=4 period=999", "ok"),
    ],
)
def test_lptim_pwm_open_reasons(line, expected):
    terminal, *_ = make("stm32wba55")
    assert reason(terminal, line) == expected


def test_lptim_pwm_is_unsupported_on_wb55_only():
    terminal, *_ = make()
    assert reason(terminal, "lptpwm.open 1 pins=PA15") == "unsupported"
    terminal, _, _, fw = make("stm32wba55")
    assert fw.lptpwm.open(2, pins=["lptim2ch1", None]) == 100_000_000
    with pytest.raises(FirmwareError) as error:
        fw.lptpwm.duty(2, 3, 50)
    assert error.value.reason == "range"


def test_groups_are_attached():
    *_, fw = make()
    assert isinstance(fw.tim, timers.Timer) and isinstance(fw.tpwm, timers.TimerPwm)
    assert isinstance(fw.lptim, timers.LpTimer) and isinstance(fw.lptpwm, timers.LpTimerPwm)


def test_expectations():
    assert timers.timer_update_rate(64_000_000, 63, 999) == 1000
    assert timers.lptim_update_rate(64_000_000, 64, 999) == 1000
    assert timers.lptim_update_rate(100_000_000, 128, 799, 3) == pytest.approx(244.140625)
    assert timers.pwm_high_ticks(99, 50) == 49
    assert timers.pwm_duty_fraction(99, 100) == 0.99, "100 % keeps one low tick"
    assert timers.pwm_duty_fraction(99, 0) == 0
    assert timers.pwm_high_ticks(0xFFFFFFFF, 50) == ((0xFFFFFFFF * 50) & 0xFFFFFFFF) // 100, "SetDuty wraps in 32 bits"
    assert (timers.timer_period_max(2), timers.timer_period_max(16)) == (0xFFFFFFFF, 0xFFFF)
