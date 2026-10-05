"""The fake `qspi` group (argument order and reasons of validation/firmware/QuadSpiGroup.cpp; PROTOCOL.md D.20), the
pins and DMA channel it holds, its bus model, the `groups.qspi` wrappers and the decoding of QUADSPI captures."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.fakes.qspi import FakeQuadSpi
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups import qspi
from hal_st_validation.patterns import crc_text, generate


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


def bus(fake) -> FakeQuadSpi:
    group = fake.group("qspi")
    assert isinstance(group, FakeQuadSpi)
    return group


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("qspi.open", "usage"),
        ("qspi.open 1 2", "usage"),
        ("qspi.open 1 speed=1", "usage"),
        ("qspi.open x", "usage"),
        ("qspi.open 2", "range"),
        ("qspi.open 0", "range"),
        ("qspi.open 0 variant=octal", "usage"),
        ("qspi.open 1 variant=octal", "usage"),
        ("qspi.open 1 prescaler=x", "usage"),
        ("qspi.open 1 prescaler=256", "range"),
        ("qspi.open 1 prescaler=256 variant=octal", "usage"),
        ("qspi.open 1 size=0", "range"),
        ("qspi.open 1 size=33", "range"),
        ("qspi.open 1", "ok"),
        ("qspi.open 1 variant=spi prescaler=255 size=32", "ok"),
    ],
)
def test_open_reasons(line, expected):
    terminal, *_ = make()
    assert reason(terminal, line) == expected


def test_open_reports_the_clock_and_tracks():
    terminal, fake, _, fw = make()
    assert fw.qspi.open(1, variant="dma", prescaler=7) == 8_000_000
    assert fake.received[-1] == "qspi.open 1 variant=dma prescaler=7"
    assert fw.open_instances == [("qspi", 1)]
    assert reason(terminal, "qspi.open 1") == "busy"
    fw.close_all()
    assert fake.received[-1] == "qspi.close 1"
    assert fw.qspi.open(1) == qspi.qspi_clock(64_000_000, 2) == 21_333_333


@pytest.mark.parametrize("pin", ["PA3", "PA2", "PB9", "PB8", "PA7", "PA6"])
def test_open_holds_the_pins(pin):
    terminal, _, _, fw = make()
    fw.gpio.cfg(pin, "out")
    assert reason(terminal, "qspi.open 1") == "busy"
    fw.gpio.release(pin)
    fw.qspi.open(1)
    assert reason(terminal, f"gpio.cfg {pin} in") == "busy"
    fw.qspi.close(1)
    assert reason(terminal, f"gpio.cfg {pin} in") == "ok"


@pytest.mark.parametrize(("variant", "claimed"), [("poll", False), ("dma", True), ("spi", True)])
def test_dma_channel_held_by_the_dma_variants(variant, claimed):
    _, fake, _, fw = make()
    fw.qspi.open(1, variant=variant)
    assert (("dma2", 3) in fake.resources) == claimed
    fw.qspi.close(1)
    assert ("dma2", 3) not in fake.resources


def test_wba55_has_no_quadspi():
    terminal, *_ = make("stm32wba55")
    for line in ("qspi.open 1", "qspi.cmd 1 instr=0x9f rx=3", "qspi.poll 1 match=1 mask=1", "qspi.xfer 1 9f", "qspi.close 1"):
        assert reason(terminal, line) == "unsupported", line


def test_commands_need_an_open_instance():
    terminal, *_ = make()
    for line in ("qspi.cmd 1 instr=0x06", "qspi.poll 1 match=1 mask=1", "qspi.xfer 1 9f", "qspi.close 1", "qspi.cmd 0"):
        assert reason(terminal, line) == "notopen", line
    for line in ("qspi.cmd 2", "qspi.close 2", "qspi.cmd x"):
        assert reason(terminal, line) == ("usage" if line.endswith("x") else "range"), line


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("qspi.cmd 1", "ok"),
        ("qspi.cmd 1 2", "usage"),
        ("qspi.cmd 1 speed=1", "usage"),
        ("qspi.cmd 1 tx=00 rx=1", "usage"),
        ("qspi.cmd 1 tx=00 len=1", "usage"),
        ("qspi.cmd 1 len=1 rx=1", "usage"),
        ("qspi.cmd 1 pattern=inc", "usage"),
        ("qspi.cmd 1 tx=00 seed=1", "usage"),
        ("qspi.cmd 1 tx=00 out=crc", "usage"),
        ("qspi.cmd 1 instr=0x03 rx=1 repeat=2", "usage"),
        ("qspi.cmd 1 abytes=2", "usage"),
        ("qspi.cmd 1 altbytes=2", "usage"),
        ("qspi.cmd 1 lines=3 tx=00 rx=1", "usage"),
        ("qspi.cmd 1 instr=x", "usage"),
        ("qspi.cmd 1 instr=0x100", "range"),
        ("qspi.cmd 1 instr=0x100 dummy=x", "range"),
        ("qspi.cmd 1 addr=0x10000 abytes=2", "range"),
        ("qspi.cmd 1 addr=0xffff abytes=2", "ok"),
        ("qspi.cmd 1 addr=0xffffffff abytes=4", "ok"),
        ("qspi.cmd 1 addr=0 abytes=5", "range"),
        ("qspi.cmd 1 alt=0x100", "range"),
        ("qspi.cmd 1 alt=0x100 altbytes=2", "ok"),
        ("qspi.cmd 1 dummy=32", "range"),
        ("qspi.cmd 1 dummy=31", "ok"),
        ("qspi.cmd 1 lines=2", "range"),
        ("qspi.cmd 1 lines=0", "range"),
        ("qspi.cmd 1 lines=4 tx=00", "ok"),
        ("qspi.cmd 1 tx=abc", "usage"),
        ("qspi.cmd 1 tx=-", "ok"),
        ("qspi.cmd 1 len=0", "range"),
        ("qspi.cmd 1 len=257", "range"),
        ("qspi.cmd 1 len=4 pattern=noise", "usage"),
        ("qspi.cmd 1 len=4 seed=x pattern=noise", "usage"),
        ("qspi.cmd 1 len=256 pattern=prbs seed=0xffffffff", "ok"),
        ("qspi.cmd 1 rx=0", "range"),
        ("qspi.cmd 1 rx=257 out=crc", "range"),
        ("qspi.cmd 1 rx=256 out=crc", "ok"),
        ("qspi.cmd 1 rx=129", "range"),
        ("qspi.cmd 1 rx=128", "ok"),
        ("qspi.cmd 1 rx=4 out=bin", "usage"),
        ("qspi.cmd 1 tx=00 repeat=0", "range"),
        ("qspi.cmd 1 tx=00 repeat=9", "range"),
        ("qspi.cmd 1 len=8 repeat=8", "ok"),
    ],
)
def test_command_reasons(line, expected):
    terminal, _, _, fw = make()
    fw.qspi.open(1, variant="dma")
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("qspi.poll 1", "usage"),
        ("qspi.poll 1 match=1", "usage"),
        ("qspi.poll 1 mask=1", "usage"),
        ("qspi.poll 1 match=1 mask=1 tx=00", "usage"),
        ("qspi.poll 1 match=1 mask=1 abytes=1", "usage"),
        ("qspi.poll 1 match=x mask=1", "usage"),
        ("qspi.poll 1 match=1 mask=1 size=0", "range"),
        ("qspi.poll 1 match=1 mask=1 size=5", "range"),
        ("qspi.poll 1 match=0x100 mask=1", "range"),
        ("qspi.poll 1 match=1 mask=0x1ff size=1", "range"),
        ("qspi.poll 1 match=0x1ff mask=0x1ff size=2", "ok"),
        ("qspi.poll 1 match=1 mask=1 lines=2", "range"),
        ("qspi.poll 1 match=1 mask=1 instr=0x05", "ok"),
    ],
)
def test_poll_reasons(line, expected):
    terminal, _, _, fw = make()
    fw.qspi.open(1, variant="dma")
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("qspi.xfer 1", "usage"),
        ("qspi.xfer 1 -", "usage"),
        ("qspi.xfer 1 9f rx=1", "usage"),
        ("qspi.xfer 1 - len=2 rx=1", "usage"),
        ("qspi.xfer 1 - rx=1 repeat=2", "usage"),
        ("qspi.xfer 1 9f tx=1", "usage"),
        ("qspi.xfer 1 9f len=1", "usage"),
        ("qspi.xfer 1 - pattern=inc rx=1", "usage"),
        ("qspi.xfer 1 9", "usage"),
        ("qspi.xfer 1 - rx=0", "range"),
        ("qspi.xfer 1 - rx=129", "range"),
        ("qspi.xfer 1 - len=257", "range"),
        ("qspi.xfer 1 9f repeat=9", "range"),
        ("qspi.xfer 1 9f", "ok"),
        ("qspi.xfer 1 - len=256 pattern=const seed=0xa5 repeat=8", "ok"),
        ("qspi.xfer 1 - rx=128", "ok"),
    ],
)
def test_xfer_reasons(line, expected):
    terminal, _, _, fw = make()
    fw.qspi.open(1, variant="spi")
    assert reason(terminal, line) == expected


@pytest.mark.parametrize("variant", ["poll", "dma"])
def test_xfer_needs_the_spi_variant(variant):
    terminal, _, _, fw = make()
    fw.qspi.open(1, variant=variant)
    assert reason(terminal, "qspi.xfer 1 9f") == "unsupported"
    assert reason(terminal, "qspi.xfer 1 9f rx=1") == "usage"


def test_wrappers_format_and_parse():
    _, fake, _, fw = make()
    fw.qspi.open(1, variant="spi")
    reply = fw.qspi.cmd(1, instr=0x38, addr=0x123456, abytes=3, alt=0xA55A, altbytes=2, lines=4, tx=b"\x00\xff")
    assert fake.received[-1] == "qspi.cmd 1 instr=56 addr=1193046 abytes=3 alt=42330 altbytes=2 lines=4 tx=00ff"
    assert reply == qspi.QuadSpiReply(flevel=0)
    assert fw.qspi.cmd(1, len=8, pattern="prbs", seed=1, repeat=2) == qspi.QuadSpiReply(flevel=0)
    assert fake.received[-1] == "qspi.cmd 1 len=8 pattern=prbs seed=1 repeat=2"
    assert fw.qspi.cmd(1, instr=0x0B, rx=3) == qspi.QuadSpiReply(data=b"\xff\xff\xff")
    assert fw.qspi.cmd(1, instr=0x0B, rx=256, out="crc") == qspi.QuadSpiReply(crc=crc_text(b"\xff" * 256))
    fw.qspi.poll(1, match=1, mask=1, instr=5, lines=1)
    assert fake.received[-1] == "qspi.poll 1 instr=5 lines=1 match=1 mask=1"
    assert fw.qspi.xfer(1, b"\x9f\x01") == qspi.QuadSpiReply(flevel=0)
    assert fake.received[-1] == "qspi.xfer 1 9f01"
    assert fw.qspi.xfer(1, len=4, repeat=2).flevel == 0
    assert fake.received[-1] == "qspi.xfer 1 - len=4 repeat=2"
    assert fw.qspi.xfer(1, rx=2) == qspi.QuadSpiReply(data=b"\xff\xff")
    assert fake.received[-1] == "qspi.xfer 1 - rx=2"


@pytest.mark.parametrize(("io", "lines", "byte"), [(0x0, 4, 0x00), (0x5, 4, 0x55), (0xA, 4, 0xAA), (0x2, 1, 0xFF), (0xD, 1, 0x00)])
def test_reads_follow_the_io_levels(io, lines, byte):
    _, fake, _, fw = make()
    fw.qspi.open(1, variant="dma")
    bus(fake).io = io
    assert fw.qspi.cmd(1, lines=lines, dummy=2, rx=4).data == bytes([byte]) * 4


def test_spi_reads_follow_io1():
    _, fake, _, fw = make()
    fw.qspi.open(1, variant="spi")
    bus(fake).io = 0b1101
    assert fw.qspi.xfer(1, rx=2).data == b"\x00\x00"
    bus(fake).io = 0b0010
    assert fw.qspi.xfer(1, rx=2).data == b"\xff\xff"


@pytest.mark.parametrize("variant", ["poll", "dma"])
def test_poll_timeout_keeps_the_group_busy_until_close(variant):
    terminal, fake, clock, fw = make()
    fw.qspi.open(1, variant=variant)
    fw.qspi.poll(1, match=0x01, mask=0x01, lines=1)
    bus(fake).io = 0b0000
    start = clock.now
    with pytest.raises(FirmwareError) as error:
        fw.qspi.poll(1, match=0x01, mask=0x01, lines=1)
    assert error.value.reason == "timeout"
    assert clock.now - start == qspi.QSPI_TIMEOUT_S
    assert reason(terminal, "qspi.cmd 1 instr=0x06") == "busy"
    assert reason(terminal, "qspi.cmd 1 instr=0x100") == "range"
    fw.qspi.close(1)
    fw.qspi.open(1, variant=variant)
    assert fw.qspi.cmd(1, instr=0x06).flevel == 0
    fw.qspi.poll(1, match=0x00, mask=0x01, size=2, lines=4)


def test_boot_resets_the_bus_and_the_instance():
    terminal, fake, _, fw = make()
    fw.qspi.open(1)
    bus(fake).io = 0
    fw.system.reset()
    fw.forget_open()
    assert bus(fake).io == 0xF
    assert reason(terminal, "qspi.cmd 1") == "notopen"


def test_qspi_clock_and_frame():
    assert qspi.qspi_clock(64_000_000, 0) == 64_000_000
    assert qspi.qspi_clock(64_000_000, 255) == 250_000
    assert qspi.qspi_frame(0x38, b"\x12\x34", b"\xa5", b"\x00") == b"\x38\x12\x34\xa5\x00"
    assert qspi.qspi_frame(data=b"\x01") == b"\x01"


def synthesize(frames: list[bytes], lines: int, half: int = 3, gap: int = 7) -> tuple[list[int], list[int], list[list[int]]]:
    """CLK, NCS and IO0-IO3 samples of QUADSPI writes in clock mode 0: each bit (1 line, on IO0) or nibble (4 lines)
    is set while CLK is low and sampled on the rising edge; NCS is high for `gap` samples around each frame."""
    clk: list[int] = []
    ncs: list[int] = []
    ios: list[list[int]] = [[], [], [], []]

    def sample(clock: int, select: int, nibble: int) -> None:
        clk.append(clock)
        ncs.append(select)
        for line in range(4):
            ios[line].append(nibble >> line & 1)

    for frame in frames:
        for _ in range(gap):
            sample(0, 1, 0)
        units = (
            [byte >> shift & 0xF for byte in frame for shift in (4, 0)]
            if lines == 4
            else [byte >> (7 - bit) & 1 for byte in frame for bit in range(8)]
        )
        for unit in units:
            for _ in range(half):
                sample(0, 0, unit)
            for _ in range(half):
                sample(1, 0, unit)
        for _ in range(half):
            sample(0, 0, 0)
    for _ in range(gap):
        sample(0, 1, 0)
    return clk, ncs, ios


@pytest.mark.parametrize("lines", [1, 4])
def test_decode_writes(lines):
    frames = [qspi.qspi_frame(0x38, b"\x12\x34\x56", b"\xa5\x5a", b"\x00\xff\x5a\xa5\xc3"), generate(32, "prbs", 1)]
    clk, ncs, ios = synthesize(frames, lines)
    samples = qspi.qspi_samples(clk, ncs, ios)
    assert len(samples) == 2
    assert [qspi.qspi_bytes(frame, lines) for frame in samples] == frames


def test_decode_one_line_on_io1_and_partial_bytes():
    nibbles = [0b0010] * 8 + [0b0000] * 8 + [0b0010] * 3
    assert qspi.qspi_bytes(nibbles, 1, line=1) == b"\xff\x00"
    assert qspi.qspi_bytes(nibbles, 1, line=0) == b"\x00\x00"
    assert qspi.qspi_bytes([0x5, 0xA, 0xF], 4) == b"\x5a"
    with pytest.raises(ValueError):
        qspi.qspi_bytes([0, 0], 2)


def test_decode_ignores_edges_outside_the_chip_select():
    clk = [0, 1, 0, 1, 0, 1, 0, 1, 0]
    ncs = [1, 1, 1, 0, 0, 0, 0, 1, 1]
    ios = [[1] * 9, [0] * 9, [0] * 9, [0] * 9]
    assert qspi.qspi_samples(clk, ncs, ios) == [[1, 1]]
    assert qspi.qspi_samples([0, 1, 0], [1, 1, 1], ios) == []
