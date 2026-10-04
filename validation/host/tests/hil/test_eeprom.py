"""EMIL's `eeprom.*` group over the `I2cEepromStm` adapter (`hal::Eeprom` on `hal::I2cStm`), against the external
24Cxx of `--with i2c` (`tests.eeprom`: a 24LC256 at 0x50 by default) and, for the 1-byte word address, against the
`i2cs` register target on the other instance.

The adapter splits writes at page boundaries and polls the chip's address after each page (B.1 address-NACK path)
until it acknowledges or `wcycle` ms pass; reads set the word address and read after a repeated START. An error
outside polling prints `EVT eeprom error=... address=...` and leaves EMIL's command pending (`ERR timeout` after
5 s); `eeprom.detach` recovers. Data written by one test is overwritten by the next, each at its own address range.
"""

from __future__ import annotations

from typing import Any

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation.firmware import quiesce, settle
from hal_st_validation.groups.i2c import EEPROM_BUFFER, EEPROM_TIMEOUT_S
from hal_st_validation.i2c import arm_on_start, i2c_decode
from hal_st_validation.patterns import generate

pytestmark = pytest.mark.requires_option("i2c")

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


def test_eeprom_present(fw, eeprom_cfg):
    """A zero-length write (address probe) to the chip is acknowledged."""
    fw.i2c.open(eeprom_cfg["index"], eeprom_cfg["scl"], eeprom_cfg["sda"])
    assert fw.i2c.write(eeprom_cfg["index"], eeprom_cfg["addr"]).result == "complete"


def test_attach_detach(fw, eeprom_cfg):
    attach(fw, eeprom_cfg)
    expect_error("busy", fw.eeprom.attach, eeprom_cfg["index"], eeprom_cfg["scl"], eeprom_cfg["sda"])
    expect_error("busy", fw.i2c.open, eeprom_cfg["index"], eeprom_cfg["scl"], eeprom_cfg["sda"])
    fw.eeprom.detach()
    expect_error("notopen", fw.eeprom.detach)
    expect_error("range", fw.eeprom.read, 0, 1)
    fw.eeprom.erase()
    attach(fw, eeprom_cfg)


