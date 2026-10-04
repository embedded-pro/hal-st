"""DMA (`hal::CircularTransmitDmaChannel`, PROTOCOL.md D.12): `dma.wave` writes one 32-bit BSRR word per pattern bit
on every TIM2 update, memory and peripheral side 32 bits wide (B.4 on both MCUs), so the pin shows the pattern,
least significant bit first, at `rate` bits per second.

Wiring set `bundle1` or `bundle2`: `tests.dma.pin` on a DIO; the argument and sharing checks need no AD3.
"""

from __future__ import annotations

import math
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

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


@pytest.mark.ad3
@pytest.mark.matrix("dma.wave")
def test_wave_32_bit(fw, ad3, need, board_cfg, dma_cfg, rate, pattern):
    """The LA sees `pattern` repeated at `rate` (TIM2 rounded to whole kernel clocks): each level lasts a whole
    number of bit times and the bits between the first and the last edge are a part of the repeated pattern."""
    pin = dma_cfg["pin"]
    dio = need.dio(pin)
    data = bytes.fromhex(pattern)
    actual = analog.trigger_rate(board_cfg.clock("timer"), rate)
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
    assert response is not None and response.ok, "dma.wave did not answer after ms"
    tolerance = dma_cfg["tolerance"]
    samples_per_bit = capture.rate / actual
    bits, first, last = decode_bits(capture.channel(dio), samples_per_bit, tolerance["bit"])
    assert is_cyclic_part(bits, analog.wave_bits(data)), f"{''.join(map(str, bits))} is no part of the pattern"
    measured = len(bits) * capture.rate / (last - first)
    assert measured == pytest.approx(actual, rel=tolerance["rate"]), "bit rate"


def test_wave_errors(fw, board_cfg, dma_cfg):
    """`rate` and `pattern` are required, `pattern` holds 1-32 bytes, `rate` 1-1000000 and `ms` 1-10000."""
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


def test_wave_shares_tim2_and_its_pin(fw, need, board_cfg, dma_cfg):
    """TIM2 paces the wave, so a PWM on TIM2 makes `dma.wave` busy; so does its pin held as a GPIO. Both are free
    again once the wave answered."""
    pin = dma_cfg["pin"]
    need.unloaded(pin)
    tim2 = next(timer for timer in board_cfg.param("pwm.timers") if timer["timer"] == 2)
    fw.pwm.open(2, pins=[tim2["channels"][0]["pin"]])
    with pytest.raises(FirmwareError) as error:
        fw.dma.wave(pin, rate=1000, pattern=b"\xa5", ms=10)
    assert error.value.reason == "busy", "TIM2 held by pwm"
    fw.pwm.close(2)
    fw.gpio.cfg(pin, "out")
    with pytest.raises(FirmwareError) as error:
        fw.dma.wave(pin, rate=1000, pattern=b"\xa5", ms=10)
    assert error.value.reason == "busy", "pin held by gpio"
    fw.gpio.release(pin)
    fw.dma.wave(pin, rate=1000, pattern=b"\xa5", ms=10)
    fw.gpio.cfg(pin, "out")
    fw.pwm.open(2, pins=[tim2["channels"][0]["pin"]])
