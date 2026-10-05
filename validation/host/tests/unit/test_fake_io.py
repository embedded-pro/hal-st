"""The fake `sgpio` and `clock` groups (argument order and reasons of validation/firmware/SyncGpioGroup.cpp and
ClockGroup.cpp; PROTOCOL.md D.14, D.15), the send-only UART checks of `uart.open sendonly=1` (D.13), the
`groups.io` wrappers and the clock and UID expectations."""

import math

import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.fakes.io import FAKE_UID
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups.io import UID_BYTES, edge_fit_frequency, parse_uid


def make(family="stm32wb55", **fake_options):
    fake = FakeFirmware(family=family, **fake_options)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    pins = WB55_PINS if family == "stm32wb55" else WBA55_PINS
    return terminal, fake, Firmware(terminal, pins)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("sgpio.out", "usage"),
        ("sgpio.out PA8", "usage"),
        ("sgpio.out PA8 1 2", "usage"),
        ("sgpio.out PA8 1 pull=up", "usage"),
        ("sgpio.out PA8 x", "usage"),
        ("sgpio.out PA8 2", "range"),
        ("sgpio.out PA8 1 od=2", "range"),
        ("sgpio.out PA8 1 speed=fastest", "usage"),
        ("sgpio.out PZ8 2", "range"),
        ("sgpio.out PZ8 1", "pin"),
        ("sgpio.out PC7 1", "pin"),
        ("sgpio.out terminaltx 1", "busy"),
        ("sgpio.out PB5 1", "busy"),
        ("sgpio.out PA8 1 od=1 speed=high", "ok"),
        ("sgpio.latch PA8", "notopen"),
        ("sgpio.latch", "usage"),
        ("sgpio.release PA8", "notopen"),
        ("sgpio.af PA15", "usage"),
        ("sgpio.af PA15 timer=2 af=1", "usage"),
        ("sgpio.af PA15 af=1 ch=1", "usage"),
        ("sgpio.af PA15 timer=0", "range"),
        ("sgpio.af PA15 timer=18", "range"),
        ("sgpio.af PA15 timer=3", "range"),
        ("sgpio.af PA15 timer=2 ch=5", "range"),
        ("sgpio.af PA15 af=16", "range"),
        ("sgpio.af PZ1 timer=2", "pin"),
        ("sgpio.af PC6 timer=2", "pin"),
        ("sgpio.af PA15 timer=2 ch=2", "pin"),
        ("sgpio.af PC7 af=1", "pin"),
        ("sgpio.af terminalrx af=7", "busy"),
        ("sgpio.af PA15 timer=2", "ok"),
        ("sgpio.af PA1 timer=2 ch=2", "ok"),
        ("sgpio.af PA6 timer=16 ch=1", "ok"),
        ("sgpio.af PA8 af=0", "ok"),
        ("sgpio.multi PA15", "usage"),
        ("sgpio.multi , timer=2", "usage"),
        ("sgpio.multi PA15,PA15 timer=2", "usage"),
        ("sgpio.multi PA15,PA5,PA0,PA15,PA5 timer=2", "usage"),
        ("sgpio.multi PA15 timer=4", "range"),
        ("sgpio.multi PA15 timer=2 ch=0", "range"),
        ("sgpio.multi PA15,PZ1 timer=2", "pin"),
        ("sgpio.multi PA15,PC6 timer=2", "pin"),
        ("sgpio.multi PA15,PA5,PA0 timer=2", "ok"),
    ],
)
def test_sgpio_reasons_wb55(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("sgpio.af PA10 timer=3", "ok"),
        ("sgpio.af PA10 timer=4", "range"),
        ("sgpio.af PB14 timer=3", "pin"),
        ("sgpio.af PB14 timer=3 ch=3", "ok"),
        ("sgpio.out PA9 1", "busy"),
        ("sgpio.out PB8 1", "ok"),
        ("sgpio.out PA3 1", "pin"),
        ("sgpio.multi PA10,PB5,PA2 timer=3", "ok"),
    ],
)
def test_sgpio_reasons_wba55(line, expected):
    terminal, _, _ = make("stm32wba55")
    assert reason(terminal, line) == expected


