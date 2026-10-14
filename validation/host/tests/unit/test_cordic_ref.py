"""`cordic_ref` (the CORDIC reference model), the fake `cordic` group (argument order and reasons of
validation/firmware/CordicGroup.cpp) and the `groups.cordic` wrapper."""

import math
import struct

import pytest
from ad3_waveforms_bench.terminal import FirmwareTerminal

from hal_st_validation import cordic_ref
from hal_st_validation.fake_firmware import G474_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware


def make(family="stm32g474"):
    fake = FakeFirmware(family=family)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    return terminal, fake, Firmware(terminal, G474_PINS)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


@pytest.mark.parametrize(
    ("value", "width", "code"),
    [(0.0, 32, 0), (0.5, 32, 0x40000000), (-1.0, 32, -(1 << 31)), (1.0, 32, (1 << 31) - 1), (2.0, 16, 0x7FFF), (-0.5, 16, -0x4000)],
)
def test_encode_saturates(value, width, code):
    assert cordic_ref.encode(value, width) == code


def test_decode_sign_extends():
    assert cordic_ref.decode(0xFFFFFFFF, 32) == -(2.0**-31)
    assert cordic_ref.decode(0x8000, 16) == -1.0
    assert cordic_ref.decode(0x4000, 16) == 0.5


@pytest.mark.parametrize(("insize", "nargs"), [(32, 1), (32, 2), (16, 1), (16, 2)])
def test_arguments_round_trip(insize, nargs):
    arguments = [(0.25, -0.5), (-0.75, 0.125)]
    data = cordic_ref.pack_arguments(arguments, insize, nargs)
    assert len(data) == len(arguments) * cordic_ref.WORD_BYTES * cordic_ref.argument_words(insize, nargs)
    decoded = cordic_ref.unpack_arguments(data, insize, nargs)
    for (arg1, arg2), (back1, back2) in zip(arguments, decoded, strict=True):
        assert back1 == arg1
        assert back2 == (arg2 if insize == 16 or nargs == 2 else cordic_ref.decode(0x7FFFFFFF, 32))


def test_q15_packs_the_second_value_high():
    (word,) = struct.unpack("<I", cordic_ref.pack_arguments([(0.5, -0.5)], 16))
    assert word == 0xC0004000
    assert cordic_ref.unpack_results(struct.pack("<I", word), 16) == [(0.5, -0.5)]


def test_results_by_nres():
    data = cordic_ref.pack_results([(0.5, -0.25), (0.125, 0.0)], 32, 2)
    assert cordic_ref.unpack_results(data, 32, 2) == [(0.5, -0.25), (0.125, 0.0)]
    assert cordic_ref.unpack_results(cordic_ref.pack_results([(0.5, -0.25)], 32, 1), 32, 1) == [(0.5,)]


@pytest.mark.parametrize(
    ("function", "scale", "arg1", "arg2", "expected"),
    [
        ("cos", 0, 0.0, 1.0, (1.0, 0.0)),
        ("sin", 0, 0.5, 0.5, (0.5, 0.5 * math.cos(math.pi / 2))),
        ("phase", 0, 0.0, 0.5, (0.5, 0.5)),
        ("mod", 0, -0.3, -0.4, (0.5, math.atan2(-0.4, -0.3) / math.pi)),
        ("atan", 3, 1 / 8, 0.0, (0.25 / 8, 0.0)),
        ("cosh", 1, 0.0, 0.0, (0.5, 0.0)),
        ("atanh", 1, 0.25, 0.0, (math.atanh(0.5) / 2, 0.0)),
        ("ln", 1, 0.5, 0.0, (0.0, 0.0)),
        ("ln", 2, 0.25 * math.e, 0.0, (0.125, 0.0)),
        ("sqrt", 1, 0.5, 0.0, (0.5, 0.0)),
        ("sqrt", 2, 0.5625, 0.0, (0.375, 0.0)),
    ],
)
def test_reference_conventions(function, scale, arg1, arg2, expected):
    assert cordic_ref.reference(function, scale, arg1, arg2) == pytest.approx(expected, abs=1e-12)


@pytest.mark.parametrize(
    ("function", "scale"), [(function, scale) for function in cordic_ref.FUNCTIONS for scale in cordic_ref.scales(function)]
)
def test_domains_give_representable_results(function, scale):
    for arg1, arg2 in cordic_ref.sweep(function, scale, 9):
        for value in cordic_ref.reference(function, scale, arg1, arg2):
            assert -1.0 <= value <= 1.0, (function, scale, arg1, arg2, value)


