"""Backup registers through the `bkp` group: `hal::BackupRamStm` used as `hal::BackupRam<volatile uint32_t>` (B.8:
public inheritance; on STM32WBA55 the driver also clocks the TAMP registers and opens the backup domain). STM32WB55:
RTC BKP0R-BKP19R; STM32WBA55: TAMP BKP0R-BKP15R. The words survive a system reset. No wiring.

Scenarios: features/backup_ram.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import parsers, scenario, then, when

from hal_st_validation.groups.system_ext import bkp_fill_value


@pytest.fixture
def bkp_cfg(board_cfg):
    return board_cfg.param("bkp")


@scenario("backup_ram.feature", "The driver has the backup words of the board file")
def test_words():
    pass


@pytest.mark.parametrize("pattern", [0xA5A5A5A5, 0x5A5A5A5A, 0x00000000, 0xFFFFFFFF])
@scenario("backup_ram.feature", "Each word holds its own value")
def test_write_read_every_word(pattern):
    pass


@pytest.mark.parametrize("seed", [1, 0xDEADBEEF])
@scenario("backup_ram.feature", "Fill and check agree on the fill values of the seed")
def test_fill_and_check(seed):
    pass


@scenario("backup_ram.feature", "The words survive a system reset")
def test_retained_across_reset():
    pass


@scenario("backup_ram.feature", "Malformed and out-of-range commands are refused")
def test_errors():
    pass


@when("each word is written in turn with the pattern XORed with its index times 0x01010101", target_fixture="values")
def write_every_word(fw, bkp_cfg, pattern):
    words = bkp_cfg["words"]
    values = [(pattern ^ (index * 0x01010101)) & 0xFFFFFFFF for index in range(words)]
    for index, value in enumerate(values):
        fw.bkp.write(index, value)
    return values


@when("the words are filled from the seed")
def fill(fw, seed):
    fw.bkp.fill(seed)


@when(parsers.parse("the words are filled from the seed {retained:x}"), target_fixture="retained_seed")
def fill_retained(fw, retained):
    fw.bkp.fill(retained)
    return retained


@when("the board resets")
def reset_board(fw, board_cfg):
    fw.system.reset(timeout=board_cfg.param("system.boot_timeout", 5.0))


@then("the driver reports the number of words of the board file")
def words(fw, bkp_cfg):
    assert fw.bkp.info() == bkp_cfg["words"]


@then("all the words read back the values written")
def read_every_word(fw, bkp_cfg, values):
    words = bkp_cfg["words"]
    assert [fw.bkp.read(index) for index in range(words)] == values


@then("checking against the seed finds no word that differs")
def check_seed(fw, seed):
    assert fw.bkp.check(seed) == 0


@then("checking against the seed with bit 0 flipped finds every word different")
def check_other_seed(fw, bkp_cfg, seed):
    assert fw.bkp.check(seed ^ 1) == bkp_cfg["words"]


@then("word 0 reads the fill value of the seed")
def first_word(fw, seed):
    assert fw.bkp.read(0) == bkp_fill_value(seed, 0)


@then("checking against that seed finds no word that differs")
def check_retained_seed(fw, retained_seed):
    assert fw.bkp.check(retained_seed) == 0


@then("the last word reads the fill value of that seed")
def last_word(fw, bkp_cfg, retained_seed):
    assert fw.bkp.read(bkp_cfg["words"] - 1) == bkp_fill_value(retained_seed, bkp_cfg["words"] - 1)


@then("every malformed or out-of-range bkp command line fails with its reason")
def errors(fw, bkp_cfg):
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