def test_sgpio_af_reports_the_table_function():
    _, _, fw = make()
    assert fw.sgpio.af("tim2ch1", timer=2) == 1
    assert fw.sgpio.af("tim16ch1", timer=16) == 14
    assert fw.sgpio.af("PA8", af=0) == 0
    _, _, fw = make("stm32wba55")
    assert fw.sgpio.af("tim3ch1", timer=3) == 2


def test_sgpio_outputs_latch_and_rebuild():
    terminal, _, fw = make()
    fw.sgpio.out("PA8", 1)
    assert fw.sgpio.latch("PA8") == 1
    fw.sgpio.out("PA8", 0, od=True)
    fw.sgpio.out("PA8", 1)
    assert fw.sgpio.latch("PA8") == 1
    assert terminal.command("sgpio.out PA8 0 speed=high").ok
    assert fw.sgpio.latch("PA8") == 0
    fw.sgpio.release("PA8")
    assert reason(terminal, "sgpio.latch PA8") == "notopen"


def test_sgpio_slots_and_pin_sharing():
    terminal, _, fw = make()
    for pin in ("PC6", "PC10", "PC12", "PC13"):
        fw.sgpio.out(pin, 0)
    assert reason(terminal, "sgpio.out PE4 0") == "busy"
    assert reason(terminal, "sgpio.af PC6 af=0") == "busy", "a pin serves one use"
    for pin in ("PA15", "PA5", "PA0", "PA6"):
        fw.sgpio.af(pin, af=1)
    assert reason(terminal, "sgpio.af PA1 af=1") == "busy"
    assert reason(terminal, "sgpio.af PA15 af=1") == "busy", "already muxed"
    assert reason(terminal, "gpio.cfg PA15 in") == "busy"
    assert reason(terminal, "sgpio.multi PA15 timer=2") == "busy"
    fw.sgpio.release("PA15")
    fw.sgpio.release("PA5")
    fw.sgpio.multi(["PA15", "PA5"], timer=2)
    assert reason(terminal, "sgpio.multi PA0 timer=2") == "busy"
    fw.sgpio.release("PA5")
    assert ("sgpio", "multi") not in fw.open_instances
    fw.sgpio.multi(["PA15", "PA5"], timer=2)


def test_sgpio_close_all_releases_everything():
    terminal, fake, fw = make()
    fw.sgpio.out("PA8", 1)
    fw.sgpio.af("PA15", timer=2)
    fw.sgpio.multi(["PA5", "PA0"], timer=2)
    assert fw.close_all() == []
    assert fake.claims == {}
    assert terminal.command("gpio.cfg PA5 in").ok


def test_sgpio_boot_forgets_everything():
    terminal, fake, fw = make()
    fw.sgpio.out("PA8", 1)
    fake.boot()
    assert reason(terminal, "sgpio.latch PA8") == "notopen"
    fw.sgpio.out("PA8", 0)


def test_clock_info_wb55():
    _, _, fw = make()
    info = fw.clock.info()
    assert (info.sysclk, info.hclk, info.pclk1, info.pclk2, info.pclk7) == (64_000_000,) * 4 + (None,)
    assert info.flags == {"hse": 1, "lse": 1, "hsi": 1, "hsi48": 1, "pll": 1}
    assert (info.rngsel, info.clk48) == ("clk48", "hsi48")


def test_clock_info_wba55():
    _, _, fw = make("stm32wba55")
    info = fw.clock.info()
    assert info.pclk7 == 100_000_000
    assert "hsi48" not in info.flags
    assert (info.rngsel, info.clk48) == ("hsi", None)


