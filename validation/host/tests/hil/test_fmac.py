"""Filter math accelerator (`hal::SynchronousFmacStm`, `hal::FmacStm`, `hal::FmacStmDma`) through `fmac.load`,
`fmac.fir` and `fmac.iir`: the calibration vectors of `fmac_ref`, FIR and IIR filters bit for bit against the model
on every variant, the variants against each other, the stores kept between commands, and the duration. No wiring.

Scenarios: features/fmac.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import fmac_ref, patterns

CALIBRATION = {vector.name: vector for vector in fmac_ref.CALIBRATION}


@pytest.fixture
def fmac_cfg(board_cfg):
    return board_cfg.param("fmac")


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


def generated_input(fmac_cfg):
    return fmac_ref.unpack(patterns.generate(fmac_cfg["len"], "prbs", fmac_cfg["seed"]))


def coefficients(count, seed, limit):
    return fmac_ref.generate_samples(count, seed, limit)


@pytest.mark.parametrize("vector", list(CALIBRATION))
@scenario("fmac.feature", "The calibration vector matches the model")
def test_calibration(vector):
    pass


@pytest.mark.board_params("variant", "fmac.variants")
@pytest.mark.board_params("gain", "fmac.fir.gains")
@pytest.mark.board_params("taps", "fmac.fir.taps")
@scenario("fmac.feature", "A FIR matches the model bit for bit")
def test_fir(taps, gain, variant):
    pass


@pytest.mark.board_params("variant", "fmac.variants")
@pytest.mark.board_params("case", "fmac.iir")
@scenario("fmac.feature", "An IIR matches the model bit for bit")
def test_iir(case, variant):
    pass


@scenario("fmac.feature", "Every variant gives the same output and DMA is faster than interrupts")
def test_variants():
    pass


@scenario("fmac.feature", "The stores keep their values between commands")
def test_stores():
    pass


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("fmac.load", "usage"),
        ("fmac.load c 0 0040", "usage"),
        ("fmac.load b 0", "usage"),
        ("fmac.load b 0 -", "usage"),
        ("fmac.load b 0 004", "usage"),
        ("fmac.load b 0 00", "usage"),
        ("fmac.load b 0 0040 bogus=1", "usage"),
        ("fmac.load b 127 0040", "range"),
        ("fmac.load a 62 00400040", "range"),
        ("fmac.load x 125 00400040", "range"),
        ("fmac.load b 0 0040 clear=2", "range"),
        ("fmac.fir 0040", "usage"),
        ("fmac.fir 0040 taps=2 q=1", "usage"),
        ("fmac.fir 0040 taps=1", "range"),
        ("fmac.fir 0040 taps=128", "range"),
        ("fmac.fir 0040 taps=2 gain=8", "range"),
        ("fmac.fir 0040 taps=2 hist=127", "range"),
        ("fmac.fir 0040 taps=2 variant=fast", "usage"),
        ("fmac.fir 0040 taps=2 out=bin", "usage"),
        ("fmac.fir 004000 taps=2", "usage"),
        ("fmac.fir - taps=2", "usage"),
        ("fmac.fir - taps=2 len=3", "usage"),
        ("fmac.fir - taps=2 len=2049", "range"),
        ("fmac.fir - taps=2 len=130", "range"),
        ("fmac.iir 0040 p=2", "usage"),
        ("fmac.iir 0040 q=1", "usage"),
        ("fmac.iir 0040 p=65 q=1", "range"),
        ("fmac.iir 0040 p=2 q=64", "range"),
        ("fmac.iir 0040 p=2 q=0", "range"),
        ("fmac.iir 0040 p=2 q=1 yhist=64", "range"),
        ("fmac.iir 0040 p=2 q=1 taps=2", "usage"),
    ],
)
@scenario("fmac.feature", "Malformed and out-of-range commands are refused")
def test_errors(line, reason):
    pass


@given("the stores loaded with the vector")
def load_vector(fw, vector):
    case = CALIBRATION[vector]
    fw.fmac.load_all("b", case.b)
    fw.fmac.load_all("a", case.a)
    fw.fmac.load_all("x", case.xh)
    fw.fmac.load_all("y", case.yh)


@given("generated coefficients for the taps in the b store", target_fixture="b")
def load_fir(fw, fmac_cfg, taps):
    b = coefficients(taps, 7 + taps, fmac_cfg["coefficient_limit"])
    fw.fmac.load_all("b", b)
    return b


@given(parsers.parse("generated coefficients for {taps:d} taps in the b store"), target_fixture="b")
def load_fixed_fir(fw, fmac_cfg, taps):
    b = coefficients(taps, 7 + taps, fmac_cfg["coefficient_limit"])
    fw.fmac.load_all("b", b)
    return b


@given("generated coefficients for the IIR in the b and a stores", target_fixture="coefficients_ba")
def load_iir(fw, fmac_cfg, case):
    b = coefficients(case["p"], 11 + case["p"], fmac_cfg["coefficient_limit"])
    a = coefficients(case["q"], 13 + case["q"], fmac_cfg["feedback_limit"] // case["q"])
    fw.fmac.load_all("b", b)
    fw.fmac.load_all("a", a)
    return b, a


@when("the firmware filters the inputs of the vector", target_fixture="filtered")
def filter_vector(fw, vector):
    case = CALIBRATION[vector]
    options = {"gain": case.gain, "clip": case.clip, "hist": len(case.xh)}
    if case.iir:
        return fw.fmac.iir(case.x, len(case.b), len(case.a), yhist=len(case.yh), **options)
    return fw.fmac.fir(case.x, len(case.b), **options)


@when("the firmware runs the FIR on the generated input with the gain and the variant", target_fixture="run")
def run_fir(fw, fmac_cfg, b, gain, variant):
    reply = fw.fmac.fir(None, len(b), gain=gain, len=fmac_cfg["len"], pattern="prbs", seed=fmac_cfg["seed"], variant=variant, out="crc")
    return reply, fmac_ref.fir(b, generated_input(fmac_cfg), gain)


@when("the firmware runs the IIR on the generated input with the variant", target_fixture="run")
def run_iir(fw, fmac_cfg, coefficients_ba, variant):
    b, a = coefficients_ba
    reply = fw.fmac.iir(None, len(b), len(a), len=fmac_cfg["len"], pattern="prbs", seed=fmac_cfg["seed"], variant=variant, out="crc")
    return reply, fmac_ref.iir(b, a, generated_input(fmac_cfg))


@when("the firmware runs the 31-tap FIR on the generated input with every variant", target_fixture="runs")
def run_every_variant(fw, fmac_cfg, b):
    return {
        variant: fw.fmac.fir(None, len(b), len=fmac_cfg["len"], pattern="prbs", seed=fmac_cfg["seed"], variant=variant, out="crc")
        for variant in fmac_cfg["variants"]
    }


@when("the b store is cleared")
def clear_b(fw):
    fw.fmac.load("b", 0, [0], clear=True)


@then("the outputs and the saturation flag are the model's")
def vector_matches(filtered, vector):
    expected, saturated = CALIBRATION[vector].evaluate()
    assert filtered.samples == expected, f"{vector}: board {filtered.samples}, model {expected}"
    assert filtered.sat == saturated, vector


@then("the CRC and the saturation flag of the output are the model's")
def crc_matches(run):
    reply, (expected, saturated) = run
    assert (reply.length, reply.crc) == (2 * len(expected), fmac_ref.crc(expected))
    assert reply.sat == saturated


@then("every variant answers the CRC of the polled run")
def variants_agree(runs):
    polled = runs["poll"]
    for variant, run in runs.items():
        assert (run.length, run.crc, run.sat) == (polled.length, polled.crc, polled.sat), variant


@then("the DMA run took less time than the interrupt run")
def dma_is_faster(runs):
    assert runs["dma"].us < runs["it"].us, {variant: run.us for variant, run in runs.items()}


@then("every run took more than 0 and at most the maximum duration of the board file")
def durations(runs, fmac_cfg):
    for variant, run in runs.items():
        assert 0 < run.us <= fmac_cfg["max_us"], variant


@then("two runs of the 8-tap FIR give the same output")
def runs_repeat(fw, fmac_cfg, b):
    first = fw.fmac.fir(None, 8, len=64, pattern="prbs", seed=fmac_cfg["seed"])
    second = fw.fmac.fir(None, 8, len=64, pattern="prbs", seed=fmac_cfg["seed"])
    assert first.samples == second.samples == fmac_ref.fir(b, fmac_ref.unpack(patterns.generate(64, "prbs", fmac_cfg["seed"])))[0]


@then("the 8-tap FIR gives zeros")
def zeros(fw, fmac_cfg):
    assert fw.fmac.fir(None, 8, len=64, pattern="prbs", seed=fmac_cfg["seed"]).samples == [0] * 32


@then("the command line fails with the reason")
def command_refused(fw, line, reason):
    expect_reason(fw, line, reason)
