"""EMIL's `eeprom.*` group over the `I2cEepromStm` adapter (`hal::Eeprom` on `hal::I2cStm`), against the external
24Cxx of `--with i2c` (`tests.eeprom`: a 24LC256 at 0x50 by default) and, for the 1-byte word address, against the
`i2cs` register target on the other instance.

The adapter splits writes at page boundaries and polls the chip's address after each page (B.1 address-NACK path)
until it acknowledges or `wcycle` ms pass; reads set the word address and read after a repeated START. An error
outside polling prints `EVT eeprom error=... address=...` and leaves EMIL's command pending (`ERR timeout` after
5 s); `eeprom.detach` recovers. Data written by one test is overwritten by the next, each at its own address range.

Scenarios: features/eeprom.feature.
"""

from __future__ import annotations

from typing import Any

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.firmware import quiesce, settle
from hal_st_validation.groups.i2c import EEPROM_BUFFER, EEPROM_TIMEOUT_S
from hal_st_validation.i2c import arm_on_start, i2c_decode
from hal_st_validation.patterns import generate

READ_CHUNK = 128
# `eeprom.write <address> <hex>` must fit the 255-character command line.
WRITE_MAX = (255 - len("eeprom.write 65535 ")) // 2


@pytest.fixture
def eeprom_cfg(need, board_cfg) -> dict[str, Any]:
    cfg = board_cfg.param("eeprom")
    need.unloaded(cfg["scl"], cfg["sda"])
    return cfg


def attach(fw, cfg, **options) -> None:
    settings = {key: cfg[key] for key in ("addr", "size", "page", "abytes", "freq")}
    settings["wcycle"] = cfg["wcycle_ms"]
    settings.update(options)
    fw.eeprom.attach(cfg["index"], cfg["scl"], cfg["sda"], **settings)


def read_all(fw, address: int, length: int) -> bytes:
    data = b""
    while len(data) < length:
        chunk = min(READ_CHUNK, length - len(data))
        data += fw.eeprom.read(address + len(data), chunk)
    return data


@pytest.fixture
def bench(request) -> None:
    """The case measures the physical bus, which the fake does not model."""
    if request.config.getoption("--fake"):
        pytest.skip("measures the physical bus: nothing to measure with --fake")


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "The chip acknowledges its address")
def test_eeprom_present():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "The attached EEPROM holds its bus until it is detached")
def test_attach_detach():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "Data written inside a page reads back")
def test_write_read_inside_a_page():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "A write across a page boundary does not wrap inside the page")
def test_write_across_a_page_boundary():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "A read of a full buffer spans pages")
def test_reads_across_pages():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "A write after a read polls for the write cycle")
def test_write_read_write_read():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "Erase writes 0xFF over the erase size")
def test_erase():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "The data survives a reset")
def test_data_survives_reset():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "Transfers past the size are refused")
def test_range_at_size():
    pass


@pytest.mark.usefixtures("bench", "eeprom_cfg")
@scenario("eeprom.feature", "The logic analyser sees the page write and the first NACKed poll")
def test_ack_polling_on_la():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "Detach recovers from an error outside polling")
def test_error_then_detach_recovers():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "Detach during a transfer is refused")
def test_detach_while_busy():
    pass


@pytest.mark.usefixtures("eeprom_cfg")
@scenario("eeprom.feature", "One-byte word addresses reach the registers of the i2cs target")
def test_one_byte_addressing_on_target():
    pass


@given("the firmware is not the fake, which completes every write at once")
def not_fake(request):
    if request.config.getoption("--fake"):
        pytest.skip("the fake completes every write at once")


@given("the logic analyser DIOs on the SCL and SDA pins of the EEPROM", target_fixture="dios")
def dios(need, eeprom_cfg):
    return need.dio(eeprom_cfg["scl"]), need.dio(eeprom_cfg["sda"])