@pytest.mark.parametrize(("scale", "limit"), [(0, 0.75), (1, 1.75), (2, 2.341)])
def test_sqrt_arguments_stay_below_the_exclusive_range_limit(scale, limit):
    """RM0440's `sqrt` ranges end below 0.75, 1.75 and 2.341; the bench saw results far off at exactly the limit."""
    largest = max(arg1 for arg1, _ in cordic_ref.sweep("sqrt", scale, 9)) * 2.0**scale
    assert largest < limit


def test_out_of_domain_arguments_do_not_raise():
    for function in cordic_ref.FUNCTIONS:
        for argument in (-1.0, -0.5, 0.0, 0.999):
            cordic_ref.reference(function, max(cordic_ref.scales(function)), argument, argument)


def test_bound_shrinks_with_the_precision_down_to_the_floor():
    tolerance = cordic_ref.Tolerance(extra_bits={"ln": 2})
    bounds = [tolerance.bound("cos", precision, 32) for precision in cordic_ref.PRECISIONS]
    assert bounds == sorted(bounds, reverse=True)
    assert bounds[-1] == tolerance.floor_q31 * 2.0**-31
    assert tolerance.bound("ln", 3, 32) == 4 * tolerance.bound("cos", 3, 32)
    assert tolerance.bound("cos", 15, 16) == 2 * 2.0**-15


@pytest.mark.parametrize("precision", [1, 3, 6, 15])
def test_simulation_stays_within_the_bound(precision):
    tolerance = cordic_ref.Tolerance()
    arguments = cordic_ref.sweep("cos", 0, 8)
    data = cordic_ref.pack_arguments(arguments, 32, 2)
    results = cordic_ref.unpack_results(cordic_ref.simulate("cos", 0, precision, data, 32, 32, 2, 2), 32, 2)
    sent = cordic_ref.unpack_arguments(data, 32, 2)
    assert max(cordic_ref.errors("cos", 0, sent, results)) <= tolerance.bound("cos", precision, 32)


def test_fake_answers_the_simulation():
    _, _, fw = make()
    arguments = cordic_ref.sweep("sqrt", 1, 4)
    data = cordic_ref.pack_arguments(arguments)
    reply = fw.cordic.calc("sqrt", data, precision=4, scale=1)
    assert reply.data == cordic_ref.simulate("sqrt", 1, 4, data, 32, 32, 1, 1)
    assert reply.us > 0


def test_fake_crc_output_and_variants():
    _, _, fw = make()
    replies = {
        variant: fw.cordic.calc("cos", len=2048, pattern="prbs", seed=3, nres=2, variant=variant, out="crc")
        for variant in ("poll", "zo", "it", "dma")
    }
    assert len({(reply.length, reply.crc) for reply in replies.values()}) == 1
    assert replies["poll"].length == 4096
    assert replies["dma"].us < replies["it"].us


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("cordic.calc", "usage"),
        ("cordic.calc tan 00000000", "usage"),
        ("cordic.calc tan 00000000 precision=0", "usage"),
        ("cordic.calc cos 00000000 precision=0 insize=8", "usage"),
        ("cordic.calc cos 00000000 precision=0 scale=9", "range"),
        ("cordic.calc cos 0g000000", "usage"),
        ("cordic.calc cos - len=2049", "range"),
        ("cordic.calc cos - len=6", "usage"),
        ("cordic.calc cos - len=64 insize=16", "ok"),
        ("cordic.calc cos - len=8 nargs=2 nres=2 variant=dma", "ok"),
        ("cordic.calc cos - len=256", "range"),
        ("cordic.calc cos - len=256 out=crc", "ok"),
    ],
)
def test_fake_argument_order(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


def test_fake_dma_busy_while_another_owner_holds_the_channel():
    terminal, fake, _ = make()
    fake.claim_resource("dma2", 6, ("other", "x"))
    assert reason(terminal, "cordic.calc cos 00000000 variant=dma") == "busy"
    assert reason(terminal, "cordic.calc cos 00000000 variant=it") == "ok"


@pytest.mark.parametrize("family", ["stm32wb55", "stm32wba55"])
def test_unsupported_off_the_g474(family):
    terminal, _, _ = make(family)
    assert reason(terminal, "cordic.calc cos 00000000") == "unsupported"
