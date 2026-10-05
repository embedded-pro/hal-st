"""QUADSPI (`hal::QuadSpiStm`, `hal::QuadSpiStmDma`, `hal::SingleSpeedQuadSpiStmDma`) through the `qspi` group,
NUCLEO-WB55RG only.

Wiring set `bundle1` (or `bundle2`): every QUADSPI pin is on a DIO (CLK PA3 DIO13, NCS PA2 DIO12, IO0 PB9 DIO5,
IO1 PB8 DIO3, IO2 PA7 DIO1, IO3 PA6 DIO7). The AD3 never drives a line the QUADSPI drives: writes are decoded with
the logic analyzer only; reads take their data from AD3 levels on lines the QUADSPI only receives on (IO1 in
single-line mode; IO0-IO3 in a 4-line read without instruction, address or alternate bytes, driven only between
`qspi.open` and the reply, with the weakest AD3 drive). Every case resolves the six pins through the load gate, so
`--with loopback` (PA6-PA7), `--with spiloop` (PA6, PA7) and `--with i2c` (PB8, PB9) skip them.

A 4-line read with a data phase only hangs the QUADSPI with BUSY set (the STM32 QUADSPI erratum "cannot be used in
indirect read mode when only data phase is activated"; its workaround is two dummy cycles), so the receive tests
add `dummy=2`, which keeps IO0-IO3 undriven. `SingleSpeedQuadSpiStmDma` receives with a data phase only
(`SingleSpeedQuadSpiStmDma.cpp:27`): test_xfer_receive shows whether the erratum hits it. The tests that can leave
the QUADSPI busy reset the board when they fail, so the next test starts clean.

B.15 (QuadSpiStmDma completes a write on the DMA transfer-complete, while the FIFO still drains): writes answer
`flevel`, the FIFO level sampled in the completion callback, which must be 0 (test_write_completes_after_last_byte),
and two writes issued from one completion callback must both reach the bus (test_back_to_back_writes).
"""

from __future__ import annotations

import contextlib
from collections.abc import Iterator
from ctypes import byref, c_double, c_int
from typing import Any

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation.firmware import quiesce, settle
from hal_st_validation.groups.qspi import (
    QSPI_BUFFER,
    QSPI_DUMMY_MAX,
    QSPI_HEX_MAX,
    QSPI_PRESCALER_MAX,
    QSPI_REPEAT_MAX,
    QSPI_SIZE_MAX,
    QSPI_TIMEOUT_S,
    qspi_bytes,
    qspi_clock,
    qspi_frame,
    qspi_samples,
)
from hal_st_validation.patterns import crc_text, generate

pytestmark = pytest.mark.family("stm32wb55")

READ_DUMMY_CYCLES = 2


@pytest.fixture
def qspi_cfg(board_cfg):
    return board_cfg.param("qspi")


def qspi_pins(cfg) -> dict[str, str]:
    pins = cfg["pins"]
    return {"clk": pins["clk"], "ncs": pins["ncs"], **{f"io{line}": pin for line, pin in enumerate(pins["io"])}}


@pytest.fixture
def index(need, qspi_cfg) -> int:
    """The QUADSPI drives all six pins: skip while an enabled option loads one of them."""
    need.unloaded(*qspi_pins(qspi_cfg).values())
    return qspi_cfg["index"]


def qspi_dios(need, cfg) -> dict[str, int]:
    return {name: need.dio(pin) for name, pin in qspi_pins(cfg).items()}


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


def write_frame(cfg) -> tuple[dict[str, Any], bytes]:
    """The `qspi.cmd` options of `tests.qspi.write` and the bytes the bus must carry for them."""
    write = cfg["write"]
    options = {key: write[key] for key in ("instr", "addr", "abytes", "alt", "altbytes")}
    data = bytes.fromhex(write["data"])
    frame = qspi_frame(
        write["instr"], write["addr"].to_bytes(write["abytes"], "big"), write["alt"].to_bytes(write["altbytes"], "big"), data
    )
    return {**options, "tx": data}, frame


