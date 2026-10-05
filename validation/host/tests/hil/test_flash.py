"""Internal flash through the `flash` group, over the scratch region of the board (absolute pages 64-127):
`hal::FlashHomogeneousInternalStm` / `hal::FlashInternalStm` (`variant=async`), their synchronous twins
(`variant=sync`) and on STM32WB55 `hal::FlashCoordinatedWithWirelessStack` (`variant=coord`) over the async driver.

`layout=homogeneous` has one sector per page; `layout=table` has single pages first and the page pattern 1, 1, 2, 4
twice at the end (`groups.system_ext.table_sectors`). The firmware accepts erases and writes only from the sector
`first` on: an unfixed erase (B.5) takes the sector index for the absolute page, which from there on cannot reach the
running image. Every write targets flash words (8 bytes on STM32WB55, 16 on STM32WBA55) no earlier write programmed
since the last erase; programming one twice would fail the driver's assertion, so the firmware answers `ERR failed`.

The coordinated driver waits for HSEM 7 (CPU2 holds it while it needs the flash), holds its steps while the wireless
stack is starting (`flash.stack starting` until `fus`), refreshes the watchdog it shares with `wdt` and excludes
`hsem.lock`. No wiring.
"""

from __future__ import annotations

from dataclasses import dataclass

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import patterns
from hal_st_validation.firmware import settle
from hal_st_validation.groups.system_ext import FlashInfo, flash_words, sector_addresses, table_sectors


@pytest.fixture
def flash_cfg(board_cfg):
    return board_cfg.param("flash")


@dataclass(frozen=True)
class Geometry:
    info: FlashInfo
    layout: str
    page: int
    word: int
    pages: list[int]
    addresses: list[int]

    @classmethod
    def read(cls, fw, flash_cfg, layout: str) -> Geometry:
        info = fw.flash.info(layout=layout)
        page = flash_cfg["page"]
        count = info.size // page
        pages = table_sectors(count) if layout == "table" else [1] * count
        return cls(info, layout, page, flash_cfg["word"], pages, sector_addresses(pages, page))

    def size(self, sector: int) -> int:
        return self.pages[sector] * self.page

    def pick(self, pages: int, following: int = 1) -> int:
        """The first accepted sector of `pages` pages followed by `following` accepted sectors."""
        for sector in range(self.info.first, len(self.pages) - following):
            if self.pages[sector] == pages:
                return sector
        pytest.skip(f"no accepted {pages}-page sector in layout {self.layout} (first={self.info.first})")


def erased(length: int) -> int:
    return patterns.crc32(b"\xff" * length)


def expect_reason(fw, line: str, reason: str) -> None:
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


@pytest.mark.board_params("layout", "flash.layouts")
def test_geometry(fw, flash_cfg, layout):
    """The region, its sectors and the first accepted sector agree with the board file and the layout rule; every
    variant reports the same geometry."""
    geometry = Geometry.read(fw, flash_cfg, layout)
    info = geometry.info
    assert info.base == flash_cfg["base"], info.raw
    assert info.size == flash_cfg["pages"] * flash_cfg["page"], info.raw
    assert info.sectors == len(geometry.pages), info.raw
    assert info.layout == layout
    assert info.image <= flash_cfg["first_page"], f"the image reaches the scratch region: {info.raw}"
    assert info.first == min(info.image, info.sectors), info.raw
    assert info.first < info.sectors, f"no sector left to test: {info.raw}"
    for variant in flash_cfg["variants"]:
        other = fw.flash.info(variant=variant, layout=layout)
        assert (other.base, other.sectors, other.size, other.first, other.image) == (
            info.base,
            info.sectors,
            info.size,
            info.first,
            info.image,
        )


