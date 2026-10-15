"""The fake `comp` group (EMIL's HilComparatorCommands over validation/firmware/ComparatorFactory.cpp: argument order,
one open comparator, `comp.dac`) and the `groups.comp` wrapper and threshold model."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareTerminal

from hal_st_validation.fake_firmware import G474_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups.comp import VREFINT, threshold_volts


def make(family="stm32g474"):
    fake = FakeFirmware(family=family)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    return terminal, fake, Firmware(terminal, G474_PINS)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


def test_thresholds():
    assert threshold_volts("vref/2") == pytest.approx(VREFINT / 2)
    assert threshold_volts("dac", 3.3, 4095) == pytest.approx(3.3)


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("comp.open 1", "ok"),
        ("comp.open 1 inp=PB1 inm=PA0 out=PA11 hyst=70 pol=inverted blank=tim2oc3 irq=immediate", "ok"),
        ("comp.open 7 inp=PB14 inm=PB12 out=PC8", "ok"),
        ("comp.open 8 hyst=x", "range"),
        ("comp.open 0 hyst=x", "range"),
        ("comp.open 1 inp=PZ1", "pin"),
        ("comp.open 1 inm=bogus hyst=x", "pin"),
        ("comp.open 1 hyst=x inp=PB5", "usage"),
        ("comp.open 1 hyst=15 inp=PB5", "range"),
        ("comp.open 1 inp=PB5 blank=tim2oc4", "pin"),
        ("comp.open 1 blank=tim2oc4 out=PB5", "pin"),
        ("comp.open 3 blank=tim2oc4", "ok"),
        ("comp.open 4 blank=tim15oc1", "ok"),
        ("comp.open 1 blank=tim9oc1 irq=x", "usage"),
        ("comp.open 1 dac=5", "usage"),
        ("comp.open 1 inm=dac dac=5", "ok"),
    ],
)
def test_open_argument_order(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


def test_one_comparator_at_a_time():
    terminal, _, _ = make()
    assert reason(terminal, "comp.open 1") == "ok"
    assert reason(terminal, "comp.open 2") == "busy"
    assert reason(terminal, "comp.close 1") == "ok"
    assert reason(terminal, "comp.open 2") == "ok"


def test_pins_and_dac_are_held():
    terminal, fake, _ = make()
    assert reason(terminal, "gpio.cfg PA1 in") == "ok"
    assert reason(terminal, "comp.open 1") == "busy"
    assert reason(terminal, "comp.open 3 inm=dac") == "ok"
    assert fake.resources[("dac", 4)] == ("comp", "3")


def test_read_compares_the_input_with_the_threshold():
    _, fake, fw = make()
    fw.comp.open(1, inm="vref/2")
    fake.adc_codes["PA1"] = 0
    assert fw.comp.read(1) == 0
    fake.adc_codes["PA1"] = 4095
    assert fw.comp.read(1) == 1
    fw.comp.close(1)
    fw.comp.open(1, inm="vref/2", pol="inverted")
    assert fw.comp.read(1) == 0


def test_dac_follows_the_open_comparator():
    terminal, _, fw = make()
    assert reason(terminal, "comp.dac 1 100") == "notopen"
    fw.comp.open(1, inm="dac")
    assert fw.comp.dac(1, 100) == 100
    assert reason(terminal, "comp.dac 2 100") == "notopen"
    assert reason(terminal, "comp.dac 1 4096") == "range"
    fw.comp.close(1)
    fw.comp.open(1)
    assert reason(terminal, "comp.dac 1 100") == "notopen"


def test_synchronous_refuses_interrupts():
    terminal, _, fw = make()
    fw.comp.open(1, sync=True)
    assert reason(terminal, "comp.irq 1 rising") == "unsupported"
    fw.comp.close(1)
    fw.comp.open(1)
    fw.comp.irq(1, "both")
    assert fw.comp.count(1, clear=True) == 0


@pytest.mark.parametrize("family", ["stm32wb55", "stm32wba55"])
def test_unsupported_off_the_g474(family):
    terminal, _, _ = make(family)
    for line in ("comp.open 1", "comp.read 1", "comp.dac 1 0"):
        assert reason(terminal, line) == "unsupported"
