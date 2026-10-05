"""Random number generator through the `rng` group: `hal::SynchronousRandomDataGeneratorStm` (`variant=sync`),
`hal::RandomDataGeneratorStm` (`variant=async`, interrupt driven) and on STM32WB55
`hal::SynchronousSynchronizedRandomDataGeneratorStm` (`variant=hsem`: HSEM semaphore 0 around the read, HSI48 managed
by its `Hsi48Enabler` unless semaphore 5 is locked).

Every variant returns exactly the requested bytes, two reads differ, and 64 KiB pass the monobit, byte chi-square and
runs tests of `rngstats`. On STM32WB55 the synchronized variant keeps HSI48 (the RNG kernel clock) running when it was
on and switches it off again when it was off (B.9b), takes the locked branch with `lock5=1`, and leaves HSEM semaphores
0 and 5 free (B.9). No wiring.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import rngstats


@pytest.fixture
def rng_cfg(board_cfg):
    return board_cfg.param("rng")


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


def assert_semaphores_free(fw, rng_cfg):
    """`hsem.status` reads the semaphores without taking them (the hsem group of area-system, when built in)."""
    if not hasattr(fw, "hsem"):
        return
    for semaphore in rng_cfg["semaphores"]:
        response = fw.command("hsem.status", semaphore)
        assert response.as_int("locked") == 0, f"semaphore {semaphore}: {response.raw}"


def needs_clock_group(fw):
    if not hasattr(fw, "clock"):
        pytest.skip("needs the clock group (clock.hsi48, clock.info)")


@pytest.mark.board_params("variant", "rng.variants")
@pytest.mark.board_params("length", "rng.lengths")
def test_read_length(fw, variant, length):
    read = fw.rng.read(length, variant=variant)
    assert len(read.data) == length, read.raw
    assert (read.hsi48 is not None) == (variant == "hsem"), read.raw


@pytest.mark.board_params("variant", "rng.variants")
def test_reads_differ(fw, variant):
    first = fw.rng.read(16, variant=variant).data
    second = fw.rng.read(16, variant=variant).data
    assert first != second
    assert first not in (bytes(16), b"\xff" * 16)


@pytest.mark.board_params("variant", "rng.variants")
def test_statistics(fw, rng_cfg, variant):
    """64 KiB generated in 256-byte chunks pass the monobit, byte chi-square and runs bounds of `rngstats`."""
    settings = rng_cfg["stats"]
    stats = fw.rng.stats(settings["length"], variant=variant, cmd_timeout=settings["timeout_s"])
    assert stats.n == settings["length"], stats.raw
    assert rngstats.failures(stats.n, stats.ones, stats.runs, stats.chisq_x1000) == [], stats.raw
    assert stats.us > 0, stats.raw


@pytest.mark.board_params("variant", "rng.variants")
def test_statistics_differ(fw, variant):
    first = fw.rng.stats(256, variant=variant)
    second = fw.rng.stats(256, variant=variant)
    assert first.n == second.n == 256
    assert first.crc != second.crc


def test_variants_take_turns(fw, rng_cfg):
    """Each variant enables and disables the RNG on its own; one after the other they all still deliver."""
    variants = list(rng_cfg["variants"])
    for variant in variants + variants[::-1]:
        assert len(fw.rng.read(32, variant=variant).data) == 32, variant


@pytest.mark.family("stm32wb55")
def test_hsem_keeps_hsi48(fw, rng_cfg):
    """With HSI48 running (the default clock's RNG kernel clock) the synchronized read leaves it on, so the other
    variants still have their clock afterwards (B.9b)."""
    read = fw.rng.read(16, variant="hsem")
    assert read.hsi48 == 1, read.raw
    assert len(fw.rng.read(16, variant="sync").data) == 16
    assert len(fw.rng.read(16, variant="async").data) == 16
    if hasattr(fw, "clock"):
        assert fw.clock.info().flags["hsi48"] == 1
    assert_semaphores_free(fw, rng_cfg)


@pytest.mark.family("stm32wb55")
def test_hsem_restores_hsi48_off(fw, rng_cfg):
    """With HSI48 off the synchronized read starts it for the read and switches it off again afterwards."""
    needs_clock_group(fw)
    fw.clock.hsi48(False)
    try:
        read = fw.rng.read(16, variant="hsem")
        assert read.hsi48 == 0, read.raw
        assert len(read.data) == 16
        assert fw.clock.info().flags["hsi48"] == 0
    finally:
        fw.clock.hsi48(True)
    assert fw.rng.read(16, variant="hsem").hsi48 == 1
    assert_semaphores_free(fw, rng_cfg)


@pytest.mark.family("stm32wb55")
def test_hsem_clock_locked(fw, rng_cfg):
    """`lock5=1` holds semaphore 5 (clock configuration) around the read: `Hsi48Enabler` takes its locked branch and
    leaves HSI48 alone; both semaphores are free afterwards."""
    first = fw.rng.read(16, variant="hsem", lock5=True)
    second = fw.rng.read(16, variant="hsem", lock5=True)
    assert first.hsi48 == second.hsi48 == 1, (first.raw, second.raw)
    assert first.data != second.data
    assert_semaphores_free(fw, rng_cfg)


@pytest.mark.family("stm32wb55")
@pytest.mark.parametrize("line", ["rng.read 4 lock5=1", "rng.read 4 variant=async lock5=1"])
def test_clock_lock_needs_hsem_variant(fw, line):
    """`lock5` only applies to the synchronized variant."""
    expect_reason(fw, line, "usage")


@pytest.mark.family("stm32wb55")
def test_clock_locked_needs_hsi48(fw, rng_cfg):
    """With semaphore 5 locked the driver asserts that HSI48 runs; the firmware refuses that read with HSI48 off."""
    needs_clock_group(fw)
    fw.clock.hsi48(False)
    try:
        expect_reason(fw, "rng.read 4 variant=hsem lock5=1", "failed")
    finally:
        fw.clock.hsi48(True)
    assert_semaphores_free(fw, rng_cfg)


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("rng.read", "usage"),
        ("rng.read 4 5", "usage"),
        ("rng.read x", "usage"),
        ("rng.read 4 speed=1", "usage"),
        ("rng.read 0", "range"),
        ("rng.read 129", "range"),
        ("rng.read 4 variant=dma", "usage"),
        ("rng.read 4 lock5=2", "range"),
        ("rng.stats", "usage"),
        ("rng.stats 16 lock5=1", "usage"),
        ("rng.stats 15", "range"),
        ("rng.stats 65537", "range"),
        ("rng.stats 16 variant=x", "usage"),
    ],
)
def test_errors(fw, line, reason):
    expect_reason(fw, line, reason)


@pytest.mark.board_params("line", "rng.unsupported")
def test_unsupported(fw, line):
    expect_reason(fw, line, "unsupported")
