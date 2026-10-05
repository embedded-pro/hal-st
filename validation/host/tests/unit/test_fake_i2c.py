"""The fake `i2c`, `i2cs` and `eeprom` groups (argument order and reasons of validation/firmware/I2cGroup.cpp,
I2cTarget.cpp, EepromGroup.cpp and EMIL's HilEepromCommands; PROTOCOL.md D.1-D.3), their bus model and the
`groups.i2c` wrappers."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups.i2c import DEFAULT_TIMING
from hal_st_validation.i2c import expected_timing
from hal_st_validation.patterns import crc_text, generate


def make(family="stm32wb55"):
    fake = FakeFirmware(family=family)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    pins = WB55_PINS if family == "stm32wb55" else WBA55_PINS
    return terminal, fake, Firmware(terminal, pins)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("i2c.open", "usage"),
        ("i2c.open 1 2 scl=PB8 sda=PB9", "usage"),
        ("i2c.open 1 scl=PB8 sda=PB9 speed=1", "usage"),
        ("i2c.open x scl=PB8 sda=PB9", "usage"),
        ("i2c.open 4 scl=PB8 sda=PB9", "range"),
        ("i2c.open 1 sda=PB9", "usage"),
        ("i2c.open 1 scl=PB8", "usage"),
        ("i2c.open 1 scl=PB8 sda=PB9 freq=100000 timing=0x10", "usage"),
        ("i2c.open 9 scl=PB8", "range"),
        ("i2c.open 1 scl=PB8 sda=PB9 pull=down", "usage"),
        ("i2c.open 2 scl=PB8 sda=PB9 pull=down", "usage"),
        ("i2c.open 1 scl=PB8 sda=PB9 freq=x", "usage"),
        ("i2c.open 2 scl=PB8 sda=PB9", "range"),
        ("i2c.open 0 scl=PB8 sda=PB9", "range"),
        ("i2c.open 1 scl=PB8 sda=PB9 freq=19999", "range"),
        ("i2c.open 1 scl=PB8 sda=PB9 freq=400001", "range"),
        ("i2c.open 2 scl=nope sda=PB9", "range"),
        ("i2c.open 1 scl=nope sda=PB9", "pin"),
        ("i2c.open 1 scl=PB9 sda=PB8", "pin"),
        ("i2c.open 1 scl=PC0 sda=PC1", "pin"),
        ("i2c.open 1 scl=PB6 sda=PB7", "busy"),
        ("i2c.open 1 scl=PB8 sda=PB9", "ok"),
        ("i2c.open 3 scl=i2c3scl sda=i2c3sda pull=up freq=400000", "ok"),
        ("i2c.open 3 scl=PA7 sda=PB4 timing=0x10b0172f", "ok"),
    ],
)
def test_open_reasons(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


def test_open_replies_timing_and_kernel():
    terminal, _, fw = make()
    opened = fw.i2c.open(1, "i2c1scl", "i2c1sda")
    assert (opened.timing, opened.kernel) == (DEFAULT_TIMING, 64_000_000)
    assert fw.open_instances == [("i2c", 1)]
    fw.i2c.close(1)
    assert fw.i2c.open(1, "PB8", "PB9", freq=100_000).timing == expected_timing(64_000_000, 100_000) == 0x40B03943
    fw.i2c.close(1)
    assert fw.i2c.open(1, "PB8", "PB9", timing=0x1FFFFFFF).timing == 0x10FFFFFF
    assert reason(terminal, "i2c.open 3 scl=PC0 sda=PC1") == "busy"


def test_wba55_instances():
    terminal, _, fw = make("stm32wba55")
    assert fw.i2c.open(3, "PB2", "PB1", freq=400_000).timing == 0x20B11931
    assert reason(terminal, "i2c.open 3 scl=PA6 sda=PA7") == "busy"
    fw.i2c.close(3)
    assert reason(terminal, "i2c.open 1 scl=PA15 sda=PB3") == "ok"


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("i2c.write 1 0x10", "usage"),
        ("i2c.write 1 0x10 00 extra", "usage"),
        ("i2c.write 1 0x10 00 stop=1", "usage"),
        ("i2c.write 4 0x10 00", "range"),
        ("i2c.write 3 0x10 00", "notopen"),
        ("i2c.write 1 0x80 00", "range"),
        ("i2c.write 1 x 00", "usage"),
        ("i2c.write 1 0x10 00 next=later", "usage"),
        ("i2c.write 1 0x10 0", "usage"),
        ("i2c.write 1 0x10 00 len=2", "usage"),
        ("i2c.write 1 0x10 - pattern=inc", "usage"),
        ("i2c.write 1 0x10 - len=0", "range"),
        ("i2c.write 1 0x10 - len=1025", "range"),
        ("i2c.write 1 0x10 - len=4 pattern=noise", "usage"),
        ("i2c.read 1 0x10", "usage"),
        ("i2c.read 1 0x10 0", "range"),
        ("i2c.read 1 0x10 1025", "range"),
        ("i2c.read 1 0x10 129", "range"),
        ("i2c.read 1 0x10 4 out=bin", "usage"),
        ("i2c.read 1 0x10 4 len=4", "usage"),
        ("i2c.read 3 0x10 4", "notopen"),
        ("i2c.close 3", "notopen"),
        ("i2c.close 4", "range"),
    ],
)
def test_transfer_reasons(line, expected):
    terminal, _, fw = make()
    fw.i2c.open(1, "PB8", "PB9")
    assert reason(terminal, line) == expected


def test_absent_device_nacks_with_the_hook():
    _, _, fw = make()
    fw.i2c.open(1, "PB8", "PB9")
    write = fw.i2c.write(1, 0x10, b"\x01\x02")
    assert (write.sent, write.result) == (0, "nack")
    assert fw.i2c.write(1, 0x10, b"").result == "nack"
    read = fw.i2c.read(1, 0x10, 4)
    assert (read.result, read.data) == ("nack", None)
    assert fw.i2c.hooks(1) == ["notfound"] * 3
    assert fw.i2c.write(1, 0x00, b"\x06").result == "nack"
    assert fw.i2c.write(1, 0x50, b"").result == "complete"


def test_target_registers_and_counters():
    terminal, _, fw = make()
    fw.i2c.open(3, "PC0", "PC1")
    assert fw.i2cs.open(1, "PB8", "PB9") == 0x42
    assert fw.i2c.write(3, 0x42, bytes([0x10, 1, 2, 3])).sent == 4
    assert fw.i2c.write(3, 0x42, b"\x10", next="restart").result == "complete"
    assert fw.i2c.read(3, 0x42, 3).data == b"\x01\x02\x03"
    assert fw.i2cs.dump(1, 0x10, 4) == b"\x01\x02\x03\x00"
    status = fw.i2cs.status(1, clear=True)
    assert (status.rx, status.tx, status.writes, status.reads, status.stops) == (5, 3, 2, 1, 2)
    assert status.crc == crc_text(bytes([0x10, 1, 2, 3, 0x10]))
    assert status.last == b"\x10"
    assert fw.i2cs.status(1).rx == 0
    fw.i2cs.regs(1, 0xFE, b"\xaa\xbb")
    assert fw.i2c.write(3, 0x42, b"\xfe").sent == 1
    assert fw.i2c.read(3, 0x42, 3).data == b"\xaa\xbb\x00"
    assert fw.i2c.hooks(3) == []
    assert reason(terminal, "i2cs.regs 1 0xff 0102") == "range"
    assert reason(terminal, "i2cs.dump 1 0xff 2") == "range"


def test_target_nack_sink_and_fault():
    _, _, fw = make("stm32wba55")
    fw.i2c.open(3, "PA6", "PA7")
    fw.i2cs.open(1, "PB2", "PB1", mode="sink")
    fw.i2cs.cfg(1, nack=3)
    write = fw.i2c.write(3, 0x42, len=8, pattern="prbs", seed=3)
    assert (write.sent, write.result) == (2, "nack")
    assert fw.i2cs.status(1).nacked == 1
    assert fw.i2c.hooks(3) == []
    fw.i2cs.cfg(1, pattern="prbs", seed=1)
    assert fw.i2c.read(3, 0x42, 300, out="crc").crc == crc_text(generate(300, "prbs", 1))
    fw.i2cs.cfg(1, fault="stop", faultat=2)
    assert fw.i2c.read(3, 0x42, 8).result == "buserror"
    assert fw.i2c.hooks(3) == ["buserror"]
    assert fw.i2c.read(3, 0x42, 2).data == generate(2)
    fw.i2cs.cfg(1, addrnack=True)
    assert fw.i2c.read(3, 0x42, 2).result == "nack"


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("i2cs.open 1", "usage"),
        ("i2cs.open 1 scl=PB8", "usage"),
        ("i2cs.open 1 scl=PB8 sda=PB9 mode=echo", "usage"),
        ("i2cs.open 1 scl=PB8 sda=PB9 addr=0x07", "range"),
        ("i2cs.open 1 scl=PB8 sda=PB9 addr=0x78", "range"),
        ("i2cs.open 2 scl=PB8 sda=PB9", "range"),
        ("i2cs.open 1 scl=PC0 sda=PC1", "pin"),
        ("i2cs.open 1 scl=PB8 sda=PB9 addr=0x08 mode=sink timing=0x00500916", "ok"),
    ],
)
def test_target_open_reasons(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("i2cs.cfg 1 nack=x", "usage"),
        ("i2cs.cfg 1 stretch=10001", "range"),
        ("i2cs.cfg 1 faultat=0", "range"),
        ("i2cs.cfg 1 fault=start", "usage"),
        ("i2cs.cfg 1 addrnack=2", "range"),
        ("i2cs.cfg 1 pattern=noise", "usage"),
        ("i2cs.cfg 1 speed=1", "usage"),
        ("i2cs.cfg 3", "notopen"),
        ("i2cs.status 1 clear=2", "range"),
        ("i2cs.regs 1 0 -", "usage"),
        ("i2cs.regs 1 256 00", "range"),
        ("i2cs.dump 1 0 0", "range"),
        ("i2cs.dump 1 0 129", "range"),
        ("i2cs.cfg 1 nack=2 stretch=100 stretchat=0 fault=stop faultat=3 pattern=const seed=7", "ok"),
    ],
)
def test_target_command_reasons(line, expected):
    terminal, _, fw = make()
    fw.i2cs.open(1, "PB8", "PB9")
    assert reason(terminal, line) == expected


def test_one_owner_per_instance():
    """`Resource::i2c` keeps i2c, i2cs and eeprom off each other's instance; pins stay busy too."""
    terminal, _, fw = make()
    fw.i2cs.open(1, "PB8", "PB9")
    assert reason(terminal, "i2c.open 1 scl=PA9 sda=PA10") == "busy"
    assert reason(terminal, "eeprom.attach 1 scl=PA9 sda=PA10") == "busy"
    assert reason(terminal, "gpio.cfg PB8 in") == "busy"
    fw.eeprom.attach(3, "PC0", "PC1")
    assert reason(terminal, "i2c.open 3 scl=PA7 sda=PB4") == "busy"
    assert reason(terminal, "eeprom.attach 3 scl=PC0 sda=PC1") == "busy"
    fw.close_all()
    assert reason(terminal, "i2c.open 1 scl=PB8 sda=PB9") == "ok"


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("eeprom.attach", "usage"),
        ("eeprom.attach 3 sda=PC1", "usage"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 speed=1", "usage"),
        ("eeprom.attach 4 scl=PC0 sda=PC1", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 size=x", "usage"),
        ("eeprom.attach 2 scl=PC0 sda=PC1", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 addr=0x78", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 size=0", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 size=65537", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 page=4", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 page=48", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 page=512", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 abytes=3", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 abytes=1", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 wcycle=21", "range"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 freq=500000", "range"),
        ("eeprom.attach 3 scl=PB8 sda=PB9", "pin"),
        ("eeprom.attach 3 scl=PC0 sda=PC1 addr=0x42 size=256 page=16 abytes=1 wcycle=0 freq=100000", "ok"),
        ("eeprom.detach", "notopen"),
        ("eeprom.detach 1", "usage"),
        ("eeprom.write 0 0102", "range"),
        ("eeprom.read 0 1", "range"),
        ("eeprom.erase", "ok"),
        ("eeprom.erase 1", "usage"),
    ],
)
def test_eeprom_reasons(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


def test_eeprom_pages_and_erase():
    terminal, fake, fw = make()
    fw.eeprom.attach(3, "PC0", "PC1", size=8192)
    data = generate(100, "prbs", 5)
    fw.eeprom.write(30, data)
    assert fw.eeprom.read(30, 100) == data
    assert reason(terminal, "eeprom.write 8190 010203") == "range"
    assert reason(terminal, "eeprom.read 8192 1") == "range"
    fw.eeprom.erase()
    assert fw.eeprom.read(0, 128) == b"\xff" * 128
    fake.boot()
    fw.forget_open()
    fw.eeprom.attach(3, "PC0", "PC1")
    fw.eeprom.write(20000, b"\x5a")
    fake.boot()
    fw.forget_open()
    fw.eeprom.attach(3, "PC0", "PC1")
    assert fw.eeprom.read(20000, 1) == b"\x5a"


def test_eeprom_page_wrap_on_direct_writes():
    """A write through `i2c.write` that crosses a page wraps to the start of that page, as on a 24LC256."""
    _, _, fw = make()
    fw.i2c.open(3, "PC0", "PC1")
    assert fw.i2c.write(3, 0x50, bytes([0x00, 0x3E, 1, 2, 3, 4])).sent == 6
    assert fw.i2c.write(3, 0x50, b"\x00\x3e", next="restart").sent == 2
    assert fw.i2c.read(3, 0x50, 2).data == b"\x01\x02"
    assert fw.i2c.write(3, 0x50, b"\x00\x00", next="restart").sent == 2
    assert fw.i2c.read(3, 0x50, 2).data == b"\x03\x04"


def test_eeprom_error_then_detach():
    terminal, _, fw = make()
    fw.eeprom.attach(3, "PC0", "PC1", addr=0x57)
    with pytest.raises(FirmwareError) as error:
        fw.eeprom.write(0, b"\x01")
    assert error.value.reason == "timeout"
    assert [event.raw for event in fw.eeprom.errors()] == ["EVT eeprom error=nack address=0"]
    assert reason(terminal, "eeprom.read 0 1") == "busy"
    fw.eeprom.detach()
    fw.eeprom.attach(3, "PC0", "PC1")
    fw.eeprom.write(0, b"\x01")


def test_one_byte_addressing_on_the_target():
    _, _, fw = make()
    fw.i2cs.open(1, "PB8", "PB9")
    fw.eeprom.attach(3, "PC0", "PC1", addr=0x42, size=256, page=16, abytes=1, wcycle=0)
    data = generate(40, "inc", 0x80)
    fw.eeprom.write(10, data)
    assert fw.eeprom.read(10, 40) == data
    assert fw.i2cs.dump(1, 10, 40) == data


def test_state_is_lost_on_reset():
    terminal, fake, fw = make()
    fw.i2c.open(1, "PB8", "PB9")
    fw.i2cs.open(3, "PC0", "PC1")
    fake.boot()
    fw.forget_open()
    assert reason(terminal, "i2cs.status 3") == "notopen"
    assert reason(terminal, "i2c.open 1 scl=PB8 sda=PB9") == "ok"
