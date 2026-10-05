"""Backup registers through the `bkp` group: `hal::BackupRamStm` used as `hal::BackupRam<volatile uint32_t>` (B.8:
public inheritance; on STM32WBA55 the driver also clocks the TAMP registers and opens the backup domain). STM32WB55:
RTC BKP0R-BKP19R; STM32WBA55: TAMP BKP0R-BKP15R. The words survive a system reset. No wiring.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation.groups.system_ext import bkp_fill_value


@pytest.fixture
def bkp_cfg(board_cfg):
    return board_cfg.param("bkp")


def test_words(fw, bkp_cfg):
    assert fw.bkp.info() == bkp_cfg["words"]


@pytest.mark.parametrize("pattern", [0xA5A5A5A5, 0x5A5A5A5A, 0x00000000, 0xFFFFFFFF])
def test_write_read_every_word(fw, bkp_cfg, pattern):
    """Each word holds its own value: written one by one, all read back, none disturbed by its neighbours."""
    words = bkp_cfg["words"]
    values = [(pattern ^ (index * 0x01010101)) & 0xFFFFFFFF for index in range(words)]
    for index, value in enumerate(values):
        fw.bkp.write(index, value)
    assert [fw.bkp.read(index) for index in range(words)] == values


@pytest.mark.parametrize("seed", [1, 0xDEADBEEF])
def test_fill_and_check(fw, bkp_cfg, seed):
    fw.bkp.fill(seed)
    assert fw.bkp.check(seed) == 0
    assert fw.bkp.check(seed ^ 1) == bkp_cfg["words"]
    assert fw.bkp.read(0) == bkp_fill_value(seed, 0)


@pytest.mark.resets_board
def test_retained_across_reset(fw, board_cfg, bkp_cfg):
    """The backup domain keeps the words through a system reset."""
    seed = 0x13579BDF
    fw.bkp.fill(seed)
    fw.system.reset(timeout=board_cfg.param("system.boot_timeout", 5.0))
    assert fw.bkp.check(seed) == 0
    assert fw.bkp.read(bkp_cfg["words"] - 1) == bkp_fill_value(seed, bkp_cfg["words"] - 1)


def test_errors(fw, bkp_cfg):
    words = bkp_cfg["words"]
    for line, reason in [
        (f"bkp.read {words}", "range"),
        (f"bkp.write {words} 1", "range"),
        ("bkp.write 0 0x100000000", "usage"),
        ("bkp.write 0", "usage"),
        ("bkp.read", "usage"),
        ("bkp.info 1", "usage"),
        ("bkp.fill", "usage"),
        ("bkp.check x", "usage"),
    ]:
        with pytest.raises(FirmwareError) as error:
            fw.terminal.command(line)
        assert error.value.reason == reason, line
