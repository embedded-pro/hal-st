"""DMA (`hal::CircularTransmitDmaChannel`, PROTOCOL.md D.12): `dma.wave` writes one 32-bit BSRR word per pattern bit
on every TIM2 update, memory and peripheral side 32 bits wide (B.4 on both MCUs), so the pin shows the pattern,
least significant bit first, at `rate` bits per second.

Wiring set `bundle1` or `bundle2`: `tests.dma.pin` on a DIO; the argument and sharing checks need no AD3.

Scenarios: features/dma.feature.
"""

from __future__ import annotations

import math
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, scenario, then, when

from hal_st_validation.firmware import settle
from hal_st_validation.groups import analog


@pytest.fixture
def dma_cfg(board_cfg):
    return board_cfg.param("dma")


def decode_bits(levels, samples_per_bit, tolerance):
    """The bits between the first and the last edge: every level run lasts a whole number of bit times."""
    runs = analysis.runs(levels)[1:-1]
    assert runs, "fewer than two edges captured"
    bits: list[int] = []
    for level, _, length in runs:
        count = length / samples_per_bit
        assert abs(count - round(count)) <= tolerance, f"a run of {count:.2f} bit times"
        bits += [level] * round(count)
    return bits, runs[0][1], runs[-1][1] + runs[-1][2]


