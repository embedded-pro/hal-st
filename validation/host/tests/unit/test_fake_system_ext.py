"""The fake `flash`, `hsem`, `bkp` and `lpm` groups (argument order and reasons of validation/firmware/FlashGroup.cpp,
HsemGroup.cpp, BackupRamGroup.cpp and LowPowerGroup.cpp; PROTOCOL.md D.17-D.19), the `wdt.start` refusal while the
coordinated flash borrows the watchdog, and the `groups.system_ext` wrappers and helpers."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from hal_st_validation import patterns
from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups.system_ext import bkp_fill_value, flash_words, sector_addresses, table_sectors

WB55_FIRST = 50
WBA55_FIRST = 24


class Clock:
    def __init__(self) -> None:
        self.now = 100.0

    def __call__(self) -> float:
        return self.now

    def advance(self, seconds: float) -> None:
        self.now += seconds


def make(family="stm32wb55", clock=None):
    # Final lines end with a line break (as the HIL conftest sets it up), so an `EVT` printed right after a final
    # line cannot join it and the prompt.
    fake = (
        FakeFirmware(family=family, style="line")
        if clock is None
        else FakeFirmware(family=family, style="line", clock=clock, sleep=clock.advance)
    )
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    return terminal, fake, Firmware(terminal, WB55_PINS if family == "stm32wb55" else WBA55_PINS)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


def test_table_sectors():
    assert table_sectors(64) == [1] * 48 + [1, 1, 2, 4, 1, 1, 2, 4]
    assert table_sectors(12) == [1] * 4 + [1, 1, 2, 4]
    assert table_sectors(7) == [1] * 7
    assert sum(table_sectors(64)) == 64
    assert sector_addresses([1, 2, 4], 4096) == [0, 4096, 12288, 28672]


def test_flash_words_and_fill_value():
    assert flash_words(0, 8, 8) == range(0, 1)
    assert flash_words(5, 4, 8) == range(0, 2)
    assert flash_words(8187, 32, 16) == range(511, 514)
    assert bkp_fill_value(0, 0) == 0x9E3779B9
    assert bkp_fill_value(0xFFFFFFFF, 1) == (0xFFFFFFFF ^ (2 * 0x9E3779B9)) & 0xFFFFFFFF


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("flash.info", "ok"),
        ("flash.info 1", "usage"),
        ("flash.info variant=fast", "usage"),
        ("flash.info layout=table variant=coord", "ok"),
        ("flash.erase", "usage"),
        ("flash.erase 50", "usage"),
        ("flash.erase x 51", "usage"),
        ("flash.erase 50 51 variant=fast", "usage"),
        ("flash.erase 49 50", "range"),
        ("flash.erase 50 50", "range"),
        ("flash.erase 63 65", "range"),
        ("flash.erase 50 57 layout=table", "range"),
        ("flash.erase 50 51", "ok"),
        ("flash.write 204800 -", "usage"),
        ("flash.write 204800 0011 len=2", "usage"),
        ("flash.write 204800 - seed=1", "usage"),
        ("flash.write 204800 001", "usage"),
        ("flash.write 204800 - len=513", "range"),
        ("flash.write 204799 00", "range"),
        ("flash.write 262143 0011", "range"),
        ("flash.read 262144 1", "range"),
        ("flash.read 0 0", "range"),
        ("flash.read 0 129", "range"),
        ("flash.read 0 4 out=bin", "usage"),
        ("flash.read 0 262144 out=crc", "ok"),
        ("flash.stack", "usage"),
        ("flash.stack running", "usage"),
        ("flash.stack fus layout=uneven", "usage"),
        ("flash.stack fus", "ok"),
    ],
)
def test_flash_errors_wb55(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("flash.info variant=coord", "unsupported"),
        ("flash.erase 24 25 variant=coord", "unsupported"),
        ("flash.erase 23 24 variant=coord", "range"),
        ("flash.read 0 129 variant=coord", "range"),
        ("flash.read 0 4 variant=coord", "unsupported"),
        ("flash.stack starting", "unsupported"),
        ("flash.erase 24 25", "ok"),
    ],
)
def test_flash_errors_wba55(line, expected):
    terminal, _, _ = make("stm32wba55")
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("family", "first", "page", "base"), [("stm32wb55", WB55_FIRST, 4096, 0x08040000), ("stm32wba55", WBA55_FIRST, 8192, 0x08080000)]
)
def test_flash_geometry(family, first, page, base):
    _, _, fw = make(family)
    info = fw.flash.info()
    assert (info.base, info.sectors, info.size, info.first, info.layout) == (base, 64, 64 * page, first, "homogeneous")
    table = fw.flash.info(layout="table")
    assert (table.sectors, table.first) == (56, first)


@pytest.mark.parametrize("variant", ["sync", "async", "coord"])
@pytest.mark.parametrize("layout", ["homogeneous", "table"])
def test_flash_memory_model(variant, layout):
    """Erase gives FF over the sector's pages only; writes land where addressed; a programmed word is refused."""
    terminal, _, fw = make()
    sizes = table_sectors(64) if layout == "table" else [1] * 64
    addresses = sector_addresses(sizes, 4096)
    sector = 50
    start, end = addresses[sector], addresses[sector + 1]
    fw.flash.erase(sector, sector + 2, variant=variant, layout=layout)
    data = patterns.generate(20, "prbs", 3)
    fw.flash.write(start + 3, data, variant=variant, layout=layout)
    fw.flash.write(end, b"\x77", variant=variant, layout=layout)
    assert fw.flash.read(start, 24, variant=variant, layout=layout) == b"\xff" * 3 + data + b"\xff"
    assert reason(terminal, f"flash.write {start + 22} 00 layout={layout}") == "failed"
    fw.flash.write(start + 24, length=300, pattern="inc", seed=9, variant=variant, layout=layout)
    assert fw.flash.read_crc(start + 24, 300, variant=variant, layout=layout) == patterns.crc32(patterns.generate(300, "inc", 9))
    fw.flash.erase(sector, sector + 1, variant=variant, layout=layout)
    assert fw.flash.read_crc(start, end - start) == patterns.crc32(b"\xff" * (end - start))
    assert fw.flash.read(end, 1) == b"\x77"


