"""The hal-st default clock configurations (`ConfigureDefaultClockNucleoWB55RG`, `ConfigureDefaultClockNucleoWBA55CG`)
through the `clock` group: bus clocks, oscillator ready flags and the RNG kernel clock selection against
`tests.clock`, and on STM32WB55 the clocks themselves on the MCO pin.

MCO (STM32WB55 only; the only MCO pin of the STM32WBA55 is the terminal RX): `sgpio.af <mco pin> af=0` muxes the
pin, `clock.mco` selects source and divider, and the logic analyzer measures the frequency with a least-squares fit
of the rising edge times (`groups.io.edge_fit_frequency`), precise enough for the LSE crystal's +-100 ppm.

Scenarios: features/clock.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.groups.io import MCO_SOURCES, edge_fit_frequency


@pytest.fixture
def clock_cfg(board_cfg):
    cfg = board_cfg.param("clock", None)
    if cfg is None:
        pytest.skip("tests.clock not configured")
    return cfg


@pytest.fixture
def mco_cfg(clock_cfg):
    cfg = clock_cfg.get("mco")
    if cfg is None:
        pytest.skip("no MCO pin on this board")
    return cfg


def measure(ad3, dio, hz, periods):
    capture = ad3.logic.record_for(periods / hz, timeout=periods / hz + 2)
    return edge_fit_frequency(capture.channel(dio), capture.rate)


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


@pytest.mark.usefixtures("clock_cfg")
@scenario("clock.feature", "The bus clocks are those of the board file")
def test_bus_clocks():
    pass


@pytest.mark.usefixtures("clock_cfg")
@scenario("clock.feature", "The oscillator ready flags and the RNG kernel clock are those of the board file")
def test_oscillators_and_rng_clock():
    pass


@pytest.mark.board_params("output", "clock.mco.outputs")
@pytest.mark.usefixtures("mco_cfg")
@scenario("clock.feature", "The MCO pin outputs the source of the output divided by its divider")
def test_mco_frequency(output):
    pass


@pytest.mark.usefixtures("mco_cfg")
@scenario("clock.feature", "clock.mco off stops the MCO output")
def test_mco_off():
    pass


@pytest.mark.usefixtures("mco_cfg")
@scenario("clock.feature", "clock.hsi48 stops and restarts HSI48")
def test_hsi48_switch():
    pass


@pytest.mark.usefixtures("clock_cfg")
@scenario("clock.feature", "clock.info takes no argument")
def test_info_errors():
    pass


@pytest.mark.usefixtures("mco_cfg")
@scenario("clock.feature", "Malformed and out-of-range clock.mco and clock.hsi48 commands are refused, and every source is accepted")
def test_mco_errors():
    pass


@given("the HSI48 output of the MCO", target_fixture="hsi48_output")
def hsi48_output(mco_cfg):
    return mco_cfg["hsi48"]


@given("the MCO pin is wired to a DIO", target_fixture="dio")
def mco_wired(need, mco_cfg):
    return need.dio(mco_cfg["pin"])


@when("the clock info is read", target_fixture="info")
def clock_info(fw):
    return fw.clock.info()


@when("the MCO pin is muxed to its alternate function")
def mco_muxed(fw, mco_cfg):
    fw.sgpio.af(mco_cfg["pin"], af=mco_cfg["af"])


@when("the MCO outputs the source of the output divided by its divider")
def mco_output(fw, output):
    fw.clock.mco(output["source"], div=output["div"])


@when(parsers.parse('the MCO outputs "{source}" divided by {div:d}'))
def mco_source(fw, source, div):
    fw.clock.mco(source, div=div)


@when(parsers.parse('the MCO outputs "{source}" divided by the divider of the HSI48 output'))
def mco_hsi48(fw, hsi48_output, source):
    fw.clock.mco(source, div=hsi48_output["div"])


@when("the MCO is turned off")
def mco_off(fw):
    fw.clock.mco("off")


@when("HSI48 is turned off")
def hsi48_off(fw):
    fw.clock.hsi48(False)


@when("HSI48 is turned on")
@then("HSI48 is turned on")
def hsi48_on(fw):
    fw.clock.hsi48(True)


@then("every bus clock is its frequency of the board file")
def bus_clocks(clock_cfg, info):
    for bus, hz in clock_cfg["frequencies"].items():
        assert getattr(info, bus) == hz, f"{bus}: {info.raw}"


@then("pclk7 is reported only where the board file has it")
def pclk7_only_with_apb7(clock_cfg, info):
    assert info.pclk7 is None or "pclk7" in clock_cfg["frequencies"], "pclk7 only where the MCU has APB7"


@then("sysclk is that of the board file and of the system info")
def sysclk_matches(fw, board_cfg, info):
    assert info.sysclk == board_cfg.sysclk == fw.system.info().sysclk


@then("pclk1 and pclk2 are the clocks of the board file the expectations use")
def expectation_clocks(board_cfg, info):
    assert (info.pclk1, info.pclk2) == (board_cfg.clock("pclk1"), board_cfg.clock("pclk2")), "the clocks the expectations use"


@then("every oscillator ready flag is that of the board file")
def oscillator_flags(clock_cfg, info):
    for flag, ready in clock_cfg["flags"].items():
        assert info.flags.get(flag) == ready, f"{flag}: {info.raw}"


@then("the RNG kernel clock selection is that of the board file")
def rng_clock(clock_cfg, info):
    assert info.rngsel == clock_cfg["rngsel"], info.raw


@then("clk48 is that of the board file, none unless set")
def clk48(clock_cfg, info):
    assert info.clk48 == clock_cfg.get("clk48"), info.raw


@then("the logic analyzer measures the frequency of the output on the DIO within its tolerance")
def output_frequency(ad3, mco_cfg, dio, output):
    measured = measure(ad3, dio, output["hz"], mco_cfg["periods"])
    assert measured == pytest.approx(output["hz"], rel=output["tolerance"]), f"{output['source']}/{output['div']}"


@then(parsers.parse("the logic analyzer records no edge of the MCO on the DIO in {ms:d} ms"))
def mco_stopped(ad3, dio, ms):
    capture = ad3.logic.record_for(ms / 1000, timeout=2.0)
    assert analysis.edge_count(capture.channel(dio)) == 0, "MCO still running after clock.mco off"


@then(parsers.parse("the logic analyzer records no edge of HSI48 on the DIO in {ms:d} ms"))
def hsi48_stopped(ad3, dio, ms):
    capture = ad3.logic.record_for(ms / 1000, timeout=2.0)
    assert analysis.edge_count(capture.channel(dio)) == 0, "HSI48 still on the MCO pin"


@then(parsers.parse("the HSI48 ready flag is {ready:d}"))
def hsi48_flag(fw, ready):
    assert fw.clock.info().flags["hsi48"] == ready


@then("the logic analyzer measures the frequency of the HSI48 output on the DIO within its tolerance")
def hsi48_frequency(ad3, mco_cfg, dio, hsi48_output):
    measured = measure(ad3, dio, hsi48_output["hz"], mco_cfg["periods"])
    assert measured == pytest.approx(hsi48_output["hz"], rel=hsi48_output["tolerance"])


@then(parsers.parse('the command line "{line}" fails with "{reason}"'))
def command_refused(fw, line, reason):
    expect_reason(fw, line, reason)


@then("every malformed or out-of-range clock.mco and clock.hsi48 command fails with its reason")
def mco_errors(fw):
    for line, reason in (
        ("clock.mco", "usage"),
        ("clock.mco pll", "usage"),
        ("clock.mco sysclk div=3", "usage"),
        ("clock.mco sysclk div=32", "usage"),
        ("clock.mco sysclk speed=1", "usage"),
        ("clock.hsi48", "usage"),
        ("clock.hsi48 2", "range"),
    ):
        expect_reason(fw, line, reason)


@then("the MCO outputs every MCO source in turn")
def every_source(fw):
    for source in MCO_SOURCES:
        fw.clock.mco(source)