def arm(ad3, dios, clock, duration):
    """The logic analyzer on NCS falling, at the highest rate that holds `duration` (at least 4 samples per clock)."""
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / duration)
    if rate < 4 * clock:
        pytest.skip(f"{duration * 1e6:.0f} us at {clock} Hz do not fit the logic analyzer buffer")
    return ad3.logic.arm(rate, int(duration * rate), trigger=(dios["ncs"], "falling"), pretrigger=0.02)


def frames(capture, dios):
    ios = [capture.channel(dios[f"io{line}"]) for line in range(4)]
    return qspi_samples(capture.channel(dios["clk"]), capture.channel(dios["ncs"]), ios)


def bus_seconds(length, lines, clock, frames_count=1, gap=200e-6):
    return frames_count * (length * 8 / lines / clock + gap)


@contextlib.contextmanager
def weakest_drive(ad3, dios) -> Iterator[None]:
    """The weakest AD3 output current where `FDwfDigitalIODriveInfo` offers a choice, restored afterwards; a device or
    runtime without drive control (or the `--fake` API) keeps its default."""
    saved: list[tuple[int, float, int]] = []
    api, handle = ad3.api, ad3.handle
    try:
        for dio in dios:
            with contextlib.suppress(AttributeError, RuntimeError):
                low, high, steps, slews = c_double(), c_double(), c_int(), c_int()
                api.FDwfDigitalIODriveInfo(handle, c_int(dio), byref(low), byref(high), byref(steps), byref(slews))
                if steps.value > 1:
                    amplitude, slew = c_double(), c_int()
                    api.FDwfDigitalIODriveGet(handle, c_int(dio), byref(amplitude), byref(slew))
                    api.FDwfDigitalIODriveSet(handle, c_int(dio), c_double(low.value), c_int(0))
                    saved.append((dio, amplitude.value, slew.value))
        yield
    finally:
        for dio, amplitude, slew in saved:
            with contextlib.suppress(AttributeError, RuntimeError):
                api.FDwfDigitalIODriveSet(handle, c_int(dio), c_double(amplitude), c_int(slew))
        with contextlib.suppress(AttributeError, RuntimeError):
            api.FDwfDigitalIOConfigure(handle)


@contextlib.contextmanager
def reset_on_failure(fw) -> Iterator[None]:
    """A QUADSPI left busy by a command that never completed fails every later command: reset the board."""
    try:
        yield
    except Exception:
        with contextlib.suppress(Exception):
            fw.system.reset()
        raise


@pytest.mark.board_params("variant", values=["poll", "dma", "spi"])
@pytest.mark.board_params("prescaler", values=[0, 7, QSPI_PRESCALER_MAX])
def test_open_reports_the_clock(fw, board_cfg, index, variant, prescaler):
    assert fw.qspi.open(index, variant=variant, prescaler=prescaler) == qspi_clock(board_cfg.clock("qspi"), prescaler)


def test_open_errors(fw, index):
    cases = [
        ("qspi.open", "usage"),
        ("qspi.open 1 2", "usage"),
        ("qspi.open 1 speed=1", "usage"),
        ("qspi.open x", "usage"),
        ("qspi.open 2", "range"),
        ("qspi.open 0", "range"),
        ("qspi.open 0 variant=octal", "usage"),
        ("qspi.open 1 variant=octal", "usage"),
        ("qspi.open 1 prescaler=x", "usage"),
        (f"qspi.open 1 prescaler={QSPI_PRESCALER_MAX + 1}", "range"),
        ("qspi.open 1 size=0", "range"),
        (f"qspi.open 1 size={QSPI_SIZE_MAX + 1}", "range"),
    ]
    for line, reason in cases:
        response = fw.terminal.command(line, check=False)
        assert response.reason == reason, line
    fw.qspi.open(index, variant="dma", size=QSPI_SIZE_MAX)
    expect_error("busy", fw.qspi.open, index)