def test_clock_scaffolding_tracks_its_undo():
    _, _, fw = make()
    fw.clock.hsi48(False)
    assert fw.clock.info().flags["hsi48"] == 0
    fw.clock.mco("lse")
    assert set(fw.open_instances) == {("clock", "hsi48"), ("clock", "mco")}
    assert fw.close_all() == []
    assert fw.clock.info().flags["hsi48"] == 1
    fw.clock.mco("sysclk", div=16)
    fw.clock.mco("off")
    assert fw.open_instances == []


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("clock.info 1", "usage"),
        ("clock.info x=1", "usage"),
        ("clock.mco", "usage"),
        ("clock.mco pll", "usage"),
        ("clock.mco sysclk div=3", "usage"),
        ("clock.mco sysclk div=16", "ok"),
        ("clock.mco hsi48 div=1", "ok"),
        ("clock.hsi48", "usage"),
        ("clock.hsi48 2", "range"),
        ("clock.hsi48 1", "ok"),
    ],
)
def test_clock_reasons_wb55(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


@pytest.mark.parametrize("line", ["clock.mco sysclk", "clock.hsi48 1"])
def test_clock_scaffolding_unsupported_on_wba55(line):
    terminal, _, _ = make("stm32wba55")
    assert reason(terminal, line) == "unsupported"


@pytest.mark.parametrize(
    ("family", "line", "expected"),
    [
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1", "ok"),
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1 tx=PA2", "ok"),
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1 rx=PA3", "usage"),
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1 dma=1", "usage"),
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1 sync=1", "usage"),
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1 tx=PA2 flow=rts", "usage"),
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1 tx=PA2 flow=rts rts=PB12", "ok"),
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1 tx=PA2 flow=rts rts=PA2", "pin"),
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1 tx=PA2 flow=cts cts=PA6", "unsupported"),
        ("stm32wb55", "uart.open 1 lp=1 sendonly=1 tx=PA2 parity=even", "unsupported"),
        ("stm32wba55", "uart.open 2 sendonly=1 tx=PB0", "ok"),
        ("stm32wba55", "uart.open 2 sendonly=1 tx=PB0 swap=1", "unsupported"),
        ("stm32wba55", "uart.open 1 lp=1 sendonly=1 tx=PB5", "unsupported"),
    ],
)
def test_uart_sendonly_reasons(family, line, expected):
    terminal, _, _ = make(family)
    assert reason(terminal, line) == expected


def test_uart_sendonly_receives_nothing():
    _, _, fw = make()
    fw.uart.open(1, lp=True, tx="lpuart1tx", sendonly=True)
    assert fw.uart.recv(1) == b""
    fw.uart.send(1, b"\x55")


def test_fake_uid_has_the_real_layout():
    _, fake, fw = make()
    assert fake.uid == FAKE_UID
    uid = parse_uid(fw.system.info().uid or "")
    assert len(uid.raw) == UID_BYTES
    assert uid.lot == b"HALSTFK"
    assert (uid.x, uid.y, uid.wafer) == (0x24, 0x51, 7)
    _, fake, _ = make(uid="00112233445566778899aabb")
    assert fake.uid == "00112233445566778899aabb", "an explicit UID is kept"


def test_parse_uid_rejects_other_lengths():
    with pytest.raises(ValueError):
        parse_uid("0011")


def square(hz, rate, seconds, phase=0.3):
    """A 50 % square wave sampled at `rate`."""
    return [1 if math.fmod((i / rate) * hz + phase, 1.0) < 0.5 else 0 for i in range(int(seconds * rate))]


@pytest.mark.parametrize(("hz", "rate"), [(32768.0, 1_000_000.0), (32771.3, 1_250_000.0), (4_000_000.0, 100_000_000.0)])
def test_edge_fit_frequency(hz, rate):
    bits = square(hz, rate, 1000 / hz)
    assert edge_fit_frequency(bits, rate) == pytest.approx(hz, rel=5e-6)


def test_edge_fit_frequency_needs_edges():
    with pytest.raises(ValueError):
        edge_fit_frequency([0, 1, 1, 0], 1e6)


def test_sgpio_wrapper_errors_raise():
    _, _, fw = make()
    with pytest.raises(FirmwareError) as error:
        fw.sgpio.af("PA15")
    assert error.value.reason == "usage"
