"""Internal flash through the `flash` group, over the scratch region of the board (absolute pages 64-143 on STM32WB55,
64-127 on STM32WBA55): `hal::FlashHomogeneousInternalStm` / `hal::FlashInternalStm` (`variant=async`), their synchronous twins
(`variant=sync`) and on STM32WB55 `hal::FlashCoordinatedWithWirelessStack` (`variant=coord`) over the async driver.

`layout=homogeneous` has one sector per page; `layout=table` has single pages first and the page pattern 1, 1, 2, 4
twice at the end (`groups.system_ext.table_sectors`). The firmware accepts erases and writes only from the sector
`first` on: an unfixed erase (B.5) takes the sector index for the absolute page, which from there on cannot reach the
running image. Every write targets flash words (8 bytes on STM32WB55, 16 on STM32WBA55) no earlier write programmed
since the last erase; programming one twice would fail the driver's assertion, so the firmware answers `ERR failed`.

The coordinated driver waits for HSEM 7 (CPU2 holds it while it needs the flash), holds its steps while the wireless
stack is starting (`flash.stack starting` until `fus`), refreshes the watchdog it shares with `wdt` and excludes
`hsem.lock`. No wiring.

Scenarios: features/flash.feature.
"""

from __future__ import annotations

from dataclasses import dataclass

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import patterns
from hal_st_validation.firmware import settle
from hal_st_validation.groups.system_ext import FlashInfo, flash_words, sector_addresses, table_sectors


@pytest.fixture
def flash_cfg(board_cfg):
    return board_cfg.param("flash")


@pytest.fixture
def state():
    """What the steps of one scenario hand on to the later ones."""
    return {}


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


@pytest.mark.board_params("layout", "flash.layouts")
@scenario("flash.feature", "The geometry agrees with the board file and the layout in every variant")
def test_geometry(layout):
    pass


@pytest.mark.board_params("variant", "flash.variants")
@pytest.mark.board_params("layout", "flash.layouts")
@scenario("flash.feature", "Erasing a sector erases exactly its pages")
def test_erase_hits_region_page(variant, layout):
    pass


@pytest.mark.board_params("variant", "flash.variants")
@pytest.mark.board_params("layout", "flash.layouts")
@scenario("flash.feature", "Writes of every shape read back exactly")
def test_write_shapes(variant, layout):
    pass


@scenario("flash.feature", "A programmed flash word is refused")
def test_programmed_word_is_refused():
    pass


@scenario("flash.feature", "A synchronous erase takes the page erase time")
def test_erase_duration():
    pass


@scenario("flash.feature", "Malformed and out-of-range commands are refused")
def test_errors():
    pass


@pytest.mark.parametrize("job", ["write", "erase"])
@scenario("flash.feature", "A coordinated step waits for the CPU2 semaphore")
def test_coord_waits_for_cpu2_semaphore(job):
    pass


@scenario("flash.feature", "A starting wireless stack holds a coordinated write until FUS")
def test_coord_stack_starting_holds_until_fus():
    pass


@scenario("flash.feature", "The coordinated driver and hsem.lock exclude each other")
def test_coord_excludes_hsem_lock():
    pass


@pytest.mark.usefixtures("reset_afterwards")
@scenario("flash.feature", "The coordinated driver borrows the unstarted watchdog while the stack starts")
def test_watchdog_borrowed_while_stack_starting():
    pass


@pytest.mark.usefixtures("reset_afterwards")
@scenario("flash.feature", "A coordinated erase refreshes the running watchdog")
def test_coord_shares_running_watchdog():
    pass


@given("the geometry of the layout")
def geometry_of_layout(fw, flash_cfg, layout, state):
    state["geometry"] = Geometry.read(fw, flash_cfg, layout)


@given("the geometry of the homogeneous layout")
def geometry_homogeneous(fw, flash_cfg, state):
    state["geometry"] = Geometry.read(fw, flash_cfg, "homogeneous")


def pick_single_page(fw, flash_cfg, state, following):
    geometry = Geometry.read(fw, flash_cfg, "homogeneous")
    sector = geometry.pick(1, following=following)
    state["geometry"], state["sector"], state["start"] = geometry, sector, geometry.addresses[sector]


@given("the first accepted single-page sector of the homogeneous layout")
def single_page_sector(fw, flash_cfg, state):
    pick_single_page(fw, flash_cfg, state, 0)


@given("the first accepted single-page sector of the homogeneous layout and the next accepted one")
def single_page_sector_and_next(fw, flash_cfg, state):
    pick_single_page(fw, flash_cfg, state, 1)


@given("the first accepted single-page sector of the homogeneous layout and the next three accepted ones")
def single_page_sector_and_next_three(fw, flash_cfg, state):
    pick_single_page(fw, flash_cfg, state, 3)


@then("its base and size are those of the scratch region of the board file")
def base_and_size(flash_cfg, state):
    info = state["geometry"].info
    assert info.base == flash_cfg["base"], info.raw
    assert info.size == flash_cfg["pages"] * flash_cfg["page"], info.raw


