"""`fmac_ref` (the bit-exact FMAC model) with the answers of its calibration vectors under the default reading and
under each alternative a bench run could show, the fake `fmac` group (argument order and reasons of
validation/firmware/FmacGroup.cpp) and the `groups.fmac` wrapper."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareTerminal

from hal_st_validation import fmac_ref, patterns
from hal_st_validation.fake_firmware import G474_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware

VECTORS = {vector.name: vector for vector in fmac_ref.CALIBRATION}

# Default reading: products truncated to q.22, a 26-bit wrapping accumulator, feedback added.
EXPECTED = {
    "order": ([0, 16383, 0, 4096], False),
    "truncation": ([0, -1, 2, -3], False),
    "products": ([0], False),
    "width": ([32767], False),
    "wrap": ([-32768], True),
    "gain": ([1024, 1028, -1028, -4], False),
    "overflow": ([32766, -4, -1, 2], False),
    "product": ([32767], False),
    "input history": ([12287, 2048, 0], False),
    "feedback": ([8192, 4096, 2048, 1024], False),
    "output history": ([20479, 18431, 14335], False),
}

# What the vectors answer under the alternatives: each one differs from the default in its own vector.
ALTERNATIVES = {
    "exact products": (fmac_ref.Arithmetic(product="exact"), "products", [63]),
    "q3.23 accumulator": (fmac_ref.Arithmetic(acc_frac=23), "width", [-32768]),
    "feedback subtracted": (fmac_ref.Arithmetic(feedback=-1), "feedback", [8192, -4096, 2048, -1024]),
}


def make(family="stm32g474"):
    fake = FakeFirmware(family=family)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    return terminal, fake, Firmware(terminal, G474_PINS)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


def test_every_vector_has_an_expectation():
    assert set(VECTORS) == set(EXPECTED)


@pytest.mark.parametrize("name", list(EXPECTED))
def test_calibration_vectors(name):
    assert VECTORS[name].evaluate() == EXPECTED[name]


def test_sat_on_output_overflow_is_the_alternative_the_bench_ruled_out():
    output_sat = fmac_ref.Arithmetic(sat="output")
    assert [VECTORS[name].evaluate(output_sat)[1] for name in ("width", "gain", "overflow", "product")] == [True] * 4


@pytest.mark.parametrize("alternative", list(ALTERNATIVES))
def test_alternatives_change_their_vector(alternative):
    arithmetic, name, answer = ALTERNATIVES[alternative]
    assert VECTORS[name].evaluate(arithmetic)[0] == answer != EXPECTED[name][0]


def test_rounding_would_change_the_truncation_vector():
    """Rounding the output to nearest would give 1 for 0x7FFF * 1 (just below one LSB) where truncation gives 0."""
    assert round(0x7FFF * 1 / (1 << 15)) == 1 != EXPECTED["truncation"][0][0]


def test_reversed_coefficients_would_change_the_order_vector():
    reversed_order = fmac_ref.fir(tuple(reversed(VECTORS["order"].b)), VECTORS["order"].x)[0]
    assert reversed_order == [16383, 0, 4096, 0] != EXPECTED["order"][0]


def test_reversed_history_would_change_the_history_vectors():
    vector = VECTORS["input history"]
    assert fmac_ref.fir(vector.b, vector.x, history=tuple(reversed(vector.xh)))[0][0] == 18431


def test_fir_is_an_iir_without_feedback():
    b = fmac_ref.generate_samples(8, 3, 0x1000)
    x = fmac_ref.generate_samples(40, 4)
    assert fmac_ref.fir(b, x, 2, True) == fmac_ref.iir(b, (), x, 2, True)


def test_history_is_padded_on_the_old_side():
    assert fmac_ref.fir((0, 0, 0x4000), (0, 0), history=(0x2000,))[0] == [0, 0x1000]
    assert fmac_ref.fir((0, 0, 0x4000), (0, 0), history=(0x7FFF, 0x0100, 0x2000))[0] == [0x80, 0x1000]


def test_pack_round_trip():
    samples = [0, 1, -1, 0x7FFF, -0x8000]
    assert fmac_ref.unpack(fmac_ref.pack(samples)) == samples
    assert fmac_ref.pack([0x40]) == bytes.fromhex("4000")


def test_generated_samples_stay_within_the_limit():
    samples = fmac_ref.generate_samples(500, 9, 0x0800)
    assert max(samples) <= 0x0800 and min(samples) >= -0x0801
    assert len(set(samples)) > 100


def test_fake_filters_with_the_stores():
    _, _, fw = make()
    fw.fmac.load_all("b", VECTORS["order"].b)
    reply = fw.fmac.fir(VECTORS["order"].x, 2)
    assert (reply.samples, reply.sat) == EXPECTED["order"]
    assert fw.fmac.load("x", 120, [1, 2, 3]) == 123


def test_fake_crc_matches_the_model():
    _, _, fw = make()
    b = fmac_ref.generate_samples(31, 38, 0x1000)
    fw.fmac.load_all("b", b)
    x = fmac_ref.unpack(patterns.generate(2048, "prbs", 5))
    expected, saturated = fmac_ref.fir(b, x, 1)
    for variant in ("poll", "it", "dma"):
        reply = fw.fmac.fir(None, 31, gain=1, len=2048, pattern="prbs", seed=5, variant=variant, out="crc")
        assert (reply.length, reply.crc, reply.sat) == (4096 // 2, fmac_ref.crc(expected), saturated)


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("fmac.load b 0 0040", "ok"),
        ("fmac.load b x 0040", "usage"),
        ("fmac.load z 999 0040", "usage"),
        ("fmac.load b 999 zz", "range"),
        ("fmac.load b 0 0040 clear=1", "ok"),
        ("fmac.fir 0040 taps=2 gain=9 variant=x", "range"),
        ("fmac.fir 0040 taps=2 variant=x out=y", "usage"),
        ("fmac.fir 00 taps=2 out=y", "usage"),
        ("fmac.fir 00 taps=2", "usage"),
        ("fmac.fir - taps=2 len=200", "range"),
        ("fmac.fir - taps=2 len=200 out=crc", "ok"),
        ("fmac.iir 0040 p=99 q=99 yhist=99", "range"),
        ("fmac.iir 0040 p=2 q=1 yhist=99 gain=99", "range"),
        ("fmac.iir 0040 p=2 q=1 variant=dma", "ok"),
    ],
)
def test_fake_argument_order(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


def test_fake_dma_busy_while_another_owner_holds_the_channel():
    terminal, fake, _ = make()
    fake.claim_resource("dma2", 8, ("other", "x"))
    assert reason(terminal, "fmac.fir 0040 taps=2 variant=dma") == "busy"
    assert reason(terminal, "fmac.fir 0040 taps=2 variant=it") == "ok"


def test_fake_stores_are_lost_on_reset():
    _, fake, fw = make()
    fw.fmac.load("b", 0, [0x4000])
    fake.boot()
    assert fw.fmac.fir([0x4000], 2).samples == [0]


@pytest.mark.parametrize("family", ["stm32wb55", "stm32wba55"])
def test_unsupported_off_the_g474(family):
    terminal, _, _ = make(family)
    for line in ("fmac.load b 0 0040", "fmac.fir 0040 taps=2", "fmac.iir 0040 p=2 q=1"):
        assert reason(terminal, line) == "unsupported"