def test_pins_are_held(fw, qspi_cfg, index):
    """`qspi.open` claims the six pins of the board profile: a pin held elsewhere makes it busy, and its pins are busy
    for the other groups until `qspi.close`."""
    pins = qspi_pins(qspi_cfg)
    fw.gpio.cfg(pins["io2"], "out")
    expect_error("busy", fw.qspi.open, index)
    fw.gpio.release(pins["io2"])
    fw.qspi.open(index, variant="spi")
    for pin in pins.values():
        expect_error("busy", fw.gpio.cfg, pin, "in")
    fw.qspi.close(index)
    fw.gpio.cfg(pins["clk"], "in")


def test_commands_need_an_open_instance(fw, index):
    for line in ("qspi.cmd 1 instr=0x06", "qspi.poll 1 match=1 mask=1", "qspi.xfer 1 9f", "qspi.close 1", "qspi.cmd 0"):
        assert fw.terminal.command(line, check=False).reason == "notopen", line
    for line in ("qspi.cmd 2", "qspi.close 2"):
        assert fw.terminal.command(line, check=False).reason == "range", line


def test_command_errors(fw, index):
    fw.qspi.open(index, variant="dma")
    cases = [
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
        ("qspi.cmd 1 addr=0x10000 abytes=2", "range"),
        ("qspi.cmd 1 addr=0 abytes=5", "range"),
        ("qspi.cmd 1 alt=0x100", "range"),
        (f"qspi.cmd 1 dummy={QSPI_DUMMY_MAX + 1}", "range"),
        ("qspi.cmd 1 lines=2", "range"),
        ("qspi.cmd 1 lines=0", "range"),
        ("qspi.cmd 1 tx=abc", "usage"),
        ("qspi.cmd 1 len=0", "range"),
        (f"qspi.cmd 1 len={QSPI_BUFFER + 1}", "range"),
        ("qspi.cmd 1 len=4 pattern=noise", "usage"),
        ("qspi.cmd 1 rx=0", "range"),
        (f"qspi.cmd 1 rx={QSPI_BUFFER + 1} out=crc", "range"),
        (f"qspi.cmd 1 rx={QSPI_HEX_MAX + 1}", "range"),
        ("qspi.cmd 1 rx=4 out=bin", "usage"),
        ("qspi.cmd 1 tx=00 repeat=0", "range"),
        (f"qspi.cmd 1 tx=00 repeat={QSPI_REPEAT_MAX + 1}", "range"),
    ]
    for line, reason in cases:
        assert fw.terminal.command(line, check=False).reason == reason, line


def test_poll_errors(fw, index):
    fw.qspi.open(index, variant="dma")
    cases = [
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
        ("qspi.poll 1 match=1 mask=1 lines=2", "range"),
    ]
    for line, reason in cases:
        assert fw.terminal.command(line, check=False).reason == reason, line


def test_xfer_errors(fw, index):
    """`qspi.xfer` needs `variant=spi` and exactly one of tx and rx: `SingleSpeedQuadSpiStmDma` is half duplex."""
    fw.qspi.open(index, variant="dma")
    expect_error("unsupported", fw.qspi.xfer, index, b"\x9f")
    fw.qspi.close(index)
    fw.qspi.open(index, variant="spi")
    cases = [
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
        (f"qspi.xfer 1 - rx={QSPI_HEX_MAX + 1}", "range"),
        (f"qspi.xfer 1 - len={QSPI_BUFFER + 1}", "range"),
        (f"qspi.xfer 1 9f repeat={QSPI_REPEAT_MAX + 1}", "range"),
    ]
    for line, reason in cases:
        assert fw.terminal.command(line, check=False).reason == reason, line