@then("it has one sector per sector of the layout")
def sector_count(state):
    info = state["geometry"].info
    assert info.sectors == len(state["geometry"].pages), info.raw


@then("it reports the layout")
def reports_layout(state, layout):
    assert state["geometry"].info.layout == layout


@then("the image ends no later than the first page of the scratch region")
def image_below_region(flash_cfg, state):
    info = state["geometry"].info
    assert info.image <= flash_cfg["first_page"], f"the image reaches the scratch region: {info.raw}"


@then("the first accepted sector is the sector of the image, at most the sector count")
def first_accepted(state):
    info = state["geometry"].info
    assert info.first == min(info.image, info.sectors), info.raw


@then("at least one sector is accepted")
def sector_left(state):
    info = state["geometry"].info
    assert info.first < info.sectors, f"no sector left to test: {info.raw}"


@then("every variant reports the same base, sector count, size, first accepted sector and image")
def variants_agree(fw, flash_cfg, layout, state):
    info = state["geometry"].info
    for variant in flash_cfg["variants"]:
        other = fw.flash.info(variant=variant, layout=layout)
        assert (other.base, other.sectors, other.size, other.first, other.image) == (
            info.base,
            info.sectors,
            info.size,
            info.first,
            info.image,
        )


@then(
    "for the first accepted 2- and 4-page sectors in the table layout, the first accepted single-page one otherwise, erased with the "
    "next sector and marked at its start, its end and the start of the next sector, erasing the sector with the variant erases it, "
    "keeps the marker of the next sector and leaves the previous accepted sector as it was"
)
def erase_hits_region_page(fw, state, variant, layout):
    geometry = state["geometry"]
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


@given("the first accepted 2-page sector in the table layout, the first accepted single-page sector and the next one otherwise")
def write_sectors(layout, state):
    geometry = state["geometry"]
    if layout == "table":
        first = geometry.pick(2, following=0)
        end = first + 1
    else:
        first = geometry.pick(1, following=1)
        end = first + 2
    state["first"], state["end"] = first, end


@given("an aligned, an unaligned, an odd-length and a page-crossing write and a 300-byte PRBS write with seed 11, on distinct flash words")
def write_shapes(state):
    geometry, first, end = state["geometry"], state["first"], state["end"]
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
    state.update(base=base, span=span, writes=writes, long_address=long_address, long_length=long_length)


@when("the sectors are erased with the variant")
def erase_write_sectors(fw, state, variant, layout):
    fw.flash.erase(state["first"], state["end"], variant=variant, layout=layout)
    state["model"] = bytearray(b"\xff" * state["span"])


@when("the writes are made with the variant")
def make_writes(fw, state, variant, layout):
    base, model = state["base"], state["model"]
    for address, data in state["writes"]:
        fw.flash.write(address, data, variant=variant, layout=layout)
        model[address - base : address - base + len(data)] = data
    long_address, long_length = state["long_address"], state["long_length"]
    fw.flash.write(long_address, length=long_length, pattern="prbs", seed=11, variant=variant, layout=layout)
    model[long_address - base : long_address - base + long_length] = patterns.generate(long_length, "prbs", 11)


@then("every variant reads back each write and the CRC-32 of the sectors with the writes and erased bytes elsewhere")
def every_variant_reads_back(fw, flash_cfg, state, layout):
    for reader in flash_cfg["variants"]:
        for address, data in state["writes"]:
            assert fw.flash.read(address, len(data), variant=reader, layout=layout) == data, f"{reader} at {address:#x}"
        assert fw.flash.read_crc(state["base"], state["span"], variant=reader, layout=layout) == patterns.crc32(bytes(state["model"])), (
            reader
        )


@when("the sector is erased")
def erase_sector(fw, state):
    fw.flash.erase(state["sector"], state["sector"] + 1)


@when("byte 5a is written at offset 1 of the sector")
def write_5a(fw, state):
    fw.flash.write(state["start"] + 1, b"\x5a")


@then(parsers.parse('writing 00 at offset 0 of the sector fails with "{reason}"'))
def rewrite_start_refused(fw, state, reason):
    expect_reason(fw, f"flash.write {state['start']} 00", reason)


@then(parsers.parse('writing 0000 at the last byte of the first flash word of the sector fails with "{reason}"'))
def rewrite_word_end_refused(fw, state, reason):
    expect_reason(fw, f"flash.write {state['start'] + state['geometry'].word - 1} 0000", reason)


@when("byte a5 is written at the start of the second flash word of the sector")
def write_a5(fw, state):
    fw.flash.write(state["start"] + state["geometry"].word, b"\xa5")


@then("the sector reads ff 5a, erased bytes up to the second flash word and a5")
def programmed_words_read_back(fw, state):
    word = state["geometry"].word
    assert fw.flash.read(state["start"], word + 1) == b"\xff\x5a" + b"\xff" * (word - 2) + b"\xa5"


@when("the sector and the next one are erased", target_fixture="us")
def erase_two_sectors(fw, state):
    return fw.flash.erase(state["sector"], state["sector"] + 2)