def test_coord_waits_for_semaphore_7():
    clock = Clock()
    terminal, _, fw = make(clock=clock)
    fw.flash.erase(WB55_FIRST, WB55_FIRST + 1)
    fw.hsem.take(7, procid=1, hold=200)
    pending = fw.flash.begin_write(WB55_FIRST * 4096, b"\x01\x02", variant="coord")
    clock.advance(0.25)
    response = pending.wait()
    assert response.as_int("us") >= 200000
    assert not fw.hsem.status(7).locked
    assert reason(terminal, "hsem.lock 3") == "ok"


def test_coord_held_while_stack_starting():
    """A held write answers `ERR timeout` after 2 s and completes after `fus` with `EVT flash`; meanwhile the
    watchdog and HSEM 0 are borrowed and only synchronous reads and the stack command go through."""
    clock = Clock()
    terminal, fake, fw = make(clock=clock)
    start = WB55_FIRST * 4096
    fw.flash.erase(WB55_FIRST, WB55_FIRST + 1)
    fw.flash.stack("starting")
    assert reason(terminal, "flash.stack starting") == "busy"
    assert reason(terminal, "wdt.start 0 timeout=100") == "busy"
    assert reason(terminal, "wdt.start 0") == "usage"
    assert reason(terminal, "hsem.lock 1") == "busy"
    assert reason(terminal, f"flash.erase {WB55_FIRST} {WB55_FIRST + 1}") == "busy"
    assert reason(terminal, f"flash.erase {WB55_FIRST} {WB55_FIRST + 1} variant=coord layout=table") == "busy"
    pending = fw.flash.begin_write(start, b"\xab", variant="coord")
    clock.advance(2.1)
    response = pending.wait(check=False)
    assert (response.ok, response.reason) == (False, "timeout")
    assert fw.flash.read(start, 1) == b"\xff"
    assert reason(terminal, f"flash.write {start + 8} 01 variant=coord") == "busy"
    fw.flash.stack("fus")
    event = fw.flash.wait_done(timeout=0.5)
    assert event["op"] == "write"
    assert fw.flash.read(start, 1) == b"\xab"
    assert fake.watchdog is None
    assert reason(terminal, "wdt.start 0 timeout=100") == "ok"


def test_coord_timeout_completes_once_semaphore_7_is_released():
    """A coordinated step stuck on HSEM 7 past its `ERR timeout` keeps the group busy and completes, with `EVT flash`,
    once the semaphore is released."""
    clock = Clock()
    terminal, _, fw = make(clock=clock)
    start = WB55_FIRST * 4096
    fw.flash.erase(WB55_FIRST, WB55_FIRST + 1)
    fw.hsem.take(7, procid=2)
    pending = fw.flash.begin_write(start, b"\x01", variant="coord")
    assert reason_after(pending, clock, 2.1) == "timeout"
    assert reason(terminal, f"flash.erase {WB55_FIRST} {WB55_FIRST + 1} variant=async") == "busy"
    assert reason(terminal, f"flash.write {start + 8} zz") == "busy"
    assert fw.flash.read(start, 1) == b"\xff"
    fw.hsem.release(7, procid=2)
    assert fw.flash.wait_done(timeout=0.5)["op"] == "write"
    assert fw.flash.read(start, 1) == b"\x01"