@pytest.mark.board_params("variant", values=["poll", "dma", "spi"])
def test_replies(fw, qspi_cfg, index, variant):
    """Writes answer `flevel=0`, reads their data (or `len` and `crc` with `out=crc`); every phase is optional. The
    reads have an instruction phase (read_instr), so they never take the data-only path of the erratum."""
    instr = qspi_cfg["read_instr"]
    fw.qspi.open(index, variant=variant)
    assert fw.qspi.cmd(index, instr=qspi_cfg["instruction"]).flevel == 0
    options, _ = write_frame(qspi_cfg)
    assert fw.qspi.cmd(index, lines=4, **options).flevel == 0
    assert fw.qspi.cmd(index, lines=1, len=QSPI_BUFFER, pattern="prbs", seed=7).flevel == 0
    data = fw.qspi.cmd(index, instr=instr, lines=1, rx=qspi_cfg["read_length"]).data
    assert data is not None and len(data) == qspi_cfg["read_length"]
    crc = fw.qspi.cmd(index, instr=instr, addr=0, lines=4, dummy=READ_DUMMY_CYCLES, rx=QSPI_BUFFER, out="crc").crc
    assert crc is not None and len(crc) == 8
    if variant == "spi":
        assert fw.qspi.xfer(index, bytes.fromhex(qspi_cfg["xfer"])).flevel == 0


@pytest.mark.board_params("variant", values=["dma", "spi"])
def test_write_completes_after_last_byte(fw, qspi_cfg, index, variant):
    """B.15: at the slowest clock the last bytes of a write take a while to leave the FIFO; the completion
    callback must see it empty."""
    fw.qspi.open(index, variant=variant, prescaler=qspi_cfg["slow_prescaler"])
    length = qspi_cfg["flevel_length"]
    if variant == "spi":
        reply = fw.qspi.xfer(index, len=length, pattern="prbs", seed=1)
    else:
        reply = fw.qspi.cmd(index, instr=qspi_cfg["instruction"], lines=1, len=length, pattern="prbs", seed=1)
    assert reply.flevel == 0


@pytest.mark.ad3
@pytest.mark.board_params("variant", values=["poll", "dma"])
@pytest.mark.board_params("lines", "qspi.lines")
def test_write_decoded(fw, ad3, need, qspi_cfg, index, variant, lines):
    """Instruction, address, alternate bytes and data of a write on the bus, all on 1 or on 4 lines."""
    dios = qspi_dios(need, qspi_cfg)
    clock = fw.qspi.open(index, variant=variant, prescaler=qspi_cfg["decode_prescaler"])
    options, expected = write_frame(qspi_cfg)
    capture = arm(ad3, dios, clock, bus_seconds(len(expected), lines, clock))
    assert fw.qspi.cmd(index, lines=lines, **options).flevel == 0
    found = frames(capture.wait(timeout=2.0), dios)
    assert found, "no chip-select frame captured"
    assert qspi_bytes(found[0], lines) == expected


@pytest.mark.ad3
@pytest.mark.resets_board
@pytest.mark.board_params("variant", values=["poll", "dma", "spi"])
def test_back_to_back_writes(fw, ad3, need, qspi_cfg, index, variant):
    """B.15: `repeat=2` issues the second write from the completion callback of the first; both reach the bus."""
    dios = qspi_dios(need, qspi_cfg)
    cfg = qspi_cfg["back_to_back"]
    clock = fw.qspi.open(index, variant=variant, prescaler=qspi_cfg["decode_prescaler"])
    payload = generate(cfg["length"], "prbs", cfg["seed"])
    instr = None if variant == "spi" else qspi_cfg["instruction"]
    expected = qspi_frame(instr, data=payload)
    capture = arm(ad3, dios, clock, bus_seconds(len(expected), 1, clock, cfg["repeat"]))
    with reset_on_failure(fw):
        if variant == "spi":
            reply = fw.qspi.xfer(index, len=cfg["length"], pattern="prbs", seed=cfg["seed"], repeat=cfg["repeat"])
        else:
            reply = fw.qspi.cmd(index, instr=instr, lines=1, len=cfg["length"], pattern="prbs", seed=cfg["seed"], repeat=cfg["repeat"])
        assert reply.flevel == 0
        found = frames(capture.wait(timeout=2.0), dios)
        assert [qspi_bytes(frame, 1) for frame in found] == [expected] * cfg["repeat"]


