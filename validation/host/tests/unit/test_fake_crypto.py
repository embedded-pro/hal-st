"""The fake `rng`, `aes` and `pka` groups (argument order and reasons of validation/firmware/RngGroup.cpp,
AesGroup.cpp and PkaGroup.cpp; PROTOCOL.md D.16) and the `groups.crypto` wrappers."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from hal_st_validation import crypto_ref, rngstats
from hal_st_validation.crypto_ref import P256
from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware

KEY = "000102030405060708090a0b0c0d0e0f"
DATA = "00112233445566778899aabbccddeeff"


def make(family="stm32wb55"):
    fake = FakeFirmware(family=family)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    return terminal, fake, Firmware(terminal, WB55_PINS if family == "stm32wb55" else WBA55_PINS)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("rng.read", "usage"),
        ("rng.read 4 5", "usage"),
        ("rng.read 4 speed=1", "usage"),
        ("rng.read x", "usage"),
        ("rng.read 0", "range"),
        ("rng.read 129", "range"),
        ("rng.read 0 variant=x", "range"),
        ("rng.read 4 variant=x", "usage"),
        ("rng.read 4 lock5=2", "range"),
        ("rng.read 4 lock5=1", "usage"),
        ("rng.read 4 variant=sync lock5=1", "usage"),
        ("rng.read 128 variant=async", "ok"),
        ("rng.read 1 variant=hsem lock5=1", "ok"),
        ("rng.stats", "usage"),
        ("rng.stats 16 lock5=0", "usage"),
        ("rng.stats 15", "range"),
        ("rng.stats 65537", "range"),
        ("rng.stats 16 variant=x", "usage"),
        ("rng.stats 16 variant=hsem", "ok"),
    ],
)
def test_rng_errors_wb55(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("rng.read 4 variant=hsem", "unsupported"),
        ("rng.read 4 variant=hsem lock5=1", "unsupported"),
        ("rng.read 4 lock5=1", "unsupported"),
        ("rng.read 4 lock5=0", "ok"),
        ("rng.read 4 lock5=2", "range"),
        ("rng.read 0 variant=hsem", "range"),
        ("rng.stats 16 variant=hsem", "unsupported"),
        ("rng.read 4 variant=async", "ok"),
    ],
)
def test_rng_errors_wba55(line, expected):
    terminal, _, _ = make("stm32wba55")
    assert reason(terminal, line) == expected


def test_rng_reads_differ_and_have_the_length():
    _, _, fw = make()
    first = fw.rng.read(16)
    second = fw.rng.read(16, variant="async")
    assert len(first.data) == len(second.data) == 16 and first.data != second.data
    assert first.hsi48 is None
    assert fw.rng.read(1, variant="hsem").hsi48 == 1


def test_rng_hsem_reports_hsi48_of_the_clock_group():
    _, fake, fw = make()
    fw.clock.hsi48(False)
    assert fw.rng.read(4, variant="hsem").hsi48 == 0
    with pytest.raises(FirmwareError) as error:
        fw.rng.read(4, variant="hsem", lock5=True)
    assert error.value.reason == "failed"
    fw.clock.hsi48(True)
    assert fw.rng.read(4, variant="hsem", lock5=True).hsi48 == 1
    assert fake.group("rng") is not None


def test_rng_stats_are_consistent_and_random():
    _, _, fw = make()
    stats = fw.rng.stats(65536)
    assert stats.n == 65536 and stats.us > 0
    assert rngstats.failures(stats.n, stats.ones, stats.runs, stats.chisq_x1000) == []
    assert fw.rng.stats(256).crc != fw.rng.stats(256).crc


def test_rng_restarts_at_boot():
    _, fake, fw = make()
    first = fw.rng.read(8).data
    fake.boot()
    assert fw.rng.read(8).data == first


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("aes.enc", "usage"),
        (f"aes.enc {KEY}", "usage"),
        (f"aes.enc {KEY} {DATA} {DATA}", "usage"),
        (f"aes.enc {KEY} {DATA} mode=ecb", "usage"),
        (f"aes.enc {KEY[:-2]} {DATA}", "usage"),
        (f"aes.enc {KEY}{KEY} {DATA}", "usage"),
        (f"aes.enc - {DATA}", "usage"),
        (f"aes.enc {KEY} -", "usage"),
        (f"aes.enc {KEY} {DATA[:-1]}", "usage"),
        (f"aes.enc {KEY} {DATA[:-2]}", "usage"),
        (f"aes.enc {KEY} {DATA[:-2]}zz", "usage"),
        (f"aes.enc {KEY} {DATA * 6}", "usage"),
        (f"aes.enc {KEY} {DATA * 5}", "ok"),
        (f"aes.enc {KEY} {DATA} swap=word", "usage"),
        (f"aes.dec {KEY} {DATA} swap=none", "ok"),
    ],
)
def test_aes_errors(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize("swap", [None, *crypto_ref.SWAPS])
def test_aes_follows_the_model(swap):
    _, _, fw = make("stm32wba55")
    vector = crypto_ref.AES_SP800_38A_F11
    ciphertext = fw.aes.enc(vector.key, vector.plaintext, swap=swap)
    assert ciphertext == crypto_ref.aes_stm(vector.key, vector.plaintext, swap or "byte")
    assert fw.aes.dec(vector.key, ciphertext, swap=swap) == vector.plaintext
    if swap in (None, "byte"):
        assert ciphertext == vector.ciphertext


OPERAND_33 = "01" * 33


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("pka.mul 1", "usage"),
        ("pka.mul z=01", "usage"),
        ("pka.mul x=01", "usage"),
        ("pka.mul y=01", "usage"),
        ("pka.mul k=1", "usage"),
        ("pka.mul k=-", "usage"),
        ("pka.mul k=zz", "usage"),
        (f"pka.mul k={OPERAND_33} x=01", "usage"),
        (f"pka.mul k={OPERAND_33}", "range"),
        (f"pka.mul k=01 x=01 y={OPERAND_33}", "range"),
        ("pka.mul k=02", "ok"),
        ("pka.check", "usage"),
        ("pka.check x=01", "usage"),
        (f"pka.check x={OPERAND_33} y=0", "usage"),
        (f"pka.check x={OPERAND_33} y=01", "range"),
        ("pka.check x=01 y=02", "ok"),
        ("pka.cmp", "usage"),
        ("pka.cmp a=00000001", "usage"),
        ("pka.cmp a=00000001 b=0000000001", "usage"),
        ("pka.cmp a=0001 b=000002", "usage"),
        ("pka.cmp a=000001 b=000002", "range"),
        ("pka.cmp a=00000001 b=00000002 c=1", "usage"),
        (f"pka.cmp a={'01' * 60} b={'02' * 60}", "ok"),
    ],
)
def test_pka_errors(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


def test_pka_results():
    _, _, fw = make()
    default = fw.pka.mul()
    assert (crypto_ref.from_bytes(default.x), crypto_ref.from_bytes(default.y)) == P256.g
    product = fw.pka.mul(k=b"\x02")
    assert (crypto_ref.from_bytes(product.x), crypto_ref.from_bytes(product.y)) == crypto_ref.scalar_multiply(P256, 2)
    assert len(product.x) == 32 and product.us > 0
    assert fw.pka.check(crypto_ref.to_bytes(P256.gx), crypto_ref.to_bytes(P256.gy))
    assert not fw.pka.check(b"\x01", b"\x02")
    assert fw.pka.cmp(b"\x00\x00\x00\x01", b"\x00\x00\x00\x02") == "lt"
    assert fw.pka.cmp(b"\x00\x00\x01\x00", b"\x00\x00\x00\x02") == "gt"
    assert fw.pka.cmp(bytes(8), bytes(8)) == "eq"


def test_crypto_groups_are_attached():
    _, _, fw = make()
    assert {"rng", "aes", "pka"} <= {name for name in vars(fw)}