@given("the i2cs target on the I2C instance of the loop that is not the EEPROM's", target_fixture="target")
def other_target(board_cfg, eeprom_cfg, need):
    target = board_cfg.param("i2c.loop.target")
    if target["index"] == eeprom_cfg["index"]:
        target = board_cfg.param("i2c.loop.master")
    need.unloaded(target["scl"], target["sda"])
    return target


@given("the EEPROM is attached")
@when("the EEPROM is attached")
def attached(fw, eeprom_cfg):
    attach(fw, eeprom_cfg)


@given(parsers.parse("the EEPROM is attached at {bus_freq:d} Hz"))
def attached_at_freq(fw, eeprom_cfg, bus_freq):
    attach(fw, eeprom_cfg, freq=bus_freq)


@given(parsers.parse("the EEPROM is attached at address {chip:x}"))
def attached_at_address(fw, eeprom_cfg, chip):
    attach(fw, eeprom_cfg, addr=chip)


@given("the EEPROM is attached with the erase size of the board file as its size", target_fixture="erase_size")
def attached_with_erase_size(fw, eeprom_cfg):
    size = eeprom_cfg["erase_size"]
    attach(fw, eeprom_cfg, size=size)
    return size


@when(
    parsers.parse(
        "the EEPROM is attached at address {chip:x} with {size:d} bytes, {page:d}-byte pages, {abytes:d} address byte "
        "and a write cycle of {wcycle:d} ms"
    )
)
def attached_to_target(fw, eeprom_cfg, chip, size, page, abytes, wcycle):
    attach(fw, eeprom_cfg, addr=chip, size=size, page=page, abytes=abytes, wcycle=wcycle)


@given(parsers.parse("the address {offset:d} bytes into page {page_index:d}"), target_fixture="address")
def address_in_page(eeprom_cfg, offset, page_index):
    return page_index * eeprom_cfg["page"] + offset


@given(parsers.parse("the address {offset:d} bytes before page {page_index:d}"), target_fixture="address")
def address_before_page(eeprom_cfg, offset, page_index):
    return page_index * eeprom_cfg["page"] - offset


@given(parsers.parse("the start of page {page_index:d}"), target_fixture="address")
def page_start(eeprom_cfg, page_index):
    return page_index * eeprom_cfg["page"]


@given(parsers.parse("the word address {at:d}"), target_fixture="address")
def word_address(at):
    return at