@pytest.mark.ad3
@pytest.mark.resets_board
@pytest.mark.board_params("variant", values=["poll", "dma"])
@pytest.mark.board_params("nibble", "qspi.nibbles")
def test_receive_nibbles(fw, ad3, need, qspi_cfg, index, variant, nibble):
    """The first command after `qspi.open` is a 4-line read without instruction, address or alternate bytes, so the
    QUADSPI drives none of IO0-IO3 while the AD3 holds them at `nibble`: every byte is the nibble twice."""
    dios = qspi_dios(need, qspi_cfg)
    ios = [dios[f"io{line}"] for line in range(4)]
    length = qspi_cfg["read_length"]
    fw.qspi.open(index, variant=variant, prescaler=qspi_cfg["decode_prescaler"])
    with reset_on_failure(fw):
        try:
            with weakest_drive(ad3, ios):
                ad3.dio.drive_many({dio: (nibble >> line) & 1 for line, dio in enumerate(ios)})
                reply = fw.qspi.cmd(index, lines=4, dummy=READ_DUMMY_CYCLES, rx=length)
        finally:
            ad3.dio.release(*ios)
        assert reply.data == bytes([nibble << 4 | nibble]) * length


@pytest.mark.ad3
@pytest.mark.board_params("variant", values=["poll", "dma"])
def test_poll_match(fw, ad3, need, qspi_cfg, index, variant):
    """A 1-line instruction followed by a 1-line status read on IO1, which the AD3 holds high: the status matches
    at once. The status is read once first, because one that never matches blocks `variant=poll` for good."""
    dios = qspi_dios(need, qspi_cfg)
    poll = qspi_cfg["poll"]
    clock = fw.qspi.open(index, variant=variant, prescaler=qspi_cfg["decode_prescaler"])
    ad3.dio.drive(dios["io1"], 1)
    status = fw.qspi.cmd(index, instr=poll["instr"], lines=1, rx=1).data
    if status != b"\xff":
        pytest.fail(f"IO1 reads {status!r} with the AD3 driving it high: check DIO3 on PB8")
    capture = arm(ad3, dios, clock, bus_seconds(2, 1, clock))
    fw.qspi.poll(index, match=poll["match"], mask=poll["mask"], instr=poll["instr"], lines=1)
    found = frames(capture.wait(timeout=2.0), dios)
    assert found and len(found[0]) >= 16, "no instruction and status on the bus"
    assert qspi_bytes(found[0][:8], 1, line=0) == bytes([poll["instr"]])
    assert qspi_bytes(found[0][8:16], 1, line=1) == b"\xff"


@pytest.mark.ad3
@pytest.mark.board_params("variant", values=["poll", "dma", "spi"])
@pytest.mark.board_params("level", values=[0, 1])
def test_read_crc(fw, ad3, need, qspi_cfg, index, variant, level):
    """A full-buffer 1-line read of IO1, which the AD3 holds at `level`, answers the CRC-32 of those bytes."""
    dios = qspi_dios(need, qspi_cfg)
    fw.qspi.open(index, variant=variant)
    ad3.dio.drive(dios["io1"], level)
    reply = fw.qspi.cmd(index, instr=qspi_cfg["read_instr"], lines=1, rx=QSPI_BUFFER, out="crc")
    assert reply.crc == crc_text(bytes([0xFF * level]) * QSPI_BUFFER)


