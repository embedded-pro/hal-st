"""CORDIC co-processor (`hal::SynchronousCordicStm`, `hal::CordicStm`, `hal::CordicStmDma`) through `cordic.calc`:
every function over its domain against `cordic_ref` within the bound of the precision, q1.15 packing, accuracy that
grows with the precision, the same words from the four variants and the measured duration. No wiring.

Scenarios: features/cordic.feature.
"""

from __future__ import annotations

import math

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, scenario, then, when

from hal_st_validation import cordic_ref


@pytest.fixture
def cordic_cfg(board_cfg):
    return board_cfg.param("cordic")


@pytest.fixture
def tolerance(cordic_cfg):
    return cordic_ref.Tolerance(**cordic_cfg["tolerance"])


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


def counts(function):
    return (2 if function in cordic_ref.TWO_ARGUMENTS else 1), (2 if function in cordic_ref.TWO_RESULTS else 1)


def calculate(fw, function, scale, arguments, precision, width=32):
    nargs, nres = counts(function)
    data = cordic_ref.pack_arguments(arguments, width, nargs)
    reply = fw.cordic.calc(function, data, precision=precision, scale=scale, insize=width, outsize=width, nargs=nargs, nres=nres)
    sent = cordic_ref.unpack_arguments(data, width, nargs)
    return sent, cordic_ref.unpack_results(reply.data, width, nres)


@pytest.mark.board_params("precision", "cordic.precisions")
@pytest.mark.board_params("function, scale", "cordic.functions")
@scenario("cordic.feature", "Each function matches the reference within the bound of the precision")
def test_function_accuracy(function, scale, precision):
    pass


@pytest.mark.board_params("function, scale", "cordic.q15_functions")
@scenario("cordic.feature", "q1.15 packs two arguments and two results per word")
def test_q15(function, scale):
    pass


@pytest.mark.board_params("function, scale", "cordic.sweep_functions")
@scenario("cordic.feature", "A lower precision is less accurate")
def test_precision_sweep(function, scale):
    pass


@scenario("cordic.feature", "The four variants calculate the same words")
def test_variants_agree():
    pass


@pytest.mark.board_params("variant", "cordic.variants")
@scenario("cordic.feature", "One calculation answers in every variant")
def test_one_calculation(variant):
    pass


@scenario("cordic.feature", "DMA is faster than interrupts")
def test_dma_is_faster():
    pass


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("cordic.calc", "usage"),
        ("cordic.calc cos", "usage"),
        ("cordic.calc tan 00000000", "usage"),
        ("cordic.calc cos 00000000 extra", "usage"),
        ("cordic.calc cos 00000000 bogus=1", "usage"),
        ("cordic.calc cos 000000", "usage"),
        ("cordic.calc cos 0000", "usage"),
        ("cordic.calc cos 00000000 nargs=2", "usage"),
        ("cordic.calc cos -", "usage"),
        ("cordic.calc cos - pattern=inc", "usage"),
        ("cordic.calc cos 00000000 len=4", "usage"),
        ("cordic.calc cos 00000000 insize=8", "usage"),
        ("cordic.calc cos 00000000 variant=fast", "usage"),
        ("cordic.calc cos 00000000 out=bin", "usage"),
        ("cordic.calc cos 00000000 precision=0", "range"),
        ("cordic.calc cos 00000000 precision=16", "range"),
        ("cordic.calc cos 00000000 scale=8", "range"),
        ("cordic.calc cos 00000000 nargs=3", "range"),
        ("cordic.calc cos 00000000 nres=0", "range"),
        ("cordic.calc cos - len=2049", "range"),
        ("cordic.calc cos - len=256", "range"),
        ("cordic.calc cos - len=128 nres=2", "range"),
    ],
)
@scenario("cordic.feature", "Malformed and out-of-range commands are refused")
def test_errors(line, reason):
    pass


@given("arguments spread over the domain of the function at its scale", target_fixture="arguments")
def arguments(cordic_cfg, function, scale):
    return cordic_ref.sweep(function, scale, cordic_cfg["samples"])