@pytest.mark.board_params("variant", "flash.variants")
@pytest.mark.board_params("layout", "flash.layouts")
def test_erase_hits_region_page(fw, flash_cfg, variant, layout):
    """Erasing sector r erases exactly its pages of the region (B.5): a marker in r disappears, the markers in the
    next sector and the content of the previous one stay. The table layout erases a 2- and a 4-page sector."""
    geometry = Geometry.read(fw, flash_cfg, layout)
    for pages in (2, 4) if layout == "table" else (1,):
        sector = geometry.pick(pages)
        start, following = geometry.addresses[sector], geometry.addresses[sector + 1]
        marker = patterns.generate(geometry.word, "prbs", sector + 1)
        fw.flash.erase(sector, sector + 2, variant=variant, layout=layout)
        assert fw.flash.read_crc(start, geometry.size(sector) + geometry.size(sector + 1)) == erased(
            geometry.size(sector) + geometry.size(sector + 1)
        )
        previous = None
        if sector - 1 >= geometry.info.first:
            previous = fw.flash.read_crc(geometry.addresses[sector - 1], geometry.size(sector - 1))
        fw.flash.write(start, marker, variant=variant, layout=layout)
        fw.flash.write(start + geometry.size(sector) - geometry.word, marker, variant=variant, layout=layout)
        fw.flash.write(following, marker, variant=variant, layout=layout)

        fw.flash.erase(sector, sector + 1, variant=variant, layout=layout)

        assert fw.flash.read_crc(start, geometry.size(sector)) == erased(geometry.size(sector)), f"sector {sector} ({pages} pages)"
        assert fw.flash.read(following, len(marker)) == marker, f"sector {sector + 1} after erasing {sector}"
        if previous is not None:
            assert fw.flash.read_crc(geometry.addresses[sector - 1], geometry.size(sector - 1)) == previous


@pytest.mark.board_params("variant", "flash.variants")
@pytest.mark.board_params("layout", "flash.layouts")
def test_write_shapes(fw, flash_cfg, variant, layout):
    """Aligned, unaligned, odd-length, page-crossing and long writes on distinct flash words read back exactly, the
    bytes around them stay erased, and every variant reads the same data."""
    geometry = Geometry.read(fw, flash_cfg, layout)
    if layout == "table":
        first = geometry.pick(2, following=0)
        end = first + 1
    else:
        first = geometry.pick(1, following=1)
        end = first + 2
    base, word, page = geometry.addresses[first], geometry.word, geometry.page
    span = geometry.addresses[end] - base
    writes: list[tuple[int, bytes]] = [
        (base, patterns.generate(word, "inc", 0x10)),
        (base + 2 * word + 3, patterns.generate(3, "inc", 0x20)),
        (base + 4 * word + 1, patterns.generate(2 * word + 3, "prbs", 3)),
        (base + page - 5, patterns.generate(2 * word, "inc", 0x40)),
    ]
    long_address, long_length = base + 16 * word, 300
    used = [set(flash_words(address, len(data), word)) for address, data in writes]
    used.append(set(flash_words(long_address, long_length, word)))
    assert all(a.isdisjoint(b) for index, a in enumerate(used) for b in used[index + 1 :]), "test writes share a flash word"

    fw.flash.erase(first, end, variant=variant, layout=layout)
    model = bytearray(b"\xff" * span)
    for address, data in writes:
        fw.flash.write(address, data, variant=variant, layout=layout)
        model[address - base : address - base + len(data)] = data
    fw.flash.write(long_address, length=long_length, pattern="prbs", seed=11, variant=variant, layout=layout)
    model[long_address - base : long_address - base + long_length] = patterns.generate(long_length, "prbs", 11)

    for reader in flash_cfg["variants"]:
        for address, data in writes:
            assert fw.flash.read(address, len(data), variant=reader, layout=layout) == data, f"{reader} at {address:#x}"
        assert fw.flash.read_crc(base, span, variant=reader, layout=layout) == patterns.crc32(bytes(model)), reader


