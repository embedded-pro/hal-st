"""SPI master (`hal::SpiMasterStm`, `SpiMasterStmDma`, `SynchronousSpiMasterStm`), decoded from a logic-analyzer
capture.

Wiring set `bundle1`: CLK, CS, MOSI and MISO of `tests.spi.instances` on DIOs. The AD3 SDK has no verified
SPI-slave mode, so the firmware master is observed with the logic analyzer: MISO is driven to a static level by
the AD3, or, with `--with loopback` and a MOSI-MISO jumper, only monitored (the firmware must then read back what
it sent). The clock is the fastest spiclk / 2^n not above `baud` (`expect.spi_clock`).
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import expect


@pytest.fixture
def spi_cfg(board_cfg):
    return board_cfg.param("spi")


def spi_dios(need, instance):
    return {key: need.dio(instance[key]) for key in ("clk", "cs", "mosi", "miso")}


def measured_clock(clk, rate, baud):
    """Clock frequency over each burst of edges (a gap of more than 1.5 periods separates bursts)."""
    edges = [edge.index for edge in analysis.edges(clk)]
    gap = 1.5 * rate / baud
    bursts, current = [], edges[:1]
    for index in edges[1:]:
        if index - current[-1] > gap:
            bursts.append(current)
            current = [index]
        else:
            current.append(index)
    bursts.append(current)
    periods = sum((len(burst) - 1) / 2 for burst in bursts if len(burst) > 2)
    samples = sum(burst[-1] - burst[0] for burst in bursts if len(burst) > 2)
    return periods * rate / samples if samples else 0.0


def open_spi(fw, spi_cfg, instance, variant, use_cs=True, **options):
    driver = spi_cfg["variants"][variant]
    fw.spi.open(
        instance["index"],
        clk=instance["clk"],
        mosi=instance["mosi"],
        miso=instance["miso"],
        cs=instance["cs"] if use_cs else None,
        dma=driver["dma"],
        sync=driver["sync"],
        **options,
    )


def expected_rx(payload, loopback, miso_level):
    return payload if loopback else bytes([0xFF if miso_level else 0x00] * len(payload))


def prepare_miso(ad3, wiring, dios, miso_level):
    loopback = wiring.has("loopback")
    if loopback and miso_level:
        pytest.skip("MISO follows MOSI with the loopback jumper")
    if not loopback:
        ad3.dio.drive(dios["miso"], miso_level)
    return loopback


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.matrix("spi.transfer")
@pytest.mark.board_params("miso_level", "spi.miso_levels")
def test_transfer(fw, ad3, need, wiring, board_cfg, spi_cfg, instance, mode, baud, variant, cs, miso_level):
    dios = spi_dios(need, instance)
    loopback = prepare_miso(ad3, wiring, dios, miso_level)
    use_cs = cs == "gpio"
    open_spi(fw, spi_cfg, instance, variant, use_cs, baud=baud, mode=mode)
    clock = expect.spi_clock(board_cfg.clock("spi", instance["index"]), baud)
    cs_dio = dios["cs"] if use_cs else None
    for text in spi_cfg["payloads"]:
        payload = bytes.fromhex(text)
        wanted = expected_rx(payload, loopback, miso_level)
        duration = len(payload) * 8 / clock * 4 + 200e-6
        rate = min(ad3.logic.clock_hz, 20 * clock, ad3.logic.buffer_size / duration)
        decodable = rate >= 4 * clock
        trigger = (dios["cs"], "falling") if use_cs else (dios["clk"], "either")
        capture = ad3.logic.arm(rate, int(duration * rate), trigger=trigger, pretrigger=0.05) if decodable else None
        assert fw.spi.xfer(instance["index"], payload) == wanted
        if capture is None:
            continue
        result = capture.wait(timeout=2.0)
        frames = result.spi(dios["clk"], dios["mosi"], dios["miso"], cs_dio, mode)
        mosi, miso = analysis.spi_join(frames)
        assert mosi == payload, f"decoded MOSI {mosi.hex()} (frames: {len(frames)})"
        assert miso == wanted
        clk = result.channel(dios["clk"])
        cpol, _ = analysis.spi_mode_bits(mode)
        cs_bits = None if cs_dio is None else result.channel(cs_dio)
        assert analysis.clock_idle_level(clk, cs_bits) == cpol, "clock idle level (CPOL)"
        assert measured_clock(clk, result.rate, clock) == pytest.approx(clock, rel=spi_cfg["tolerance"]["baud"])
        if cs_bits is not None:
            assert cs_bits[-1] == 1, "the chip select must be released after the transfer"


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.matrix("spi.sessions")
def test_continued_session(fw, ad3, need, spi_cfg, instance, mode, variant):
    """`continue=1` keeps the chip select low for the next `spi.xfer`; the bytes of both arrive in one session."""
    dios = spi_dios(need, instance)
    ad3.dio.drive(dios["miso"], 0)
    baud = spi_cfg["session_baud"]
    open_spi(fw, spi_cfg, instance, variant, baud=baud, mode=mode)
    duration = 0.2
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / duration)
    if rate < 4 * baud:
        pytest.skip("the session does not fit the logic analyzer buffer")
    capture = ad3.logic.arm(rate, int(duration * rate), trigger=(dios["cs"], "falling"), pretrigger=0.01)
    fw.spi.xfer(instance["index"], b"\x12\x34", continue_=True)
    fw.spi.xfer(instance["index"], b"\x56", continue_=False)
    result = capture.wait(timeout=3.0)
    cs_bits = result.channel(dios["cs"])
    assert analysis.edge_count(cs_bits[result.trigger_index or 0 :], "rising") == 1, "one session: CS rises once"
    frames = result.spi(dios["clk"], dios["mosi"], None, dios["cs"], mode)
    assert analysis.spi_join(frames)[0] == b"\x12\x34\x56"


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.matrix("spi.receive_only")
def test_receive_only_first(fw, ad3, need, wiring, spi_cfg, instance, variant):
    """A receive-only transfer (`spi.xfer <i> - rx=<n>`) right after `spi.open` clocks out zeros and returns MISO."""
    dios = spi_dios(need, instance)
    loopback = prepare_miso(ad3, wiring, dios, 0 if wiring.has("loopback") else 1)
    open_spi(fw, spi_cfg, instance, variant, baud=1000000)
    assert fw.spi.xfer(instance["index"], b"", rx=4) == (b"\x00" * 4 if loopback else b"\xff" * 4)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.matrix("spi.largest")
def test_largest_transfer(fw, ad3, need, wiring, spi_cfg, instance, variant):
    dios = spi_dios(need, instance)
    loopback = prepare_miso(ad3, wiring, dios, 0 if wiring.has("loopback") else 1)
    open_spi(fw, spi_cfg, instance, variant, baud=1000000)
    size = spi_cfg["max_transfer"]
    payload = bytes((i * 11 + 5) & 0xFF for i in range(size))
    assert fw.spi.xfer(instance["index"], payload) == (payload if loopback else b"\xff" * size)
    assert fw.spi.xfer(instance["index"], payload[:8], rx=0) == b""
    assert fw.spi.xfer(instance["index"], payload[:2], rx=6) == (payload[:2] + b"\x00" * 4 if loopback else b"\xff" * 6)
    assert fw.spi.xfer(instance["index"], b"", rx=4) == (b"\x00" * 4 if loopback else b"\xff" * 4)


@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.board_params("baud", "spi.open_bauds")
def test_open_bauds(fw, board_cfg, instance, baud):
    """`baud` outside spiclk/256 .. spiclk/2 is `ERR range`."""
    fits = expect.spi_baud_fits(board_cfg.clock("spi", instance["index"]), baud)
    try:
        fw.spi.open(instance["index"], clk=instance["clk"], mosi=instance["mosi"], miso=instance["miso"], baud=baud)
    except FirmwareError as error:
        assert error.reason == "range" and not fits, f"ERR {error.reason} although {baud} Hz fits"
        return
    assert fits, f"{baud} Hz accepted outside the limits"
    fw.spi.close(instance["index"])


@pytest.mark.board_params("instance", "spi.instances")
def test_open_errors(fw, board_cfg, instance):
    pins = {"clk": instance["clk"], "mosi": instance["mosi"], "miso": instance["miso"]}
    cases = [
        ({"clk": instance["clk"], "mosi": instance["mosi"]}, "usage"),
        ({**pins, "dma": True, "sync": True}, "usage"),
        ({**pins, "mode": 4}, "range"),
        ({**pins, "baud": 0}, "range"),
        ({"clk": instance["miso"], "mosi": instance["mosi"], "miso": instance["clk"]}, "pin"),
        ({**pins, "cs": board_cfg.terminal.pins[0]}, "busy"),
        ({**pins, "cs": instance["clk"]}, "busy"),
    ]
    for options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.spi.open(instance["index"], **options)
        assert error.value.reason == reason, options
    fw.spi.open(instance["index"], **pins, cs=instance["cs"], sync=True)


@pytest.mark.board_params("instance", "spi.instances")
def test_transfer_errors(fw, spi_cfg, instance):
    with pytest.raises(FirmwareError) as error:
        fw.spi.xfer(instance["index"], b"\x00")
    assert error.value.reason == "notopen"
    fw.spi.open(instance["index"], clk=instance["clk"], mosi=instance["mosi"], miso=instance["miso"])
    size = spi_cfg["max_transfer"]
    for line, reason in (
        (f"spi.xfer {instance['index']} -", "usage"),
        (f"spi.xfer {instance['index']} 00 rx=0x", "usage"),
        (f"spi.xfer {instance['index']} 123", "usage"),
        (f"spi.xfer {instance['index']} " + "00" * (size + 1), "range"),
        (f"spi.xfer {instance['index']} - rx={size + 1}", "range"),
        (f"spi.xfer {instance['index']} 00 continue=2", "range"),
    ):
        with pytest.raises(FirmwareError) as error:
            fw.terminal.command(line)
        assert error.value.reason == reason, line