@pytest.mark.ad3
@pytest.mark.slow
@pytest.mark.resets_board
@pytest.mark.board_params("variant", values=["dma", "poll"])
def test_poll_timeout(fw, ad3, need, qspi_cfg, index, variant):
    """A status that never matches answers `ERR timeout` after 2 s; the group stays busy until `qspi.close`, which
    stops the polling (`~QuadSpiStmDma` clears CR), and a new open works. `variant=poll` blocks instead (B.14):
    the AD3 then makes the status match, so the firmware returns."""
    dios = qspi_dios(need, qspi_cfg)
    poll = qspi_cfg["poll"]
    fw.qspi.open(index, variant=variant)
    ad3.dio.drive(dios["io1"], 0)
    with reset_on_failure(fw):
        pending = fw.qspi.begin(
            "poll",
            index,
            match=poll["match"],
            mask=poll["mask"],
            instr=poll["instr"],
            lines=1,
            cmd_timeout=fw.terminal.timeout + QSPI_TIMEOUT_S,
        )
        response = settle(pending)
        if response is None:
            ad3.dio.drive(dios["io1"], 1)
            quiesce(fw.terminal, quiet=0.5)
        assert response is not None and response.reason == "timeout", f"qspi.poll answered {response!r}"
        expect_error("busy", fw.qspi.cmd, index, instr=qspi_cfg["instruction"])
        fw.qspi.close(index)
        ad3.dio.release(dios["io1"])
        fw.qspi.open(index, variant=variant)
        assert fw.qspi.cmd(index, instr=qspi_cfg["instruction"]).flevel == 0


@pytest.mark.slow
@pytest.mark.resets_board
def test_close_recovers_from_a_data_only_read(fw, qspi_cfg, index):
    """A 4-line read with a data phase only hangs the QUADSPI with BUSY set (erratum), which only an abort or a
    reset clears: `variant=dma` answers `ERR timeout`. `qspi.close` resets the QUADSPI, so after a new open both
    drivers complete a write again (`QuadSpiStm` would otherwise wait in `HAL_QSPI_Init` and fail every command)."""
    fw.qspi.open(index, variant="dma")
    with reset_on_failure(fw):
        response = settle(fw.qspi.begin("cmd", index, lines=4, rx=4, cmd_timeout=fw.terminal.timeout + QSPI_TIMEOUT_S))
        if response is not None and response.ok:
            pytest.skip("the data-only read completed: the erratum did not hang the QUADSPI")
        assert response is not None and response.reason == "timeout", f"qspi.cmd answered {response!r}"
        fw.qspi.close(index)
        for variant in ("dma", "poll"):
            fw.qspi.open(index, variant=variant)
            assert fw.qspi.cmd(index, instr=qspi_cfg["instruction"]).flevel == 0, variant
            fw.qspi.close(index)


@pytest.mark.ad3
def test_xfer_decoded(fw, ad3, need, qspi_cfg, index):
    """`variant=spi`: `SingleSpeedQuadSpiStmDma` writes are SPI mode 0 with MOSI on IO0 and NCS as chip select."""
    dios = qspi_dios(need, qspi_cfg)
    payload = bytes.fromhex(qspi_cfg["xfer"])
    clock = fw.qspi.open(index, variant="spi", prescaler=qspi_cfg["decode_prescaler"])
    capture = arm(ad3, dios, clock, bus_seconds(len(payload), 1, clock))
    assert fw.qspi.xfer(index, payload).flevel == 0
    result = capture.wait(timeout=2.0)
    spi_frames = result.spi(dios["clk"], dios["io0"], None, dios["ncs"], 0)
    assert analysis.spi_join(spi_frames)[0] == payload
    assert analysis.clock_idle_level(result.channel(dios["clk"]), result.channel(dios["ncs"])) == 0


@pytest.mark.ad3
@pytest.mark.resets_board
@pytest.mark.board_params("level", values=[0, 1])
def test_xfer_receive(fw, ad3, need, qspi_cfg, index, level):
    """`variant=spi` receives on IO1 (MISO), which the AD3 holds at `level`; the read has a data phase only."""
    dios = qspi_dios(need, qspi_cfg)
    length = qspi_cfg["read_length"]
    fw.qspi.open(index, variant="spi", prescaler=qspi_cfg["decode_prescaler"])
    ad3.dio.drive(dios["io1"], level)
    with reset_on_failure(fw):
        assert fw.qspi.xfer(index, rx=length).data == bytes([0xFF * level]) * length
