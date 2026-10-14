"""The fake `opamp` group (argument order and reasons of validation/firmware/OpampGroup.cpp, one amplifier at a time,
the DAC resource) and the `groups.opamp` wrapper and output model."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareTerminal

from hal_st_validation.fake_firmware import G474_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups.opamp import RAIL_MARGIN_V, opamp_volts


def make(family="stm32g474"):
    fake = FakeFirmware(family=family)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    return terminal, fake, Firmware(terminal, G474_PINS)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


def test_output_model():
    assert opamp_volts(1.0) == 1.0
    assert opamp_volts(0.2, "pga", 8) == pytest.approx(1.6)
    assert opamp_volts(1.0, "pga", 64) == pytest.approx(3.3 - RAIL_MARGIN_V)
    assert opamp_volts(0.0) == RAIL_MARGIN_V


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("opamp.open 1", "ok"),
        ("opamp.open 1 mode=pga gain=64 inp=PA7", "ok"),
        ("opamp.open 3 mode=standalone inm=PB10 inp=PB13 out=pin", "ok"),
        ("opamp.open 1 mode=pga pga=bias inm=PA3", "busy"),
        ("opamp.open 6 inp=dac dac=4095", "ok"),
        ("opamp.open 7 mode=x", "range"),
        ("opamp.open 1 mode=x gain=3", "usage"),
        ("opamp.open 1 gain=3", "usage"),
        ("opamp.open 1 inp=PZ1 mode=x", "usage"),
        ("opamp.open 1 inp=PZ1", "pin"),
        ("opamp.open 1 mode=pga gain=3 inp=PB5", "range"),
        ("opamp.open 2 inp=dac inm=PA5", "usage"),
        ("opamp.open 2 inp=dac", "unsupported"),
        ("opamp.open 2 inp=PB5 dac=1", "usage"),
    ],
)
def test_open_argument_order(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


def test_one_amplifier_and_its_dac():
    terminal, fake, fw = make()
    trim = fw.opamp.open(1, inp="dac", dac=100)
    assert 0 <= trim.p <= 31 and 0 <= trim.n <= 31
    assert reason(terminal, "opamp.open 3") == "busy"
    assert reason(terminal, "comp.open 1 inm=dac") == "busy"
    assert fw.opamp.dac(1, 2048) == 2048
    assert fw.opamp.read(1) == 2048
    fw.opamp.close(1)
    assert reason(terminal, "opamp.read 1") == "notopen"
    assert ("dac", 4) not in fake.resources


def test_read_follows_the_model():
    _, fake, fw = make()
    fake.adc_codes["PA1"] = 512
    fw.opamp.open(1, mode="pga", gain=4)
    assert fw.opamp.read(1) == 2048
    fw.opamp.close(1)
    fw.opamp.open(2)
    assert reason(fw.terminal, "opamp.read 2") == "unsupported"


def test_a_failed_output_claim_leaves_nothing_open():
    terminal, _, _ = make()
    assert reason(terminal, "opamp.open 1 out=pin") == "busy"
    assert reason(terminal, "opamp.read 1") == "notopen"
    assert reason(terminal, "opamp.open 1") == "ok"


@pytest.mark.parametrize("family", ["stm32wb55", "stm32wba55"])
def test_unsupported_off_the_g474(family):
    terminal, _, _ = make(family)
    for line in ("opamp.open 1", "opamp.read 1", "opamp.dac 1 0", "opamp.close 1"):
        assert reason(terminal, line) == "unsupported"