@when("the firmware calculates them in q1.31 with the precision", target_fixture="calculated")
def calculate_q31(fw, function, scale, arguments, precision):
    return calculate(fw, function, scale, arguments, precision)


@when("the firmware calculates them in q1.15", target_fixture="calculated")
def calculate_q15(fw, cordic_cfg, function, scale, arguments):
    return calculate(fw, function, scale, arguments, cordic_cfg["q15_precision"], 16)


@when("the firmware calculates them in q1.31 at every precision of the board file", target_fixture="largest_errors")
def calculate_every_precision(fw, cordic_cfg, function, scale, arguments):
    largest = {}
    for precision in cordic_cfg["sweep_precisions"]:
        sent, results = calculate(fw, function, scale, arguments, precision)
        largest[precision] = max(cordic_ref.errors(function, scale, sent, results))
    return largest


@when("the firmware calculates the cosine of 0 alone with the variant", target_fixture="single")
def calculate_one(fw, variant):
    return fw.cordic.calc("cos", cordic_ref.pack_arguments([(0.0, 1.0)]), variant=variant)


@then("the result is 1 within the bound of the default precision")
def single_is_one(single, tolerance):
    (result,) = cordic_ref.unpack_results(single.data)
    assert abs(result[0] - 1.0) <= tolerance.bound("cos", 6, 32), result


@when("the firmware calculates the generated batch with each variant", target_fixture="batches")
def calculate_batches(fw, cordic_cfg):
    bulk = cordic_cfg["bulk"]
    return {
        variant: fw.cordic.calc(
            bulk["function"],
            len=bulk["len"],
            pattern="prbs",
            seed=bulk["seed"],
            nargs=bulk["nargs"],
            nres=bulk["nres"],
            variant=variant,
            out="crc",
        )
        for variant in cordic_cfg["variants"]
    }


@then("every result is within the bound of the precision from the reference")
def within_q31_bound(function, scale, precision, calculated, tolerance):
    sent, results = calculated
    bound = tolerance.bound(function, precision, 32)
    found = cordic_ref.errors(function, scale, sent, results)
    assert max(found) <= bound, f"largest error {max(found):.3e} (2^{math.log2(max(found) or 2.0**-64):.1f}) > {bound:.3e}"


@then("every q1.15 result is within the bound of the precision from the reference")
def within_q15_bound(cordic_cfg, function, scale, calculated, tolerance):
    sent, results = calculated
    bound = tolerance.bound(function, cordic_cfg["q15_precision"], 16)
    found = cordic_ref.errors(function, scale, sent, results)
    assert max(found) <= bound, f"largest error {max(found):.3e} > {bound:.3e}"


@then("the largest error does not grow with the precision")
def error_does_not_grow(largest_errors, tolerance):
    floor = tolerance.floor_q31 * cordic_ref.lsb(32)
    precisions = sorted(largest_errors)
    for lower, higher in zip(precisions, precisions[1:], strict=False):
        assert largest_errors[higher] <= largest_errors[lower] + floor, largest_errors


@then("the largest error at the lowest precision exceeds the one at the highest")
def lowest_is_worse(largest_errors):
    precisions = sorted(largest_errors)
    assert largest_errors[precisions[0]] > largest_errors[precisions[-1]], largest_errors


@then("every variant answers the length and CRC of the polled batch")
def variants_agree(batches):
    polled = batches["poll"]
    for variant, batch in batches.items():
        assert (batch.length, batch.crc) == (polled.length, polled.crc), variant


@then("the DMA variant took less time than the interrupt variant")
def dma_is_faster(batches):
    assert batches["dma"].us < batches["it"].us, {variant: batch.us for variant, batch in batches.items()}


@then("every variant took more than 0 and at most the maximum duration of the board file")
def durations(batches, cordic_cfg):
    for variant, batch in batches.items():
        assert 0 < batch.us <= cordic_cfg["max_us"], variant


@then("the command line fails with the reason")
def command_refused(fw, line, reason):
    expect_reason(fw, line, reason)