@given(parsers.parse("half a page of PRBS data with seed {prbs_seed:d}"), target_fixture="data")
def half_page_data(eeprom_cfg, prbs_seed):
    return generate(eeprom_cfg["page"] // 2, "prbs", prbs_seed)


@given(
    parsers.parse("a page and {extra:d} bytes of PRBS data with seed {prbs_seed:d}, or as much as a command line can write"),
    target_fixture="data",
)
def page_and_more_data(eeprom_cfg, extra, prbs_seed):
    return generate(min(WRITE_MAX, eeprom_cfg["page"] + extra), "prbs", prbs_seed)


@given(parsers.parse("{count:d} bytes of PRBS data with seed {prbs_seed:d}"), target_fixture="data")
def prbs_data(count, prbs_seed):
    return generate(count, "prbs", prbs_seed)


@given(parsers.parse("a buffer of data incrementing from {start:x}"), target_fixture="data")
def incrementing_buffer(start):
    return generate(EEPROM_BUFFER, "inc", start)


@when("the I2C instance of the EEPROM is opened on its pins")
def open_i2c(fw, eeprom_cfg):
    fw.i2c.open(eeprom_cfg["index"], eeprom_cfg["scl"], eeprom_cfg["sda"])


@when("the EEPROM is detached")
def detach(fw):
    fw.eeprom.detach()


@when("the EEPROM is erased")
def erase(fw):
    fw.eeprom.erase()


@when("the data is written there")
def write_data(fw, address, data):
    fw.eeprom.write(address, data)


@when("the data is written there in two halves")
def write_halves(fw, address, data):
    half = EEPROM_BUFFER // 2
    fw.eeprom.write(address, data[:half])
    fw.eeprom.write(address + half, data[half:])


@when(parsers.parse("{count:d} zero bytes are written at the end of the erase size"))
def write_zeros_at_end(fw, erase_size, count):
    fw.eeprom.write(erase_size - count, b"\x00" * count)


@when(parsers.parse("{value:x} is written at the last address"))
def write_last(fw, eeprom_cfg, value):
    fw.eeprom.write(eeprom_cfg["size"] - 1, bytes([value]))


@when("the board resets and the firmware forgets what was open")
def reset_board(fw):
    fw.system.reset()
    fw.forget_open()


@when("the logic analyser is armed on a START for 2 ms at up to 2 MHz, with 1 % pretrigger", target_fixture="capture")
def arm(ad3, dios):
    scl, sda = dios
    rate = min(ad3.logic.clock_hz, 2e6)
    samples = min(ad3.logic.buffer_size, int(rate * 2e-3))
    return arm_on_start(ad3, scl, sda, rate, samples, pretrigger=0.01)


@when("the pending EEPROM errors are cleared")
def clear_errors(fw):
    fw.eeprom.errors()


@when("the longest write a command line holds is begun there, with EMIL's timeout on top of the command timeout", target_fixture="pending")
def begin_longest_write(fw, address):
    return fw.eeprom.begin("write", address, generate(WRITE_MAX), cmd_timeout=fw.terminal.timeout + EEPROM_TIMEOUT_S)


@when("the EEPROM is detached without waiting for a reply")
def detach_nowait(fw):
    fw.terminal.send_nowait("eeprom.detach")


@when(parsers.parse("the i2cs target opens at address {chip:x}"))
def open_target(fw, target, chip):
    fw.i2cs.open(target["index"], target["scl"], target["sda"], addr=chip)


@then("a zero-length write to the chip address completes")
def probe_completes(fw, eeprom_cfg):
    assert fw.i2c.write(eeprom_cfg["index"], eeprom_cfg["addr"]).result == "complete"


@then(parsers.parse('attaching the EEPROM again on its pins fails with "{reason}"'))
def attach_again_refused(fw, eeprom_cfg, reason):
    expect_error(reason, fw.eeprom.attach, eeprom_cfg["index"], eeprom_cfg["scl"], eeprom_cfg["sda"])


@then(parsers.parse('opening the I2C instance of the EEPROM on its pins fails with "{reason}"'))
def open_i2c_refused(fw, eeprom_cfg, reason):
    expect_error(reason, fw.i2c.open, eeprom_cfg["index"], eeprom_cfg["scl"], eeprom_cfg["sda"])


@then(parsers.parse('detaching the EEPROM again fails with "{reason}"'))
def detach_refused(fw, reason):
    expect_error(reason, fw.eeprom.detach)


@then(parsers.parse('reading {length:d} byte at address {at:d} fails with "{reason}"'))
def read_refused(fw, length, at, reason):
    expect_error(reason, fw.eeprom.read, at, length)


@then(parsers.parse('writing {count:d} zero bytes at the last address fails with "{reason}"'))
def write_last_refused(fw, eeprom_cfg, count, reason):
    expect_error(reason, fw.eeprom.write, eeprom_cfg["size"] - 1, b"\x00" * count)


@then(parsers.parse('reading {length:d} byte at the size fails with "{reason}"'))
def read_at_size_refused(fw, eeprom_cfg, length, reason):
    expect_error(reason, fw.eeprom.read, eeprom_cfg["size"], length)


@then(parsers.parse('reading {length:d} bytes at the last address fails with "{reason}"'))
def read_last_refused(fw, eeprom_cfg, length, reason):
    expect_error(reason, fw.eeprom.read, eeprom_cfg["size"] - 1, length)


@then(parsers.parse('writing the byte {value:x} at address {at:d} fails with "{reason}"'))
def write_byte_refused(fw, value, at, reason):
    expect_error(reason, fw.eeprom.write, at, bytes([value]))


@then("reading the data back at the address gives the data")
def read_back(fw, address, data):
    assert fw.eeprom.read(address, len(data)) == data


@then(parsers.parse("the first {count:d} bytes of page {page_index:d} read as bytes {first:d} to {last:d} of the data"))
def page_start_reads(fw, eeprom_cfg, data, count, page_index, first, last):
    assert fw.eeprom.read(page_index * eeprom_cfg["page"], count) == data[first : last + 1]


@then("reading from 1 byte past the address gives the rest of the data")
def read_rest(fw, address, data):
    assert fw.eeprom.read(address + 1, EEPROM_BUFFER - 1) == data[1:]


@then(parsers.parse("{count:d} bytes of PRBS data with seed {first_seed:d}, then with seed {second_seed:d}, each written there read back"))
def write_read_twice(fw, address, count, first_seed, second_seed):
    for prbs_seed in (first_seed, second_seed):
        data = generate(count, "prbs", prbs_seed)
        fw.eeprom.write(address, data)
        assert fw.eeprom.read(address, count) == data


@then(parsers.parse("the whole erase size reads {value:x}"))
def erased(fw, erase_size, value):
    assert read_all(fw, 0, erase_size) == bytes([value]) * erase_size


@then(parsers.parse("the last address reads {value:x}"))
def last_reads(fw, eeprom_cfg, value):
    assert fw.eeprom.read(eeprom_cfg["size"] - 1, 1) == bytes([value])


@then("the logic analyser decodes the page write and at least one more transfer within 2 s", target_fixture="transfers")
def decoded(capture, dios):
    transfers = i2c_decode(*capture.wait(timeout=2.0).channels(*dios))
    assert len(transfers) >= 2, "no poll after the page write"
    return transfers


@then("the first transfer writes the word address and the data to the chip, acknowledged and ended by a STOP")
def page_write_decoded(eeprom_cfg, transfers, address, data):
    write = transfers[0]
    chip = eeprom_cfg["addr"]
    assert (write.address, write.read, write.address_ack) == (chip, False, True)
    assert write.payload == address.to_bytes(eeprom_cfg["abytes"], "big") + data
    assert write.stop


@then("the second transfer, the first poll, addresses the chip and is not acknowledged")
def poll_decoded(eeprom_cfg, transfers):
    poll = transfers[1]
    chip = eeprom_cfg["addr"]
    assert (poll.address, poll.address_ack) == (chip, False)


@then(parsers.parse("the EEPROM reports exactly one error, {error} at address {at:d}"))
def reported_error(fw, error, at):
    errors = fw.eeprom.errors()
    assert [(event["error"], event.as_int("address")) for event in errors] == [(error, at)]


@then(parsers.parse("writing the byte {value:x} at the start of page {page_index:d} succeeds"))
def write_byte_succeeds(fw, eeprom_cfg, value, page_index):
    fw.eeprom.write(page_index * eeprom_cfg["page"], bytes([value]))


@then(parsers.parse('the detach is answered first, with "{reason}"'))
def detach_answered_busy(fw, pending, reason):
    # The detach answers first; the write's own OK follows once its pages are written and is dropped by quiesce.
    reply = settle(pending)
    quiesce(fw.terminal, quiet=0.2)
    assert reply is not None and reply.reason == reason, reply


@then(parsers.parse("the first {count:d} bytes there read as the default pattern"))
def default_pattern_reads(fw, address, count):
    assert fw.eeprom.read(address, count) == generate(count)


@then("the registers of the i2cs target from the address hold the data")
def target_registers(fw, target, address, data):
    assert fw.i2cs.dump(target["index"], address, len(data)) == data