def is_cyclic_part(bits, pattern):
    repeated = pattern * (len(bits) // len(pattern) + 2)
    return any(repeated[start : start + len(bits)] == bits for start in range(len(pattern)))


@pytest.mark.matrix("dma.wave")
@scenario("dma.feature", "The pin shows the pattern repeated at the rate")
def test_wave_32_bit(rate, pattern):
    pass


@scenario("dma.feature", "Missing and out-of-range wave arguments are refused")
def test_wave_errors():
    pass


@scenario("dma.feature", "The wave shares TIM2 and its pin")
def test_wave_shares_tim2_and_its_pin():
    pass


@given("the DMA pin is wired to a DIO", target_fixture="dio")
def pin_wired(need, dma_cfg):
    return need.dio(dma_cfg["pin"])


@given("the actual rate: the rate rounded to whole timer kernel clocks", target_fixture="actual")
def actual_rate(board_cfg, rate):
    return analog.trigger_rate(board_cfg.clock("timer"), rate)


@when(
    "the wave of the pattern runs at the rate for the wave time, at least long enough for the start delay and the recording, while "
    "the logic analyzer records the record bits at the samples per bit after the start delay",
    target_fixture="wave",
)
def wave_recorded(fw, ad3, dma_cfg, rate, pattern, actual):
    pin = dma_cfg["pin"]
    data = bytes.fromhex(pattern)
    sample_rate = min(ad3.logic.clock_hz, actual * dma_cfg["samples_per_bit"])
    samples = min(ad3.logic.buffer_size, math.ceil(dma_cfg["record_bits"] * sample_rate / actual))
    delay = dma_cfg["start_delay_s"]
    ms = max(dma_cfg["ms"], math.ceil((delay + samples / sample_rate + 0.1) * 1000))
    pending = fw.dma.begin("wave", fw.pin(pin), rate=rate, pattern=data, ms=ms, cmd_timeout=fw.terminal.timeout + ms / 1000)
    try:
        time.sleep(delay)
        capture = ad3.logic.record(sample_rate, samples, timeout=samples / sample_rate + 2)
    finally:
        response = settle(pending)
    return {"capture": capture, "response": response}


@then("dma.wave answered OK after the wave time")
def wave_answered(wave):
    response = wave["response"]
    assert response is not None and response.ok, "dma.wave did not answer after ms"


@then(
    "every level on the DIO lasts a whole number of bit times within the tolerance and the bits between the first and the last edge "
    "are a part of the repeated pattern",
    target_fixture="decoded",
)
def wave_bits(dma_cfg, pattern, dio, actual, wave):
    capture = wave["capture"]
    data = bytes.fromhex(pattern)
    tolerance = dma_cfg["tolerance"]
    samples_per_bit = capture.rate / actual
    bits, first, last = decode_bits(capture.channel(dio), samples_per_bit, tolerance["bit"])
    assert is_cyclic_part(bits, analog.wave_bits(data)), f"{''.join(map(str, bits))} is no part of the pattern"
    return bits, first, last


@then("those bits run at the actual rate within the tolerance")
def wave_rate(dma_cfg, actual, wave, decoded):
    capture = wave["capture"]
    bits, first, last = decoded
    tolerance = dma_cfg["tolerance"]
    measured = len(bits) * capture.rate / (last - first)
    assert measured == pytest.approx(actual, rel=tolerance["rate"]), "bit rate"


@then(
    "a wave on the DMA pin without pattern, without rate, with pattern -, with odd hex, a pattern too long, rate 0, a rate too high, "
    "ms 0, ms too long, on two pins, on an unbonded pin, on an unknown pin or on the terminal TX pin fails with usage, usage, usage, "
    "usage, range, range, range, range, range, usage, pin, pin and busy"
)
def wave_refused(fw, board_cfg, dma_cfg):
    pin = dma_cfg["pin"]
    unbonded = board_cfg.param("system.unbonded_pins")[0]
    wave = {"rate": 1000, "pattern": "a5"}
    cases = [
        ((pin,), {"rate": 1000}, "usage"),
        ((pin,), {"pattern": "a5"}, "usage"),
        ((pin,), {**wave, "pattern": "-"}, "usage"),
        ((pin,), {**wave, "pattern": "abc"}, "usage"),
        ((pin,), {**wave, "pattern": "00" * (analog.DMA_PATTERN_MAX + 1)}, "range"),
        ((pin,), {**wave, "rate": 0}, "range"),
        ((pin,), {**wave, "rate": analog.DMA_RATE_MAX + 1}, "range"),
        ((pin,), {**wave, "ms": 0}, "range"),
        ((pin,), {**wave, "ms": analog.DMA_WAVE_MAX_MS + 1}, "range"),
        ((pin, pin), wave, "usage"),
        ((unbonded,), wave, "pin"),
        (("nosuchpin",), wave, "pin"),
        (("terminaltx",), wave, "busy"),
    ]
    for args, options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.command("dma.wave", *args, **options)
        assert error.value.reason == reason, (args, options)


@given("the DMA pin is loaded by no option the test does not handle")
def pin_unloaded(need, dma_cfg):
    need.unloaded(dma_cfg["pin"])


@given("the first channel pin of TIM2 among the PWM timers", target_fixture="tim2_pin")
def tim2_channel(board_cfg):
    tim2 = next(timer for timer in board_cfg.param("pwm.timers") if timer["timer"] == 2)
    return tim2["channels"][0]["pin"]


@given("the PWM opens TIM2 on that pin")
@when("the PWM opens TIM2 on that pin")
@then("the PWM opens TIM2 on that pin")
def pwm_opened(fw, tim2_pin):
    fw.pwm.open(2, pins=[tim2_pin])


@then('a wave of a5 at 1000 bit/s for 10 ms on the DMA pin fails with "busy", as the PWM holds TIM2')
def wave_busy_pwm(fw, dma_cfg):
    with pytest.raises(FirmwareError) as error:
        fw.dma.wave(dma_cfg["pin"], rate=1000, pattern=b"\xa5", ms=10)
    assert error.value.reason == "busy", "TIM2 held by pwm"


@then('a wave of a5 at 1000 bit/s for 10 ms on the DMA pin fails with "busy", as a GPIO holds the pin')
def wave_busy_gpio(fw, dma_cfg):
    with pytest.raises(FirmwareError) as error:
        fw.dma.wave(dma_cfg["pin"], rate=1000, pattern=b"\xa5", ms=10)
    assert error.value.reason == "busy", "pin held by gpio"


@then("a wave of a5 at 1000 bit/s for 10 ms on the DMA pin runs")
def wave_runs(fw, dma_cfg):
    fw.dma.wave(dma_cfg["pin"], rate=1000, pattern=b"\xa5", ms=10)


@when("the PWM closes TIM2")
def pwm_closed(fw):
    fw.pwm.close(2)


@when("the DMA pin is configured as a GPIO output")
@then("the DMA pin is configured as a GPIO output")
def pin_as_gpio(fw, dma_cfg):
    fw.gpio.cfg(dma_cfg["pin"], "out")


@when("the GPIO of the DMA pin is released")
def gpio_released(fw, dma_cfg):
    fw.gpio.release(dma_cfg["pin"])
