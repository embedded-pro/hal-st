"""Operational amplifiers (`hal::OpampStm`) through the `opamp` group: follower and PGA against W1 and the internal
DAC, the output read by ADC1, the output swing, the trims, and the pins and DAC held while open.

Scenarios: features/opamp.feature.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, scenario, then, when

from hal_st_validation.groups.opamp import opamp_volts


@pytest.fixture
def opamp_cfg(board_cfg):
    return board_cfg.param("opamp")


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


def volts(opamp_cfg, code):
    return opamp_cfg["vref"] * code / 4095


def within(opamp_cfg, measured, expected):
    tolerance = opamp_cfg["tolerance"]
    return abs(measured - expected) <= tolerance["abs_v"] + tolerance["rel"] * abs(expected)


@pytest.mark.board_params("level", "opamp.levels_v")
@scenario("opamp.feature", "The follower tracks W1")
def test_follower(level):
    pass


@pytest.mark.board_params("gain, level", "opamp.pga_cases")
@scenario("opamp.feature", "The PGA multiplies W1 by its gain")
def test_pga(gain, level):
    pass


@pytest.mark.board_params("mode, gain, code", "opamp.dac_cases")
@scenario("opamp.feature", "The internal DAC drives the follower and the PGA")
def test_dac(mode, gain, code):
    pass


@pytest.mark.board_params("level", "opamp.pin_levels_v")
@scenario("opamp.feature", "OPAMP3 drives its output pin")
def test_pin_output(level):
    pass


@scenario("opamp.feature", "The output stops at the rail")
def test_rail():
    pass


@scenario("opamp.feature", "Self-calibration and factory trims are in range")
def test_trims():
    pass


@scenario("opamp.feature", "The pins and the DAC of an amplifier are held while it is open")
def test_ownership():
    pass


@pytest.mark.parametrize(
    ("line", "reason"),
    [
        ("opamp.open", "usage"),
        ("opamp.open 1 bogus=1", "usage"),
        ("opamp.open 0", "range"),
        ("opamp.open 7", "range"),
        ("opamp.open 1 mode=x", "usage"),
        ("opamp.open 1 gain=4", "usage"),
        ("opamp.open 1 mode=pga gain=3", "range"),
        ("opamp.open 1 mode=pga gain=128", "range"),
        ("opamp.open 1 mode=pga pga=bias", "usage"),
        ("opamp.open 1 mode=standalone", "usage"),
        ("opamp.open 1 inm=PA3", "usage"),
        ("opamp.open 1 dac=100", "usage"),
        ("opamp.open 1 inp=PB5", "pin"),
        ("opamp.open 1 inp=PZ1", "pin"),
        ("opamp.open 1 mode=standalone inm=PB5", "pin"),
        ("opamp.open 1 mode=pga pga=filter inm=PC5", "pin"),
        ("opamp.open 2 inp=dac", "unsupported"),
        ("opamp.open 1 out=pin", "busy"),
        ("opamp.open 1 cal=1", "unsupported"),
        ("opamp.read 1", "notopen"),
        ("opamp.dac 1 100", "notopen"),
        ("opamp.close 1", "notopen"),
        ("opamp.read 7", "range"),
    ],
)
@scenario("opamp.feature", "Malformed and out-of-range commands are refused")
def test_errors(line, reason):
    pass


@given("OPAMP1 is open as a follower of W1 with its internal output", target_fixture="amp")
def follower_on_w1(fw, need, opamp_cfg):
    case = opamp_cfg["w1"]
    amp = {"w": need.wavegen(case["inp"]), "index": case["index"]}
    fw.opamp.open(case["index"], inp=case["inp"])
    return amp


@given("OPAMP1 is open as a PGA of the gain on W1 with its internal output", target_fixture="amp")
def pga_on_w1(fw, need, opamp_cfg, gain):
    case = opamp_cfg["w1"]
    amp = {"w": need.wavegen(case["inp"]), "index": case["index"]}
    fw.opamp.open(case["index"], inp=case["inp"], mode="pga", gain=gain)
    return amp


@given("OPAMP1 is open as a PGA of gain 64 on W1 with its internal output", target_fixture="amp")
def pga64_on_w1(fw, need, opamp_cfg):
    case = opamp_cfg["w1"]
    amp = {"w": need.wavegen(case["inp"]), "index": case["index"]}
    fw.opamp.open(case["index"], inp=case["inp"], mode="pga", gain=64)
    return amp


@given("OPAMP3 is open as a follower of W1 with its output on PB1", target_fixture="amp")
def follower_on_pin(fw, need, opamp_cfg):
    case = opamp_cfg["pin_output"]
    amp = {"w": need.wavegen(case["inp"]), "index": case["index"], "dio": need.dio(case["out"])}
    fw.opamp.open(case["index"], inp=case["inp"], out="pin")
    return amp


@given("OPAMP1 is open on the internal DAC with the mode and gain of the case")
def on_dac(fw, opamp_cfg, mode, gain):
    fw.opamp.open(opamp_cfg["w1"]["index"], inp="dac", mode=mode, gain=gain if mode == "pga" else None)


@when("W1 is set to the level")
def set_level(ad3, opamp_cfg, amp, level):
    ad3.wavegen.dc(amp["w"], level)
    time.sleep(opamp_cfg["settle_s"])


@when("W1 is set to the rail test level")
def set_rail_level(ad3, opamp_cfg, amp):
    ad3.wavegen.dc(amp["w"], opamp_cfg["rail"]["input_v"])
    time.sleep(opamp_cfg["settle_s"])


@when("the DAC is set to the code of the case")
def set_dac(fw, opamp_cfg, code):
    fw.opamp.dac(opamp_cfg["w1"]["index"], code)
    time.sleep(opamp_cfg["settle_s"])


def read_volts(fw, opamp_cfg, index):
    return volts(opamp_cfg, fw.opamp.read(index))


@then("the ADC reads the level within the tolerance")
def reads_level(fw, opamp_cfg, amp, level):
    measured = read_volts(fw, opamp_cfg, amp["index"])
    assert within(opamp_cfg, measured, opamp_volts(level)), (measured, level)


@then("the ADC reads the level times the gain within the tolerance")
def reads_gain(fw, opamp_cfg, amp, level, gain):
    measured = read_volts(fw, opamp_cfg, amp["index"])
    expected = opamp_volts(level, "pga", gain, opamp_cfg["vref"])
    assert within(opamp_cfg, measured, expected), (measured, expected)


@then("the ADC reads the model output of the DAC level within the tolerance")
def reads_dac(fw, opamp_cfg, mode, gain, code):
    measured = read_volts(fw, opamp_cfg, opamp_cfg["w1"]["index"])
    expected = opamp_volts(volts(opamp_cfg, code), mode, gain, opamp_cfg["vref"])
    assert within(opamp_cfg, measured, expected), (measured, expected)


@then("the output DIO reads the logic level of the level")
def dio_level(ad3, opamp_cfg, amp, level):
    logic = opamp_cfg["logic"]
    if logic["low_v"] < level < logic["high_v"]:
        return
    assert ad3.dio.read(amp["dio"]) == int(level >= logic["high_v"])


@then("the ADC reads at least the rail level of the board file")
def at_rail(fw, opamp_cfg, amp):
    assert read_volts(fw, opamp_cfg, amp["index"]) >= opamp_cfg["rail"]["min_v"]


@then("OPAMP3 self-calibrates with its output on PB1 and OPAMP1 keeps the factory trims, all within range")
def trims(fw, need, opamp_cfg):
    case = opamp_cfg["pin_output"]
    need.unloaded(case["out"])
    for calibrate in (True, False):
        trim = fw.opamp.open(case["index"], inp=case["inp"], out="pin", cal=calibrate)
        fw.opamp.close(case["index"])
        assert 0 < trim.p < opamp_cfg["trim_max"] and 0 < trim.n < opamp_cfg["trim_max"], (calibrate, trim)
    trim = fw.opamp.open(opamp_cfg["dac_index"], inp="dac")
    assert 0 < trim.p < opamp_cfg["trim_max"] and 0 < trim.n < opamp_cfg["trim_max"], trim


@when("OPAMP1 is open on the internal DAC")
def open_on_dac(fw, opamp_cfg):
    fw.opamp.open(opamp_cfg["dac_index"], inp="dac")


@then("comp.open of comparator 1 on the same DAC channel answers busy")
def comparator_busy(fw):
    expect_reason(fw, "comp.open 1 inm=dac", "busy")


@then("opamp.open of OPAMP3 answers busy while OPAMP1 is open")
def second_busy(fw):
    expect_reason(fw, "opamp.open 3 inp=dac", "busy")


@when("OPAMP1 is closed")
def close_opamp(fw, opamp_cfg):
    fw.opamp.close(opamp_cfg["dac_index"])


@then("OPAMP1 opens on its VINP0 pin again")
def reopen(fw, opamp_cfg):
    fw.opamp.open(opamp_cfg["dac_index"], inp=opamp_cfg["w1"]["inp"])


@then("the command line fails with the reason")
def command_refused(fw, line, reason):
    expect_reason(fw, line, reason)
