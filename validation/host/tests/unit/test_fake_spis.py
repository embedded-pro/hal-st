"""The fake `spis` group (argument order and reasons of validation/firmware/SpiSlaveGroup.cpp; PROTOCOL.md D.5), the
pins, SPI instance and DMA channels it holds, its arm/result state, the SPI loop through `spi.xfer` and the
`groups.spis` wrappers."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareTerminal

from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware, settle
from hal_st_validation.groups.spis import SpisResult
from hal_st_validation.patterns import crc_text, generate

# (family, slave instance, its pins, the other instance and its pins) per board
BOARDS = {
    "stm32wb55": (2, ("PB13", "PB14", "PB15", "PB12"), 1, ("PA5", "PA6", "PA7", "PA4")),
    "stm32wba55": (3, ("PA0", "PB9", "PB8", "PA5"), 1, ("PB4", "PB3", "PA15", "PA12")),
}
SLAVE_DMA = {"stm32wb55": [("dma2", 1), ("dma2", 2)], "stm32wba55": [("dma1", 8), ("dma1", 7)]}


class Clock:
    def __init__(self) -> None:
        self.now = 100.0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


def make(family="stm32wb55"):
    clock = Clock()
    fake = FakeFirmware(family=family, clock=clock, sleep=clock.sleep)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    pins = WB55_PINS if family == "stm32wb55" else WBA55_PINS
    return terminal, fake, clock, Firmware(terminal, pins)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


def open_line(index, pins):
    clk, miso, mosi, nss = pins
    return f"spis.open {index} clk={clk} miso={miso} mosi={mosi} nss={nss}"


def open_slave(fw, family, which=0):
    index, pins, other, other_pins = BOARDS[family]
    if which:
        index, pins = other, other_pins
    fw.spis.open(index, *pins)
    return index


def open_master(fw, family, which=1):
    index, pins, other, other_pins = BOARDS[family]
    if which:
        index, pins = other, other_pins
    clk, miso, mosi, _ = pins
    fw.spi.open(index, clk=clk, mosi=mosi, miso=miso)
    return index


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("spis.open", "usage"),
        ("spis.open 2 3", "usage"),
        ("spis.open 2 cs=PB12", "usage"),
        ("spis.open x", "usage"),
        ("spis.open 4", "range"),
        ("spis.open 0 clk=PB13", "range"),
        ("spis.open 3 clk=PB13", "range"),
        ("spis.open 2 clk=PZ1 miso=PB14 mosi=PB15 nss=PB12", "pin"),
        ("spis.open 2 clk=PB13 miso=PB14 mosi=PB15", "usage"),
        ("spis.open 2 clk=PB14 miso=PB13 mosi=PB15 nss=PB12", "pin"),
        ("spis.open 2 clk=PB13 miso=PB14 mosi=PB15 nss=PA4", "pin"),
        ("spis.open 2 clk=PB13 miso=PB14 mosi=PB15 nss=PB12", "ok"),
        ("spis.open 1 clk=spi1clk miso=spi1miso mosi=spi1mosi nss=spi1nss", "ok"),
        ("spis.open 2 clk=spi2clk miso=spi2miso mosi=spi2mosi nss=spi2nss", "ok"),
    ],
)
def test_open_reasons_wb55(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("spis.open 2 clk=PA0 miso=PB9 mosi=PB8 nss=PA5", "range"),
        ("spis.open 3 clk=PA0 miso=PB9 mosi=PB8 nss=PA12", "pin"),
        ("spis.open 3 clk=spi3clk miso=spi3miso mosi=spi3mosi nss=spi3nss", "ok"),
        ("spis.open 1 clk=PB4 miso=PB3 mosi=PA15 nss=PA12", "ok"),
    ],
)
def test_open_reasons_wba55(line, expected):
    terminal, *_ = make("stm32wba55")
    assert reason(terminal, line) == expected


@pytest.mark.parametrize("family", list(BOARDS))
def test_open_holds_pins_spi_instance_and_dma_channels(family):
    terminal, fake, _, fw = make(family)
    index, pins, other, other_pins = BOARDS[family]
    fw.spis.open(index, *pins)
    owner = ("spis", str(index))
    assert all(fake.resources[key] == owner for key in [("spi", index), *SLAVE_DMA[family]])
    assert fw.open_instances == [("spis", index)]
    assert reason(terminal, open_line(other, other_pins)) == "busy"
    clk, miso, mosi, _ = pins
    assert reason(terminal, f"spi.open {index} clk={clk} mosi={mosi} miso={miso}") == "busy"
    assert reason(terminal, f"gpio.cfg {pins[3]} out") == "busy"
    fw.close_all()
    assert fake.resources == {}
    assert reason(terminal, f"spi.open {index} clk={clk} mosi={mosi} miso={miso}") == "ok"
    assert reason(terminal, open_line(index, pins)) == "busy"


@pytest.mark.parametrize("family", list(BOARDS))
def test_shared_dma_channel_is_busy(family):
    terminal, fake, *_ = make(family)
    index, pins, *_ = BOARDS[family]
    kind, channel = SLAVE_DMA[family][1]
    fake.claim_resource(kind, channel, ("adc", "x"))
    assert reason(terminal, open_line(index, pins)) == "busy"
    fake.release_resources(("adc", "x"))
    assert reason(terminal, open_line(index, pins)) == "ok"


@pytest.mark.parametrize(
    ("arguments", "expected"),
    [
        ("", "usage"),
        ("0102 3", "usage"),
        ("0102 cs=1", "usage"),
        ("-", "usage"),
        ("- rx=0", "usage"),
        ("0102 rx=3", "usage"),
        ("0102 rx=1", "usage"),
        ("012", "usage"),
        ("0102 len=2", "usage"),
        ("- pattern=inc", "usage"),
        ("- len=2 pattern=walk", "usage"),
        ("- len=0", "range"),
        ("- len=1025", "range"),
        ("- rx=1025", "range"),
        ("0102", "ok"),
        ("0102 rx=2", "ok"),
        ("0102 rx=0", "ok"),
        ("- rx=1024", "ok"),
        ("- len=1024 pattern=prbs seed=7", "ok"),
    ],
)
def test_arm_reasons(arguments, expected):
    terminal, _, _, fw = make()
    open_slave(fw, "stm32wb55")
    assert reason(terminal, f"spis.arm 2 {arguments}".rstrip()) == expected


def test_arm_needs_the_open_instance_and_answers_busy_while_armed():
    terminal, _, _, fw = make()
    assert reason(terminal, "spis.arm 2 01") == "notopen"
    open_slave(fw, "stm32wb55")
    assert reason(terminal, "spis.arm 1 01") == "notopen"
    assert reason(terminal, "spis.arm 4 01") == "range"
    fw.spis.arm(2, b"\x01")
    assert reason(terminal, "spis.arm 2 02") == "busy"
    assert reason(terminal, "spis.arm 2 0203 rx=1") == "usage"


@pytest.mark.parametrize(
    ("arguments", "expected"),
    [
        ("", "usage"),
        ("2 3", "usage"),
        ("2 rx=1", "usage"),
        ("2 out=bin", "usage"),
        ("2 wait=x", "usage"),
        ("2 wait=10001", "range"),
        ("1", "notopen"),
        ("2 wait=0 out=crc", "ok"),
    ],
)
def test_result_reasons(arguments, expected):
    terminal, _, _, fw = make()
    open_slave(fw, "stm32wb55")
    assert reason(terminal, f"spis.result {arguments}".rstrip()) == expected


def test_result_waits_only_while_armed():
    _, _, clock, fw = make()
    open_slave(fw, "stm32wb55")
    start = clock.now
    assert fw.spis.result(2) == SpisResult(False)
    assert clock.now == start
    fw.spis.arm(2, b"\x01\x02")
    assert fw.spis.result(2, wait=250) == SpisResult(False)
    assert clock.now == pytest.approx(start + 0.25)
    assert fw.spis.result(2) == SpisResult(False)
    assert clock.now == pytest.approx(start + 1.25)
    assert fw.spis.result(2, wait=0) == SpisResult(False)
    assert clock.now == pytest.approx(start + 1.25)


def test_hex_output_up_to_128_bytes():
    terminal, _, _, fw = make()
    open_slave(fw, "stm32wb55")
    fw.spis.arm(2, b"", rx=129)
    assert reason(terminal, "spis.result 2 wait=0") == "range"
    assert reason(terminal, "spis.result 2 wait=0 out=crc") == "ok"
    fw.spis.cancel(2)
    fw.spis.arm(2, b"", rx=128)
    assert reason(terminal, "spis.result 2 wait=0") == "ok"


@pytest.mark.parametrize("family", list(BOARDS))
def test_loop_full_duplex(family):
    _, _, _, fw = make(family)
    slave = open_slave(fw, family)
    master = open_master(fw, family)
    fw.spis.arm(slave, b"\x11\x22\x33\x44")
    assert fw.spi.xfer(master, b"\xa1\xa2\xa3\xa4") == b"\x11\x22\x33\x44"
    assert fw.spis.result(slave) == SpisResult(True, rx=b"\xa1\xa2\xa3\xa4")
    assert fw.spis.result(slave, out="crc") == SpisResult(True, length=4, crc=crc_text(b"\xa1\xa2\xa3\xa4"))
    assert fw.spis.cancel(slave) is False


def test_loop_in_the_other_direction_and_generated_payload():
    _, _, _, fw = make()
    slave = open_slave(fw, "stm32wb55", which=1)
    master = open_master(fw, "stm32wb55", which=0)
    fw.spis.arm(slave, len=64, pattern="prbs", seed=3)
    assert fw.spi.xfer(master, bytes(64)) == generate(64, "prbs", 3)
    assert fw.spis.result(slave) == SpisResult(True, rx=bytes(64))


def test_loop_send_only_and_receive_only():
    _, fake, _, fw = make()
    slave = open_slave(fw, "stm32wb55")
    master = open_master(fw, "stm32wb55")
    fw.spis.arm(slave, b"\x5a\xa5", rx=0)
    assert fw.spi.xfer(master, b"\x01\x02") == b"\x5a\xa5"
    assert fw.spis.result(slave) == SpisResult(True, rx=b"")
    fw.spis.arm(slave, rx=3)
    assert fw.spi.xfer(master, b"\x07\x08\x09", rx=0) == b""
    assert fw.spis.result(slave) == SpisResult(True, rx=b"\x07\x08\x09")
    fake.spi_miso = 0xFF
    assert fw.spi.xfer(master, b"\x01") == b"\xff", "no transfer armed: MISO is the static level"


def test_master_clocks_more_or_less_than_armed():
    _, fake, _, fw = make()
    fake.spi_miso = 0xEE
    slave = open_slave(fw, "stm32wb55")
    master = open_master(fw, "stm32wb55")
    fw.spis.arm(slave, b"\x01\x02\x03\x04")
    assert fw.spi.xfer(master, b"\x10\x20\x30\x40\x50\x60") == b"\x01\x02\x03\x04\xee\xee"
    assert fw.spis.result(slave) == SpisResult(True, rx=b"\x10\x20\x30\x40")
    fw.spis.arm(slave, b"\x0a\x0b\x0c\x0d")
    assert fw.spi.xfer(master, b"\x11\x12") == b"\x0a\x0b"
    assert fw.spis.result(slave, wait=0) == SpisResult(False)
    assert fw.spi.xfer(master, b"\x13\x14") == b"\x0c\x0d"
    assert fw.spis.result(slave) == SpisResult(True, rx=b"\x11\x12\x13\x14")


def test_cancel_and_close():
    terminal, _, _, fw = make()
    assert reason(terminal, "spis.cancel 2") == "notopen"
    slave = open_slave(fw, "stm32wb55")
    master = open_master(fw, "stm32wb55")
    assert reason(terminal, "spis.cancel 2 1") == "usage"
    assert fw.spis.cancel(slave) is False
    fw.spis.arm(slave, b"\x01\x02")
    fw.spi.xfer(master, b"\x03")
    assert fw.spis.cancel(slave) is True
    assert fw.spis.result(slave) == SpisResult(False)
    fw.spis.arm(slave, b"\x04")
    assert fw.spi.xfer(master, b"\x05") == b"\x04"
    fw.spis.close(slave)
    assert fw.open_instances == [("spi", master)]
    assert reason(terminal, "spis.close 2") == "notopen"
    assert fw.spi.xfer(master, b"\x05") == b"\x00"


def test_spi_loop_mapping():
    _, fake, _, fw = make()
    slave = open_slave(fw, "stm32wb55")
    master = open_master(fw, "stm32wb55")
    fw.spis.arm(slave, b"\x77")
    fake.spi_loop = {}
    assert fw.spi.xfer(master, b"\x01") == b"\x00"
    fake.spi_loop = {master: slave}
    assert fw.spi.xfer(master, b"\x01") == b"\x77"


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("spi.xfer 1", "usage"),
        ("spi.xfer 1 00 cs=1", "usage"),
        ("spi.xfer 5 00", "range"),
        ("spi.xfer 2 00", "notopen"),
        ("spi.xfer 1 0", "usage"),
        ("spi.xfer 1 -", "usage"),
        ("spi.xfer 1 " + "00" * 65, "range"),
        ("spi.xfer 1 - rx=65", "range"),
        ("spi.xfer 1 00 continue=2", "range"),
        ("spi.xfer 1 00 continue=1", "ok"),
    ],
)
def test_master_transfer_reasons_are_emil_s(line, expected):
    terminal, _, _, fw = make()
    open_master(fw, "stm32wb55", which=1)
    assert reason(terminal, line) == expected


def test_pending_result_wrapper():
    _, _, _, fw = make()
    slave = open_slave(fw, "stm32wb55")
    fw.spis.arm(slave, b"\x01")
    pending = fw.spis.begin_result(slave, wait=0)
    assert SpisResult.parse(settle(pending)) == SpisResult(False)


def test_reset_forgets_the_slave():
    terminal, fake, _, fw = make()
    open_slave(fw, "stm32wb55")
    fake.boot()
    fw.forget_open()
    assert fake.resources == {}
    assert reason(terminal, "spis.arm 2 01") == "notopen"