def test_programmed_word_is_refused(fw, flash_cfg):
    """A second write to a programmed flash word answers `ERR failed` (the driver would assert); a word next to it
    is still writable."""
    geometry = Geometry.read(fw, flash_cfg, "homogeneous")
    sector = geometry.pick(1, following=0)
    start, word = geometry.addresses[sector], geometry.word
    fw.flash.erase(sector, sector + 1)
    fw.flash.write(start + 1, b"\x5a")
    expect_reason(fw, f"flash.write {start} 00", "failed")
    expect_reason(fw, f"flash.write {start + word - 1} 0000", "failed")
    fw.flash.write(start + word, b"\xa5")
    assert fw.flash.read(start, word + 1) == b"\xff\x5a" + b"\xff" * (word - 2) + b"\xa5"


def test_erase_duration(fw, flash_cfg):
    """A synchronous erase blocks for the page erase time of the flash (`us` per page within the board's bounds)."""
    geometry = Geometry.read(fw, flash_cfg, "homogeneous")
    sector = geometry.pick(1, following=1)
    low, high = flash_cfg["erase_us_per_page"]
    us = fw.flash.erase(sector, sector + 2)
    assert 2 * low <= us <= 2 * high, f"{us} us for two pages"


def test_errors(fw, flash_cfg):
    geometry = Geometry.read(fw, flash_cfg, "homogeneous")
    first, sectors, size = geometry.info.first, geometry.info.sectors, geometry.info.size
    start = geometry.addresses[first]
    lines = [
        ("flash.erase", "usage"),
        (f"flash.erase {first} {first + 1} variant=fast", "usage"),
        (f"flash.erase {first} {first + 1} layout=uneven", "usage"),
        (f"flash.erase {first} {first}", "range"),
        (f"flash.erase {first} {sectors + 1}", "range"),
        (f"flash.write {start} -", "usage"),
        (f"flash.write {start} 0011 len=2", "usage"),
        (f"flash.write {start} - pattern=inc", "usage"),
        (f"flash.write {start} 001", "usage"),
        (f"flash.write {start} - len=513", "range"),
        (f"flash.write {size - 1} 0011", "range"),
        (f"flash.read {size} 1", "range"),
        ("flash.read 0 0", "range"),
        ("flash.read 0 129", "range"),
        ("flash.read 0 4 out=bin", "usage"),
        ("flash.info 1", "usage"),
    ]
    if first > 0:
        lines += [(f"flash.erase {first - 1} {first}", "range"), (f"flash.write {start - 1} 00", "range")]
    if "coord" not in flash_cfg["variants"]:
        lines += [("flash.info variant=coord", "unsupported"), ("flash.read 0 4 variant=coord", "unsupported")]
    else:
        lines += [("flash.stack running", "usage"), ("flash.stack", "usage")]
    for line, reason in lines:
        expect_reason(fw, line, reason)


@pytest.mark.family("stm32wb55")
@pytest.mark.parametrize("job", ["write", "erase"])
def test_coord_waits_for_cpu2_semaphore(fw, flash_cfg, job):
    """While HSEM 7 (CPU2's flash request) is held, a coordinated step waits; it runs once the semaphore is freed
    (here by the `hold=` timer of `hsem.take`), which the HSEM interrupt reports to the driver."""
    geometry = Geometry.read(fw, flash_cfg, "homogeneous")
    sector = geometry.pick(1, following=0)
    start = geometry.addresses[sector]
    data = patterns.generate(geometry.word * 2, "prbs", 5)
    hold_ms, margin_ms = flash_cfg["hsem_hold_ms"], flash_cfg["hold_margin_ms"]
    timeout = fw.terminal.timeout + hold_ms / 1000
    fw.flash.erase(sector, sector + 1)
    if job == "erase":
        fw.flash.write(start, data)
    fw.hsem.take(7, procid=1, hold=hold_ms)
    if job == "write":
        us = fw.flash.write(start, data, variant="coord", cmd_timeout=timeout)
        assert fw.flash.read(start, len(data)) == data
    else:
        us = fw.flash.erase(sector, sector + 1, variant="coord", cmd_timeout=timeout)
        assert fw.flash.read_crc(start, geometry.size(sector)) == erased(geometry.size(sector))
    assert us >= (hold_ms - margin_ms) * 1000, f"{job} took {us} us with HSEM 7 held {hold_ms} ms"
    assert not fw.hsem.status(7).locked


