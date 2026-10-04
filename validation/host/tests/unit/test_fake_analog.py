"""The fake `ain` and `dma` groups (argument order and reasons of validation/firmware/AnalogInputGroup.cpp and
DmaGroup.cpp; PROTOCOL.md D.11, D.12), what they share with `adc`, `pwm` and the other TIM2 users, their deferred
final lines over a fake clock, and the `groups.analog` wrappers and expectations."""

import pytest
from ad3_waveforms_bench.protocol import parse_response
from ad3_waveforms_bench.terminal import FirmwareTerminal

from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.fakes import analog as fake_analog
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups import analog


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


def ticking(fake, clock, step):
    """Advance the fake clock by `step` whenever the host reads or writes, so deferred replies arrive."""
    original = fake.poll

    def poll():
        clock.now += step
        original()

    fake.poll = poll


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("ain.read", "usage"),
        ("ain.read 1", "usage"),
        ("ain.read 1 ain4 extra", "usage"),
        ("ain.read 1 ain4 speed=1", "usage"),
        ("ain.read x ain4", "usage"),
        ("ain.read 65536 ain4", "range"),
        ("ain.read 1 ain4 sampling=3.5", "usage"),
        ("ain.read 2 ain4", "range"),
        ("ain.read 2 ain4 sampling=3.5", "usage"),
        ("ain.read 1 PZ1", "pin"),
        ("ain.read 1 gpio0", "pin"),
        ("ain.read 1 PC7", "pin"),
        ("ain.read 1 terminaltx", "pin"),
        ("ain.read 1 ain4", "ok"),
        ("ain.read 1 ain4 sampling=640.5", "ok"),
        ("ain.read 1 temp sampling=640.5", "ok"),
    ],
)
def test_read_reasons(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("ain.burst 1 ain4", "usage"),
        ("ain.burst 1 ain4 n=16", "usage"),
        ("ain.burst 1 ain4 rate=1000", "usage"),
        ("ain.burst 1 n=16 rate=1000", "usage"),
        ("ain.burst 1 ain4 n=16 rate=1000 speed=1", "usage"),
        ("ain.burst 1 ain4 n=x rate=1000", "usage"),
        ("ain.burst 1 ain4 n=0 rate=1000", "range"),
        ("ain.burst 1 ain4 n=257 rate=1000", "range"),
        ("ain.burst 1 ain4 n=16 rate=9", "range"),
        ("ain.burst 1 ain4 n=16 rate=100001", "range"),
        ("ain.burst 1 ain4 n=16 rate=1000 repeat=0", "range"),
        ("ain.burst 1 ain4 n=16 rate=1000 repeat=3", "range"),
        ("ain.burst 1 ain4 n=16 rate=1000 out=hex", "usage"),
        ("ain.burst 1 ain4 n=65 rate=1000", "range"),
        ("ain.burst 4 ain4 n=16 rate=1000", "range"),
        ("ain.burst 4 gpio0 n=16 rate=1000", "range"),
        ("ain.burst 1 gpio0 n=16 rate=1000", "pin"),
        ("ain.burst 1 PZ1 n=16 rate=1000", "pin"),
    ],
)
def test_burst_reasons(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


def test_read_codes_and_temperature():
    _, fake, _, fw = make("stm32wba55")
    fake.adc_codes["PA7"] = 1234
    assert fw.ain.read(4, "ain2") == analog.AinReading(code=1234, mcelsius=None)
    assert fake.received[-1] == "ain.read 4 PA7"
    reading = fw.ain.read(4, "temp", sampling="814.5")
    assert reading.mcelsius == fake_analog.TEMPERATURE_MCELSIUS
    assert fake.received[-1] == "ain.read 4 temp sampling=814.5"
    assert fake.resources == {} and fake.claims == {}


def test_burst_answers_after_n_over_rate_and_holds_its_claims_until_then():
    terminal, fake, clock, _ = make()
    fake.adc_codes["PC3"] = 777
    started = clock.now
    pending = terminal.begin("ain.burst 1 ain4 n=100 rate=1000 out=stats")
    clock.now = started + 0.099
    terminal.pump()
    assert fake.group("ain").burst is not None, "answered before n / rate"
    owner = fake_analog.FakeAnalogInput.owner
    assert fake.resources == {("adc", 1): owner, ("dma1", 7): owner}
    assert fake.timer_owners == {2: owner} and fake.claims["PC3"] == (owner, True)
    clock.now = started + 0.1
    run = analog.BurstRun.from_values(pending.wait())
    assert run == analog.BurstRun(n=100, minimum=777, maximum=777, mean=777, us=100_000, samples=None)
    assert fake.resources == {} and fake.timer_owners == {} and fake.claims == {}


def test_burst_twice_reports_the_first_run_as_event():
    terminal, fake, clock, fw = make()
    fake.adc_codes["PC3"] = 100
    pending = fw.ain.begin("burst", 1, "PC3", n=16, rate=1000, repeat=2)
    clock.now += 0.016
    assert [event.as_int("run") for event in terminal.events("ain")] == [1]
    clock.now += 0.016
    response = pending.wait()
    first = terminal.drain_events("ain")
    runs = [analog.BurstRun.from_values(first[0]), analog.BurstRun.from_values(response)]
    assert [run.samples for run in runs] == [(100,) * 16] * 2
    assert [run.us for run in runs] == [16_000, 16_000]


def test_burst_wrapper_collects_every_run():
    _, fake, clock, fw = make()
    ticking(fake, clock, 0.001)
    fake.adc_codes["PC3"] = 5
    runs = fw.ain.burst(1, "ain4", n=16, rate=1000, repeat=2)
    assert [run.n for run in runs] == [16, 16]
    assert all(run.mean == 5 and run.samples == (5,) * 16 for run in runs)
    assert fw.ain.burst(1, "ain4", n=200, rate=10000, out="stats")[0].n == 200


def test_ain_shares_the_adc_tim2_and_the_dma_channel():
    terminal, fake, clock, fw = make()
    fw.adc.open(1, pins=["ain4"])
    assert reason(terminal, "ain.read 1 ain3") == "busy"
    assert reason(terminal, "ain.read 1 temp") == "busy"
    assert reason(terminal, "ain.burst 1 ain3 n=16 rate=1000") == "busy"
    fw.adc.close(1)
    fw.pwm.open(2, pins=["tim2ch1"])
    assert reason(terminal, "ain.burst 1 ain3 n=16 rate=1000") == "busy"
    assert reason(terminal, "ain.read 1 ain3") == "ok"
    fw.pwm.close(2)
    fake.resources[("dma1", 7)] = ("spis", "1")
    assert reason(terminal, "ain.burst 1 ain3 n=16 rate=1000") == "busy"
    assert reason(terminal, "ain.read 1 ain3") == "ok"
    del fake.resources[("dma1", 7)]
    pending = terminal.begin("ain.burst 1 ain4 n=16 rate=1000")
    clock.now += 0.016
    assert pending.wait().ok
    assert reason(terminal, "adc.open 1 pins=PC2 timer=2") == "ok"


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("dma.wave", "usage"),
        ("dma.wave gpio0 rate=1000", "usage"),
        ("dma.wave gpio0 pattern=a5", "usage"),
        ("dma.wave gpio0 PA1 rate=1000 pattern=a5", "usage"),
        ("dma.wave gpio0 rate=1000 pattern=a5 speed=1", "usage"),
        ("dma.wave gpio0 rate=x pattern=a5", "usage"),
        ("dma.wave gpio0 rate=0 pattern=a5", "range"),
        ("dma.wave gpio0 rate=1000001 pattern=a5", "range"),
        ("dma.wave gpio0 rate=1000 pattern=-", "usage"),
        ("dma.wave gpio0 rate=1000 pattern=a", "usage"),
        ("dma.wave gpio0 rate=1000 pattern=zz", "usage"),
        ("dma.wave gpio0 rate=1000 pattern=" + "00" * 33, "range"),
        ("dma.wave gpio0 rate=1000 pattern=a5 ms=0", "range"),
        ("dma.wave gpio0 rate=1000 pattern=a5 ms=10001", "range"),
        ("dma.wave gpio0 rate=0 pattern=zz", "range"),
        ("dma.wave PZ1 rate=1000 pattern=a5", "pin"),
        ("dma.wave PC7 rate=1000 pattern=a5", "pin"),
        ("dma.wave terminaltx rate=1000 pattern=a5", "busy"),
    ],
)
def test_wave_reasons(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(("family", "resource"), [("stm32wb55", ("dma2", 4)), ("stm32wba55", ("dma1", 8))])
def test_wave_holds_its_claims_for_ms(family, resource):
    terminal, fake, clock, _ = make(family)
    pin = "PC6" if family == "stm32wb55" else "PB14"
    pending = terminal.begin("dma.wave gpio0 rate=1000000 pattern=" + "ff" * 32 + " ms=200")
    owner = fake_analog.FakeDma.owner
    clock.now += 0.199
    terminal.pump()
    assert fake.group("dma").wave is not None, "answered before ms"
    assert fake.resources == {resource: owner} and fake.timer_owners == {2: owner} and fake.claims == {pin: (owner, False)}
    clock.now += 0.001
    assert pending.wait().ok
    assert fake.resources == {} and fake.timer_owners == {} and fake.claims == {}
    assert reason(terminal, "gpio.cfg gpio0 out") == "ok"


def test_wave_busy_with_tim2_users_and_the_shared_channel():
    terminal, fake, clock, fw = make("stm32wba55")
    fw.pwm.open(2, pins=["tim2ch1"])
    assert reason(terminal, "dma.wave gpio1 rate=1000 pattern=a5") == "busy"
    fw.pwm.close(2)
    fake.resources[("dma1", 8)] = ("spis", "1")
    assert reason(terminal, "dma.wave gpio1 rate=1000 pattern=a5") == "busy"
    del fake.resources[("dma1", 8)]
    fw.gpio.cfg("gpio1", "out")
    assert reason(terminal, "dma.wave gpio1 rate=1000 pattern=a5") == "busy"
    pending = terminal.begin("dma.wave gpio0 rate=1000 pattern=a5")
    clock.now += 0.05
    assert pending.wait().ok


def test_wave_wrapper_sends_hex_and_waits():
    _, fake, clock, fw = make()
    ticking(fake, clock, 0.01)
    fw.dma.wave("gpio0", rate=100000, pattern=bytes([0xA5, 0x0F]), ms=20)
    assert fake.received[-1] == "dma.wave PC6 rate=100000 pattern=a50f ms=20"


def test_reboot_drops_the_pending_commands():
    terminal, fake, *_ = make()
    terminal.begin("dma.wave gpio0 rate=1000 pattern=a5 ms=10000")
    fake.boot()
    assert fake.group("dma").wave is None and fake.resources == {}
    assert fake.group("ain").burst is None


def test_wave_bits_are_lsb_first():
    assert analog.wave_bits(bytes([0x01, 0x80])) == [1, 0, 0, 0, 0, 0, 0, 0] + [0, 0, 0, 0, 0, 0, 0, 1]


@pytest.mark.parametrize(
    ("clock", "rate", "expected"),
    [
        (64_000_000, 1_000_000, (0, 63)),
        (64_000_000, 1000, (0, 63_999)),
        (100_000_000, 300_000, (0, 332)),
        (64_000_000, 32_000_000, (0, 1)),
    ],
)
def test_trigger_timing_matches_the_firmware(clock, rate, expected):
    assert analog.trigger_timing(clock, rate) == expected


def test_trigger_timing_on_16_bit_counters():
    prescaler, period = analog.trigger_timing(64_000_000, 10, counter_bits=16)
    assert period < 1 << 16
    assert 64_000_000 / ((prescaler + 1) * (period + 1)) == pytest.approx(10, rel=1e-4)


def test_burst_seconds_follow_the_real_rate():
    assert analog.burst_seconds(100, 64_000_000, 1000) == pytest.approx(0.1)
    assert analog.burst_seconds(3, 100_000_000, 300_000) == pytest.approx(3 * 333 / 100_000_000)


def test_stats_and_list_runs_agree():
    listed = analog.BurstRun.from_values(parse_response("OK samples=1,2,4 us=30"))
    assert (listed.n, listed.minimum, listed.maximum, listed.mean, listed.us) == (3, 1, 4, 2, 30)
    assert listed.samples == (1, 2, 4)
    stats = analog.BurstRun.from_values(parse_response("OK n=3 min=1 max=4 mean=2 us=30"))
    assert stats == analog.BurstRun(n=3, minimum=1, maximum=4, mean=2, us=30, samples=None)
