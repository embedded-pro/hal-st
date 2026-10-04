"""The hal-st default clock configurations (`ConfigureDefaultClockNucleoWB55RG`, `ConfigureDefaultClockNucleoWBA55CG`)
through the `clock` group: bus clocks, oscillator ready flags and the RNG kernel clock selection against
`tests.clock`, and on STM32WB55 the clocks themselves on the MCO pin.

MCO (STM32WB55 only; the only MCO pin of the STM32WBA55 is the terminal RX): `sgpio.af <mco pin> af=0` muxes the
pin, `clock.mco` selects source and divider, and the logic analyzer measures the frequency with a least-squares fit
of the rising edge times (`groups.io.edge_fit_frequency`), precise enough for the LSE crystal's +-100 ppm.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

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


def test_bus_clocks(fw, board_cfg, clock_cfg):
    info = fw.clock.info()
    for bus, hz in clock_cfg["frequencies"].items():
        assert getattr(info, bus) == hz, f"{bus}: {info.raw}"
    assert info.pclk7 is None or "pclk7" in clock_cfg["frequencies"], "pclk7 only where the MCU has APB7"
    assert info.sysclk == board_cfg.sysclk == fw.system.info().sysclk
    assert (info.pclk1, info.pclk2) == (board_cfg.clock("pclk1"), board_cfg.clock("pclk2")), "the clocks the expectations use"


def test_oscillators_and_rng_clock(fw, clock_cfg):
    info = fw.clock.info()
    for flag, ready in clock_cfg["flags"].items():
        assert info.flags.get(flag) == ready, f"{flag}: {info.raw}"
    assert info.rngsel == clock_cfg["rngsel"], info.raw
    assert info.clk48 == clock_cfg.get("clk48"), info.raw


@pytest.mark.ad3
@pytest.mark.board_params("output", "clock.mco.outputs")
def test_mco_frequency(fw, ad3, need, mco_cfg, output):
    dio = need.dio(mco_cfg["pin"])
    fw.sgpio.af(mco_cfg["pin"], af=mco_cfg["af"])
    fw.clock.mco(output["source"], div=output["div"])
    measured = measure(ad3, dio, output["hz"], mco_cfg["periods"])
    assert measured == pytest.approx(output["hz"], rel=output["tolerance"]), f"{output['source']}/{output['div']}"


@pytest.mark.ad3
def test_mco_off(fw, ad3, need, mco_cfg):
    dio = need.dio(mco_cfg["pin"])
    fw.sgpio.af(mco_cfg["pin"], af=mco_cfg["af"])
    fw.clock.mco("sysclk", div=16)
    fw.clock.mco("off")
    capture = ad3.logic.record_for(1e-3, timeout=2.0)
    assert analysis.edge_count(capture.channel(dio)) == 0, "MCO still running after clock.mco off"


@pytest.mark.ad3
def test_hsi48_switch(fw, ad3, need, mco_cfg):
    """`clock.hsi48 0` stops HSI48 (no MCO edges, ready flag 0); `clock.hsi48 1` brings both back."""
    output = mco_cfg["hsi48"]
    dio = need.dio(mco_cfg["pin"])
    fw.sgpio.af(mco_cfg["pin"], af=mco_cfg["af"])
    fw.clock.mco("hsi48", div=output["div"])
    fw.clock.hsi48(False)
    assert fw.clock.info().flags["hsi48"] == 0
    capture = ad3.logic.record_for(1e-3, timeout=2.0)
    assert analysis.edge_count(capture.channel(dio)) == 0, "HSI48 still on the MCO pin"
    fw.clock.hsi48(True)
    assert fw.clock.info().flags["hsi48"] == 1
    measured = measure(ad3, dio, output["hz"], mco_cfg["periods"])
    assert measured == pytest.approx(output["hz"], rel=output["tolerance"])


def expect_reason(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


def test_info_errors(fw, clock_cfg):
    expect_reason(fw, "clock.info 1", "usage")
    expect_reason(fw, "clock.info all=1", "usage")


def test_mco_errors(fw, mco_cfg):
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
    for source in MCO_SOURCES:
        fw.clock.mco(source)
    fw.clock.hsi48(True)