@pytest.mark.family("stm32wb55")
def test_coord_stack_starting_holds_until_fus(fw, flash_cfg):
    """`flash.stack starting` (`WirelessStackStarting()`) holds a coordinated write: its `ERR timeout` goes out, the
    flash stays erased, and `flash.stack fus` (`FirmwareUpgradeServicesReady()`) lets it complete with `EVT flash`."""
    geometry = Geometry.read(fw, flash_cfg, "homogeneous")
    sector = geometry.pick(1, following=0)
    start = geometry.addresses[sector]
    data = patterns.generate(geometry.word, "inc", 0x33)
    fw.flash.erase(sector, sector + 1)
    fw.flash.stack("starting")
    try:
        held = fw.flash.begin_write(start, data, variant="coord", cmd_timeout=flash_cfg["transfer_timeout_s"] + 2)
        response = settle(held)
        assert response is not None and not response.ok and response.reason == "timeout", response
        assert fw.flash.read(start, len(data)) == b"\xff" * len(data), "written while the stack was starting"
    finally:
        fw.flash.stack("fus")
    event = fw.flash.wait_done(timeout=2.0)
    assert event["op"] == "write", event.raw
    assert fw.flash.read(start, len(data)) == data


@pytest.mark.family("stm32wb55")
def test_coord_excludes_hsem_lock(fw):
    """The coordinated driver and `hsem.lock` both rely on the HSEM interrupt: they exclude each other (HSEM 0 of
    `ResourceAllocation`)."""
    fw.flash.stack("starting")
    try:
        expect_reason(fw, "hsem.lock 12", "busy")
    finally:
        fw.flash.stack("fus")
    assert fw.hsem.lock(12) >= 0


@pytest.fixture
def reset_afterwards(fw, board_cfg):
    """A started watchdog cannot be stopped."""
    yield
    boot_timeout = board_cfg.param("system.boot_timeout", 5.0)
    fw.terminal.drain_events()
    try:
        fw.system.ping()
        fw.system.reset(timeout=boot_timeout)
    except Exception:  # noqa: BLE001 - the board may be rebooting right now
        fw.system.wait_boot(timeout=boot_timeout)
    fw.terminal.drain_events()


@pytest.mark.family("stm32wb55")
@pytest.mark.resets_board
def test_watchdog_borrowed_while_stack_starting(fw, reset_afterwards):
    """The coordinated driver borrows the unstarted WWDG: `wdt.start` answers `ERR busy` until it is given back."""
    fw.flash.stack("starting")
    try:
        expect_reason(fw, "wdt.start 0 timeout=100", "busy")
    finally:
        fw.flash.stack("fus")
    fw.wdt.start(0, timeout=100, feed="auto")


@pytest.mark.family("stm32wb55")
@pytest.mark.resets_board
def test_coord_shares_running_watchdog(fw, flash_cfg, reset_afterwards):
    """With the watchdog running (`feed=auto`), a coordinated erase refreshes it around every step it runs with the
    interrupts masked, so the board does not reset."""
    geometry = Geometry.read(fw, flash_cfg, "homogeneous")
    sector = geometry.pick(1, following=3)
    fw.wdt.start(0, timeout=flash_cfg["watchdog_timeout_ms"], feed="auto")
    fw.flash.erase(sector, sector + 4, variant="coord", cmd_timeout=fw.terminal.timeout + 2)
    fw.terminal.collect_events("wdt", 0.3)
    assert not fw.terminal.events("boot"), "the board reset during a coordinated erase"
    fw.system.ping()