def test_write_read_inside_a_page(fw, eeprom_cfg):
    attach(fw, eeprom_cfg)
    address = 2 * eeprom_cfg["page"] + 3
    data = generate(eeprom_cfg["page"] // 2, "prbs", 11)
    fw.eeprom.write(address, data)
    assert fw.eeprom.read(address, len(data)) == data


def test_write_across_a_page_boundary(fw, eeprom_cfg):
    """The adapter splits the write at the page boundary, so nothing wraps inside a page."""
    attach(fw, eeprom_cfg)
    page = eeprom_cfg["page"]
    address = 4 * page - 5
    data = generate(min(WRITE_MAX, page + 10), "prbs", 12)
    fw.eeprom.write(address, data)
    assert fw.eeprom.read(address, len(data)) == data
    assert fw.eeprom.read(4 * page, 5) == data[5:10]


def test_reads_across_pages(fw, eeprom_cfg):
    attach(fw, eeprom_cfg)
    address = 6 * eeprom_cfg["page"]
    data = generate(EEPROM_BUFFER, "inc", 0x30)
    half = EEPROM_BUFFER // 2
    fw.eeprom.write(address, data[:half])
    fw.eeprom.write(address + half, data[half:])
    assert fw.eeprom.read(address, EEPROM_BUFFER) == data
    assert fw.eeprom.read(address + 1, EEPROM_BUFFER - 1) == data[1:]


def test_write_read_write_read(fw, eeprom_cfg):
    """B.1h: ACK polling after a write that follows a read (stale counters used to abort or report 4294967295)."""
    attach(fw, eeprom_cfg)
    address = 10 * eeprom_cfg["page"]
    for seed in (1, 2):
        data = generate(16, "prbs", seed)
        fw.eeprom.write(address, data)
        assert fw.eeprom.read(address, 16) == data


@pytest.mark.slow
def test_erase(fw, eeprom_cfg):
    """`eeprom.erase` writes 0xFF over `erase_size` (attached with that size: EMIL's 5 s limit covers it)."""
    size = eeprom_cfg["erase_size"]
    attach(fw, eeprom_cfg, size=size)
    fw.eeprom.write(size - 8, b"\x00" * 8)
    fw.eeprom.erase()
    assert read_all(fw, 0, size) == b"\xff" * size


@pytest.mark.resets_board
def test_data_survives_reset(fw, eeprom_cfg):
    attach(fw, eeprom_cfg)
    address = 12 * eeprom_cfg["page"]
    data = generate(8, "prbs", 99)
    fw.eeprom.write(address, data)
    fw.system.reset()
    fw.forget_open()
    attach(fw, eeprom_cfg)
    assert fw.eeprom.read(address, 8) == data


def test_range_at_size(fw, eeprom_cfg):
    size = eeprom_cfg["size"]
    attach(fw, eeprom_cfg)
    expect_error("range", fw.eeprom.write, size - 1, b"\x00\x00")
    expect_error("range", fw.eeprom.read, size, 1)
    expect_error("range", fw.eeprom.read, size - 1, 2)
    fw.eeprom.write(size - 1, b"\xa5")
    assert fw.eeprom.read(size - 1, 1) == b"\xa5"


@pytest.mark.ad3
def test_ack_polling_on_la(fw, ad3, need, bench, eeprom_cfg):
    """At 100 kHz from the START of the page write: the control byte (0xA0 for a chip at 0x50), the word address and
    the data, then the first poll (the address bytes again) NACKed during the write cycle."""
    scl, sda = need.dio(eeprom_cfg["scl"]), need.dio(eeprom_cfg["sda"])
    attach(fw, eeprom_cfg, freq=100_000)
    address = 14 * eeprom_cfg["page"]
    data = generate(4, "prbs", 3)
    rate = min(ad3.logic.clock_hz, 2e6)
    samples = min(ad3.logic.buffer_size, int(rate * 2e-3))
    capture = arm_on_start(ad3, scl, sda, rate, samples, pretrigger=0.01)
    fw.eeprom.write(address, data)
    transfers = i2c_decode(*capture.wait(timeout=2.0).channels(scl, sda))
    assert len(transfers) >= 2, "no poll after the page write"
    write, poll = transfers[0], transfers[1]
    chip = eeprom_cfg["addr"]
    assert (write.address, write.read, write.address_ack) == (chip, False, True)
    assert write.payload == address.to_bytes(eeprom_cfg["abytes"], "big") + data
    assert write.stop
    assert (poll.address, poll.address_ack) == (chip, False)


@pytest.mark.slow
def test_error_then_detach_recovers(fw, eeprom_cfg):
    """No device at 0x57: the first page write is NACKed (`EVT eeprom error=nack address=0`), EMIL answers
    `ERR timeout` and stays busy; `eeprom.detach` completes the operation and a new attach works."""
    attach(fw, eeprom_cfg, addr=0x57)
    fw.eeprom.errors()
    expect_error("timeout", fw.eeprom.write, 0, b"\x01")
    errors = fw.eeprom.errors()
    assert [(event["error"], event.as_int("address")) for event in errors] == [("nack", 0)]
    expect_error("busy", fw.eeprom.read, 0, 1)
    fw.eeprom.detach()
    attach(fw, eeprom_cfg)
    fw.eeprom.write(16 * eeprom_cfg["page"], b"\x01")


def test_detach_while_busy(fw, request, eeprom_cfg):
    """`eeprom.detach` while the adapter is in a transfer answers `ERR busy` (the longest write the command line
    holds spans two pages, two write cycles)."""
    if request.config.getoption("--fake"):
        pytest.skip("the fake completes every write at once")
    attach(fw, eeprom_cfg)
    address = 18 * eeprom_cfg["page"]
    pending = fw.eeprom.begin("write", address, generate(WRITE_MAX), cmd_timeout=fw.terminal.timeout + EEPROM_TIMEOUT_S)
    fw.terminal.send_nowait("eeprom.detach")
    # The detach answers first; the write's own OK follows once its pages are written and is dropped by quiesce.
    reply = settle(pending)
    quiesce(fw.terminal, quiet=0.2)
    assert reply is not None and reply.reason == "busy", reply
    assert fw.eeprom.read(address, 8) == generate(8)


def test_one_byte_addressing_on_target(fw, board_cfg, eeprom_cfg, need):
    """`abytes=1` and 16-byte pages against the `i2cs` register file at 0x42 on the other instance: the word address
    is the register pointer, so the data lands in the target's registers."""
    target = board_cfg.param("i2c.loop.target")
    if target["index"] == eeprom_cfg["index"]:
        target = board_cfg.param("i2c.loop.master")
    need.unloaded(target["scl"], target["sda"])
    fw.i2cs.open(target["index"], target["scl"], target["sda"], addr=0x42)
    attach(fw, eeprom_cfg, addr=0x42, size=256, page=16, abytes=1, wcycle=0)
    data = generate(40, "prbs", 21)
    fw.eeprom.write(10, data)
    assert fw.eeprom.read(10, len(data)) == data
    assert fw.i2cs.dump(target["index"], 10, len(data)) == data