@then("the erase took twice the page erase time within its bounds")
def erase_duration(flash_cfg, us):
    low, high = flash_cfg["erase_us_per_page"]
    assert 2 * low <= us <= 2 * high, f"{us} us for two pages"


@then("every malformed, out-of-range or unsupported command fails with its reason")
def commands_refused(fw, flash_cfg, state):
    geometry = state["geometry"]
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


@given("two flash words of PRBS data with seed 5")
def prbs_data(state):
    state["data"] = patterns.generate(state["geometry"].word * 2, "prbs", 5)


@given("one flash word of incrementing data from 0x33")
def incrementing_data(state):
    state["data"] = patterns.generate(state["geometry"].word, "inc", 0x33)


@when("the data is written to the sector if the job is an erase")
def write_before_erase_job(fw, state, job):
    if job == "erase":
        fw.flash.write(state["start"], state["data"])


@when("process 1 takes HSEM 7 for the HSEM hold")
def take_hsem_7(fw, flash_cfg):
    fw.hsem.take(7, procid=1, hold=flash_cfg["hsem_hold_ms"])


@when("the job runs coordinated: a write of the data or an erase of the sector", target_fixture="us")
def coordinated_job(fw, flash_cfg, state, job):
    timeout = fw.terminal.timeout + flash_cfg["hsem_hold_ms"] / 1000
    if job == "write":
        return fw.flash.write(state["start"], state["data"], variant="coord", cmd_timeout=timeout)
    return fw.flash.erase(state["sector"], state["sector"] + 1, variant="coord", cmd_timeout=timeout)


@then("the sector holds the data after a write and is erased after an erase")
def job_result(fw, state, job):
    start = state["start"]
    if job == "write":
        assert fw.flash.read(start, len(state["data"])) == state["data"]
    else:
        assert fw.flash.read_crc(start, state["geometry"].size(state["sector"])) == erased(state["geometry"].size(state["sector"]))


@then("the job took at least the HSEM hold less the hold margin")
def job_waited(flash_cfg, job, us):
    hold_ms, margin_ms = flash_cfg["hsem_hold_ms"], flash_cfg["hold_margin_ms"]
    assert us >= (hold_ms - margin_ms) * 1000, f"{job} took {us} us with HSEM 7 held {hold_ms} ms"


@then("HSEM 7 is free")
def hsem_7_free(fw):
    assert not fw.hsem.status(7).locked


@then(
    'while the wireless stack is starting, a coordinated write of the data answers "timeout" and leaves the sector erased, and then the '
    "stack reports FUS ready"
)
def write_held_until_fus(fw, flash_cfg, state):
    start, data = state["start"], state["data"]
    fw.flash.stack("starting")
    try:
        held = fw.flash.begin_write(start, data, variant="coord", cmd_timeout=flash_cfg["transfer_timeout_s"] + 2)
        response = settle(held)
        assert response is not None and not response.ok and response.reason == "timeout", response
        assert fw.flash.read(start, len(data)) == b"\xff" * len(data), "written while the stack was starting"
    finally:
        fw.flash.stack("fus")


@then(parsers.parse("the flash event of a write arrives within {seconds:g} s"))
def write_event(fw, seconds):
    event = fw.flash.wait_done(timeout=seconds)
    assert event["op"] == "write", event.raw


@then("the sector holds the data")
def sector_holds_data(fw, state):
    assert fw.flash.read(state["start"], len(state["data"])) == state["data"]


@then(
    parsers.parse('while the wireless stack is starting, the command "{line}" fails with "{reason}", and then the stack reports FUS ready')
)
def refused_while_stack_starting(fw, line, reason):
    fw.flash.stack("starting")
    try:
        expect_reason(fw, line, reason)
    finally:
        fw.flash.stack("fus")


@then(parsers.parse("HSEM {number:d} locks"))
def hsem_locks(fw, number):
    assert fw.hsem.lock(number) >= 0


@then(parsers.parse("watchdog {number:d} starts with a {timeout:d} ms timeout and automatic feeding"))
def watchdog_starts(fw, number, timeout):
    fw.wdt.start(number, timeout=timeout, feed="auto")


@when("watchdog 0 starts with the watchdog timeout and automatic feeding")
def watchdog_starts_with_timeout(fw, flash_cfg):
    fw.wdt.start(0, timeout=flash_cfg["watchdog_timeout_ms"], feed="auto")


@when("the sector and the next three are erased coordinated")
def erase_four_coordinated(fw, state):
    fw.flash.erase(state["sector"], state["sector"] + 4, variant="coord", cmd_timeout=fw.terminal.timeout + 2)


@when(parsers.parse("the watchdog events of {seconds:g} s are collected"))
def collect_watchdog_events(fw, seconds):
    fw.terminal.collect_events("wdt", seconds)


@then("the board did not reset")
def no_reset(fw):
    assert not fw.terminal.events("boot"), "the board reset during a coordinated erase"


@then("the board answers ping")
def answers_ping(fw):
    fw.system.ping()
