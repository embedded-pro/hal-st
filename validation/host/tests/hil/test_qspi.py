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

Scenarios: features/qspi.feature.
"""

from __future__ import annotations

import contextlib
from collections.abc import Iterator
from ctypes import byref, c_double, c_int
from typing import Any

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

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


@pytest.fixture
def failure_resets() -> bool:
    """Whether a failing step resets the board (`reset_on_failure`); the step "any failure from here on resets the board"
    sets it for the rest of the scenario."""
    return False


def guarded(fw, failure_resets: bool) -> contextlib.AbstractContextManager[None]:
    return reset_on_failure(fw) if failure_resets else contextlib.nullcontext()


@pytest.fixture
def state():
    """What the steps of one scenario hand on to the later ones."""
    return {}


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("variant", values=["poll", "dma", "spi"])
@pytest.mark.board_params("prescaler", values=[0, 7, QSPI_PRESCALER_MAX])
@scenario("qspi.feature", "The open reports the QUADSPI clock")
def test_open_reports_the_clock(variant, prescaler):
    pass


@pytest.mark.usefixtures("index")
@scenario("qspi.feature", "Malformed and out-of-range opens are refused")
def test_open_errors():
    pass


@pytest.mark.usefixtures("index")
@scenario("qspi.feature", "The open holds the six pins")
def test_pins_are_held():
    pass


@pytest.mark.usefixtures("index")
@scenario("qspi.feature", "The commands need an open instance")
def test_commands_need_an_open_instance():
    pass


@pytest.mark.usefixtures("index")
@scenario("qspi.feature", "Malformed and out-of-range commands are refused")
def test_command_errors():
    pass


@pytest.mark.usefixtures("index")
@scenario("qspi.feature", "Malformed and out-of-range polls are refused")
def test_poll_errors():
    pass


@pytest.mark.usefixtures("index")
@scenario("qspi.feature", "Transfers need variant spi and one direction")
def test_xfer_errors():
    pass


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("variant", values=["poll", "dma", "spi"])
@scenario("qspi.feature", "Every phase is optional and every command answers its reply")
def test_replies(variant):
    pass


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("variant", values=["dma", "spi"])
@scenario("qspi.feature", "A write completes after its last byte left the FIFO")
def test_write_completes_after_last_byte(variant):
    pass


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("variant", values=["poll", "dma"])
@pytest.mark.board_params("lines", "qspi.lines")
@scenario("qspi.feature", "A write carries every phase on the bus")
def test_write_decoded(variant, lines):
    pass


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("variant", values=["poll", "dma", "spi"])
@scenario("qspi.feature", "Back-to-back writes both reach the bus")
def test_back_to_back_writes(variant):
    pass


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("variant", values=["poll", "dma"])
@pytest.mark.board_params("nibble", "qspi.nibbles")
@scenario("qspi.feature", "A 4-line read receives the nibble the AD3 holds")
def test_receive_nibbles(variant, nibble):
    pass


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("variant", values=["poll", "dma"])
@scenario("qspi.feature", "A status poll matches at once")
def test_poll_match(variant):
    pass


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("variant", values=["poll", "dma", "spi"])
@pytest.mark.board_params("level", values=[0, 1])
@scenario("qspi.feature", "A full-buffer read answers the CRC-32 of the level the AD3 holds")
def test_read_crc(variant, level):
    pass


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("variant", values=["dma", "poll"])
@scenario("qspi.feature", "A status that never matches times out and the close stops the polling")
def test_poll_timeout(variant):
    pass


@pytest.mark.usefixtures("index")
@scenario("qspi.feature", "The close recovers from a data-only read")
def test_close_recovers_from_a_data_only_read():
    pass


@pytest.mark.usefixtures("index")
@scenario("qspi.feature", "A transfer is SPI mode 0 on IO0")
def test_xfer_decoded():
    pass


@pytest.mark.usefixtures("index")
@pytest.mark.board_params("level", values=[0, 1])
@scenario("qspi.feature", "A transfer receives the level the AD3 holds on IO1")
def test_xfer_receive(level):
    pass


@given("any failure from here on resets the board", target_fixture="failure_resets")
@when("any failure from here on resets the board", target_fixture="failure_resets")
def failures_reset_the_board():
    return True


@given("the six QUADSPI pins are on DIOs", target_fixture="dios")
def pins_on_dios(need, qspi_cfg):
    return qspi_dios(need, qspi_cfg)


@when(parsers.parse("the instance opens with variant {driver:w}"))
def open_with(fw, index, driver):
    fw.qspi.open(index, variant=driver)


@when("the instance opens with variant dma and the largest size")
def open_largest(fw, index):
    fw.qspi.open(index, variant="dma", size=QSPI_SIZE_MAX)


@when(parsers.parse("the instance opens with variant {driver:w} at the decode prescaler"), target_fixture="clock")
def open_with_at_decode(fw, qspi_cfg, index, driver):
    return fw.qspi.open(index, variant=driver, prescaler=qspi_cfg["decode_prescaler"])


@when("the instance opens with the variant")
def open_variant(fw, index, variant, failure_resets):
    with guarded(fw, failure_resets):
        fw.qspi.open(index, variant=variant)


@when("the instance opens with the variant at the decode prescaler", target_fixture="clock")
def open_variant_at_decode(fw, qspi_cfg, index, variant):
    return fw.qspi.open(index, variant=variant, prescaler=qspi_cfg["decode_prescaler"])


@when("the instance opens with the variant at the slow prescaler")
def open_variant_at_slow(fw, qspi_cfg, index, variant):
    fw.qspi.open(index, variant=variant, prescaler=qspi_cfg["slow_prescaler"])


@when("the instance closes")
def close(fw, index, failure_resets):
    with guarded(fw, failure_resets):
        fw.qspi.close(index)


@then("opening the instance with the variant and the prescaler reports the QUADSPI kernel clock over the prescaler plus one")
def open_reports_clock(fw, board_cfg, index, variant, prescaler):
    assert fw.qspi.open(index, variant=variant, prescaler=prescaler) == qspi_clock(board_cfg.clock("qspi"), prescaler)


@then(parsers.parse('opening the instance fails with "{reason}"'))
def open_refused(fw, index, reason):
    expect_error(reason, fw.qspi.open, index)


@then("every malformed or out-of-range open answers its reason")
def opens_refused(fw):
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


@given("IO2 is configured as a GPIO output")
def io2_output(fw, qspi_cfg):
    fw.gpio.cfg(qspi_pins(qspi_cfg)["io2"], "out")


@when("IO2 is released")
def io2_released(fw, qspi_cfg):
    fw.gpio.release(qspi_pins(qspi_cfg)["io2"])


@then(parsers.parse('configuring any of the six pins as a GPIO input fails with "{reason}"'))
def pins_busy(fw, qspi_cfg, reason):
    for pin in qspi_pins(qspi_cfg).values():
        expect_error(reason, fw.gpio.cfg, pin, "in")


@then("CLK can be configured as a GPIO input")
def clk_input(fw, qspi_cfg):
    fw.gpio.cfg(qspi_pins(qspi_cfg)["clk"], "in")


@then(parsers.parse('every command on the closed instance answers "{reason}"'))
def commands_not_open(fw, reason):
    for line in ("qspi.cmd 1 instr=0x06", "qspi.poll 1 match=1 mask=1", "qspi.xfer 1 9f", "qspi.close 1", "qspi.cmd 0"):
        assert fw.terminal.command(line, check=False).reason == reason, line


@then(parsers.parse('every command on instance 2 answers "{reason}"'))
def commands_out_of_range(fw, reason):
    for line in ("qspi.cmd 2", "qspi.close 2"):
        assert fw.terminal.command(line, check=False).reason == reason, line


@then("every malformed or out-of-range command answers its reason")
def commands_refused(fw):
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


@then("every malformed or out-of-range poll answers its reason")
def polls_refused(fw):
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


@then(parsers.parse('a transfer of 9f fails with "{reason}"'))
def xfer_refused(fw, index, reason):
    expect_error(reason, fw.qspi.xfer, index, b"\x9f")


@then("every malformed or out-of-range transfer answers its reason")
def xfers_refused(fw):
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


@then("a write of the instruction alone answers flevel 0")
def instruction_write(fw, qspi_cfg, index, failure_resets):
    with guarded(fw, failure_resets):
        assert fw.qspi.cmd(index, instr=qspi_cfg["instruction"]).flevel == 0


@then(parsers.parse('a write of the instruction alone fails with "{reason}"'))
def instruction_write_refused(fw, qspi_cfg, index, failure_resets, reason):
    with guarded(fw, failure_resets):
        expect_error(reason, fw.qspi.cmd, index, instr=qspi_cfg["instruction"])


@then(parsers.parse("the write of the board file on {write_lines:d} lines answers flevel 0"))
def board_write_on(fw, qspi_cfg, index, write_lines):
    options, _ = write_frame(qspi_cfg)
    assert fw.qspi.cmd(index, lines=write_lines, **options).flevel == 0


@then(parsers.parse("a full-buffer 1-line PRBS write with seed {seed:d} answers flevel 0"))
def prbs_write(fw, index, seed):
    assert fw.qspi.cmd(index, lines=1, len=QSPI_BUFFER, pattern="prbs", seed=seed).flevel == 0


@then("a 1-line read with the read instruction answers the read length of data")
def read_data(fw, qspi_cfg, index):
    data = fw.qspi.cmd(index, instr=qspi_cfg["read_instr"], lines=1, rx=qspi_cfg["read_length"]).data
    assert data is not None and len(data) == qspi_cfg["read_length"]


@then("a full-buffer 4-line read with the read instruction, address 0 and two dummy cycles answers an 8-digit CRC")
def read_crc_reply(fw, qspi_cfg, index):
    crc = fw.qspi.cmd(index, instr=qspi_cfg["read_instr"], addr=0, lines=4, dummy=READ_DUMMY_CYCLES, rx=QSPI_BUFFER, out="crc").crc
    assert crc is not None and len(crc) == 8


@then("with variant spi, a transfer of the transfer bytes answers flevel 0")
def spi_xfer_reply(fw, qspi_cfg, index, variant):
    if variant == "spi":
        assert fw.qspi.xfer(index, bytes.fromhex(qspi_cfg["xfer"])).flevel == 0


@when(
    "a 1-line PRBS write with seed 1 of the FIFO-level length is made, as a transfer with variant spi and after the instruction otherwise",
    target_fixture="reply",
)
def flevel_write(fw, qspi_cfg, index, variant):
    length = qspi_cfg["flevel_length"]
    if variant == "spi":
        return fw.qspi.xfer(index, len=length, pattern="prbs", seed=1)
    return fw.qspi.cmd(index, instr=qspi_cfg["instruction"], lines=1, len=length, pattern="prbs", seed=1)


@then("the write answers flevel 0")
def write_flevel(reply):
    assert reply.flevel == 0


@when("the logic analyzer is armed on NCS falling for the write of the board file on the lines", target_fixture="capture")
def arm_for_board_write(ad3, qspi_cfg, dios, clock, lines):
    _, expected = write_frame(qspi_cfg)
    return arm(ad3, dios, clock, bus_seconds(len(expected), lines, clock))


@then("the write of the board file on the lines answers flevel 0")
def board_write(fw, qspi_cfg, index, lines):
    options, _ = write_frame(qspi_cfg)
    assert fw.qspi.cmd(index, lines=lines, **options).flevel == 0


@then("the first chip-select frame captured carries the instruction, address, alternate bytes and data of the write on the lines")
def board_write_decoded(qspi_cfg, dios, capture, lines):
    _, expected = write_frame(qspi_cfg)
    found = frames(capture.wait(timeout=2.0), dios)
    assert found, "no chip-select frame captured"
    assert qspi_bytes(found[0], lines) == expected


@when(
    "the logic analyzer is armed on NCS falling for the back-to-back 1-line PRBS writes, after the instruction unless the variant is spi",
    target_fixture="capture",
)
def arm_for_back_to_back(ad3, qspi_cfg, dios, clock, variant, state):
    cfg = qspi_cfg["back_to_back"]
    payload = generate(cfg["length"], "prbs", cfg["seed"])
    instr = None if variant == "spi" else qspi_cfg["instruction"]
    expected = qspi_frame(instr, data=payload)
    state.update(instr=instr, expected=expected)
    return arm(ad3, dios, clock, bus_seconds(len(expected), 1, clock, cfg["repeat"]))


@when("the back-to-back writes are issued, as transfers with variant spi and as commands otherwise", target_fixture="reply")
def back_to_back(fw, qspi_cfg, index, variant, state, failure_resets):
    cfg = qspi_cfg["back_to_back"]
    with guarded(fw, failure_resets):
        if variant == "spi":
            return fw.qspi.xfer(index, len=cfg["length"], pattern="prbs", seed=cfg["seed"], repeat=cfg["repeat"])
        return fw.qspi.cmd(index, instr=state["instr"], lines=1, len=cfg["length"], pattern="prbs", seed=cfg["seed"], repeat=cfg["repeat"])


@then("the writes answer flevel 0")
def writes_flevel(fw, reply, failure_resets):
    with guarded(fw, failure_resets):
        assert reply.flevel == 0


@then("every chip-select frame captured carries the instruction and the payload, once per repeat")
def back_to_back_decoded(fw, qspi_cfg, dios, capture, state, failure_resets):
    with guarded(fw, failure_resets):
        found = frames(capture.wait(timeout=2.0), dios)
        assert [qspi_bytes(frame, 1) for frame in found] == [state["expected"]] * qspi_cfg["back_to_back"]["repeat"]


@when(
    "a 4-line read of the read length with two dummy cycles runs while the AD3 holds IO0-IO3 at the nibble with its weakest drive, "
    "releasing them afterwards",
    target_fixture="reply",
)
def read_nibble(fw, ad3, qspi_cfg, index, dios, nibble, failure_resets):
    ios = [dios[f"io{line}"] for line in range(4)]
    with guarded(fw, failure_resets):
        try:
            with weakest_drive(ad3, ios):
                ad3.dio.drive_many({dio: (nibble >> line) & 1 for line, dio in enumerate(ios)})
                return fw.qspi.cmd(index, lines=4, dummy=READ_DUMMY_CYCLES, rx=qspi_cfg["read_length"])
        finally:
            ad3.dio.release(*ios)


@then("every byte read is the nibble twice")
def nibble_twice(fw, qspi_cfg, reply, nibble, failure_resets):
    with guarded(fw, failure_resets):
        assert reply.data == bytes([nibble << 4 | nibble]) * qspi_cfg["read_length"]


@when(parsers.parse("the AD3 holds IO1 at {io1_level:d}"))
def hold_io1_at(ad3, dios, io1_level):
    ad3.dio.drive(dios["io1"], io1_level)


@when("the AD3 holds IO1 at the level")
def hold_io1_level(ad3, dios, level):
    ad3.dio.drive(dios["io1"], level)


@when("the AD3 releases IO1")
def release_io1(fw, ad3, dios, failure_resets):
    with guarded(fw, failure_resets):
        ad3.dio.release(dios["io1"])


@then("a 1-line read of one status byte with the poll instruction reads ff")
def status_reads_high(fw, qspi_cfg, index):
    status = fw.qspi.cmd(index, instr=qspi_cfg["poll"]["instr"], lines=1, rx=1).data
    if status != b"\xff":
        pytest.fail(f"IO1 reads {status!r} with the AD3 driving it high: check DIO3 on PB8")


@when(parsers.parse("the logic analyzer is armed on NCS falling for {length:d} bytes on 1 line"), target_fixture="capture")
def arm_for_bytes(ad3, dios, clock, length):
    return arm(ad3, dios, clock, bus_seconds(length, 1, clock))


@when("the instance polls for the status match with the poll instruction on 1 line")
def poll_match(fw, qspi_cfg, index):
    poll = qspi_cfg["poll"]
    fw.qspi.poll(index, match=poll["match"], mask=poll["mask"], instr=poll["instr"], lines=1)


@then("the first chip-select frame captured carries the poll instruction on IO0 and the status ff on IO1")
def poll_decoded(qspi_cfg, dios, capture):
    found = frames(capture.wait(timeout=2.0), dios)
    assert found and len(found[0]) >= 16, "no instruction and status on the bus"
    assert qspi_bytes(found[0][:8], 1, line=0) == bytes([qspi_cfg["poll"]["instr"]])
    assert qspi_bytes(found[0][8:16], 1, line=1) == b"\xff"


@then("a full-buffer 1-line read with the read instruction answers the CRC-32 of bytes of the level")
def read_crc_of_level(fw, qspi_cfg, index, level):
    reply = fw.qspi.cmd(index, instr=qspi_cfg["read_instr"], lines=1, rx=QSPI_BUFFER, out="crc")
    assert reply.crc == crc_text(bytes([0xFF * level]) * QSPI_BUFFER)


@when(
    "a 1-line status poll with the poll instruction runs, the AD3 driving IO1 high and the line going quiet if no answer comes",
    target_fixture="response",
)
def poll_until_timeout(fw, ad3, qspi_cfg, index, dios, failure_resets):
    poll = qspi_cfg["poll"]
    with guarded(fw, failure_resets):
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
        return response


@then(parsers.parse('the poll answers "{reason}"'))
def poll_answers(fw, response, failure_resets, reason):
    with guarded(fw, failure_resets):
        assert response is not None and response.reason == reason, f"qspi.poll answered {response!r}"


@when("a 4-line read of 4 bytes with a data phase only runs", target_fixture="response")
def data_only_read(fw, index, failure_resets):
    with guarded(fw, failure_resets):
        return settle(fw.qspi.begin("cmd", index, lines=4, rx=4, cmd_timeout=fw.terminal.timeout + QSPI_TIMEOUT_S))


@then(parsers.parse('the read answers "{reason}", unless it completed and the scenario skips'))
def data_only_read_times_out(fw, response, failure_resets, reason):
    with guarded(fw, failure_resets):
        if response is not None and response.ok:
            pytest.skip("the data-only read completed: the erratum did not hang the QUADSPI")
        assert response is not None and response.reason == reason, f"qspi.cmd answered {response!r}"


@then("with variant dma and then poll, a new open completes a write of the instruction alone and closes")
def reopen_and_write(fw, qspi_cfg, index, failure_resets):
    with guarded(fw, failure_resets):
        for variant in ("dma", "poll"):
            fw.qspi.open(index, variant=variant)
            assert fw.qspi.cmd(index, instr=qspi_cfg["instruction"]).flevel == 0, variant
            fw.qspi.close(index)


@when("the logic analyzer is armed on NCS falling for the transfer bytes on 1 line", target_fixture="capture")
def arm_for_xfer(ad3, qspi_cfg, dios, clock):
    return arm(ad3, dios, clock, bus_seconds(len(bytes.fromhex(qspi_cfg["xfer"])), 1, clock))


@then("a transfer of the transfer bytes answers flevel 0")
def xfer_bytes(fw, qspi_cfg, index):
    assert fw.qspi.xfer(index, bytes.fromhex(qspi_cfg["xfer"])).flevel == 0


@then("the capture decodes as SPI mode 0 with MOSI on IO0 and NCS as chip select to the transfer bytes, the clock idling low")
def xfer_decoded(qspi_cfg, dios, capture):
    result = capture.wait(timeout=2.0)
    spi_frames = result.spi(dios["clk"], dios["io0"], None, dios["ncs"], 0)
    assert analysis.spi_join(spi_frames)[0] == bytes.fromhex(qspi_cfg["xfer"])
    assert analysis.clock_idle_level(result.channel(dios["clk"]), result.channel(dios["ncs"])) == 0


@then("a transfer receiving the read length reads bytes of the level")
def xfer_receive(fw, qspi_cfg, index, level, failure_resets):
    length = qspi_cfg["read_length"]
    with guarded(fw, failure_resets):
        assert fw.qspi.xfer(index, rx=length).data == bytes([0xFF * level]) * length