def reason_after(pending, clock, seconds):
    clock.advance(seconds)
    response = pending.wait(check=False)
    return "ok" if response.ok else response.reason


def test_hsem_state():
    clock = Clock()
    terminal, _, fw = make(clock=clock)
    fw.hsem.take(4, procid=9)
    status = fw.hsem.status(4)
    assert (status.locked, status.core, status.procid) == (True, 4, 9)
    assert fw.hsem.mine(4)
    assert reason(terminal, "hsem.take 4 procid=8") == "busy"
    assert reason(terminal, "hsem.take 4 procid=9") == "ok"
    assert reason(terminal, "hsem.lock 4") == "busy"
    fw.hsem.release(4, procid=9)
    assert not fw.hsem.mine(4)
    fw.hsem.take(5, procid=1, hold=100)
    assert reason(terminal, "hsem.take 6 procid=1 hold=100") == "busy"
    clock.advance(0.2)
    assert not fw.hsem.status(5).locked
    assert fw.hsem.lock(5, hold=1500) == 1500
    assert fw.hsem.lock(5) == 0


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("hsem.take 1", "usage"),
        ("hsem.take procid=1", "usage"),
        ("hsem.take 32 procid=1", "range"),
        ("hsem.take 1 procid=0", "range"),
        ("hsem.take 1 procid=1 hold=60001", "range"),
        ("hsem.release 1", "usage"),
        ("hsem.status 32", "range"),
        ("hsem.lock 1 hold=1", "range"),
        ("hsem.lock 1 hold=65537", "range"),
        ("hsem.lock 1 procid=1", "usage"),
        ("hsem.mine", "usage"),
    ],
)
def test_hsem_errors(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


def test_hsem_lock_needs_scaffold_timer():
    terminal, fake, _ = make()
    fake.timer_owners[17] = ("tim", "17")
    assert reason(terminal, "hsem.lock 1 hold=100") == "busy"
    assert reason(terminal, "hsem.lock 1") == "ok"


@pytest.mark.parametrize(("family", "words"), [("stm32wb55", 20), ("stm32wba55", 16)])
def test_backup_ram(family, words):
    terminal, _, fw = make(family)
    assert fw.bkp.info() == words
    fw.bkp.write(words - 1, 0x89ABCDEF)
    assert fw.bkp.read(words - 1) == 0x89ABCDEF
    assert terminal.command(f"bkp.read {words - 1}")["value"] == "89abcdef"
    fw.bkp.fill(7)
    assert fw.bkp.check(7) == 0
    assert fw.bkp.check(8) == words
    fw.system.reset()
    assert fw.bkp.check(7) == 0
    for line, expected in [(f"bkp.read {words}", "range"), ("bkp.write 0 0x100000000", "usage"), ("bkp.info 0", "usage")]:
        assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("lpm.enter", "usage"),
        ("lpm.enter nap", "usage"),
        ("lpm.enter sleep extra", "usage"),
        ("lpm.enter sleep edge=both", "usage"),
        ("lpm.enter sleep timeout=0", "range"),
        ("lpm.enter sleep timeout=10001", "range"),
        ("lpm.enter sleep wake=PA16", "pin"),
        ("lpm.enter sleep wake=gpio0 marker=gpio0", "usage"),
        ("lpm.enter sleep wake=PC7", "pin"),
        ("lpm.enter deep marker=PE0", "pin"),
        ("lpm.enter deep", "ok"),
    ],
)
def test_lpm_errors_wb55(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


def test_lpm_wakes_and_checks_resources():
    terminal, fake, fw = make("stm32wba55")
    wake = fw.lpm.enter("sleep")
    assert (wake.woke, wake.restored, wake.sleeps) == ("exti", 0, 1)
    fw.gpio.cfg("tim1bkin", "out")
    assert reason(terminal, "lpm.enter sleep") == "busy"
    fw.gpio.release("tim1bkin")
    fake.timer_owners[17] = ("tim", "17")
    assert reason(terminal, "lpm.enter sleep") == "busy"


def test_groups_track_their_undo():
    _, _, fw = make()
    fw.flash.stack("starting")
    fw.hsem.take(9, procid=4)
    fw.hsem.take(10, procid=4, hold=1000)
    assert ("flash", "stack") in fw.open_instances
    assert ("hsem", 9, 4) in fw.open_instances
    assert ("hsem", 10, 4) not in fw.open_instances
    assert fw.close_all() == []
    assert not fw.hsem.status(9).locked
    fw.flash.stack("starting", layout="table")
    with pytest.raises(FirmwareError) as error:
        fw.flash.stack("starting")
    assert error.value.reason == "busy"
    fw.flash.stack("stopped")
    assert ("flash", "stack") not in fw.open_instances
