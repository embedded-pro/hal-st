"""SPI master (`hal::SpiMasterStm`, `SpiMasterStmDma`, `SynchronousSpiMasterStm`), decoded from a logic-analyzer
capture.

Wiring set `bundle1`: CLK, CS, MOSI and MISO of `tests.spi.instances` on DIOs (an instance with `option` is
reached only through that option's jumpers, e.g. WB55 SPI2 with `--with spiloop`). The firmware master is
observed with the logic analyzer: MISO is driven to a static level by the AD3, or, where the loopback jumper ties
the instance's MOSI to its MISO (`--with loopback`), only monitored (the firmware must then read back what it
sent). The clock is the fastest spiclk / 2^n not above `baud` (`expect.spi_clock`).

Scenarios: features/spi.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, scenario, then, when

from hal_st_validation import expect


@pytest.fixture
def spi_cfg(board_cfg):
    return board_cfg.param("spi")


def spi_dios(need, wiring, instance):
    option = instance.get("option")
    if option and not wiring.has(option):
        pytest.skip(f"{instance['name']} is wired with --with {option} only")
    return {key: need.dio(instance[key]) for key in ("clk", "cs", "mosi", "miso")}


def looped(wiring, board_cfg, instance):
    """The loopback jumper ties this instance's MOSI to its MISO."""
    mosi, miso = (board_cfg.resolve_pin(instance[key]) for key in ("mosi", "miso"))
    return miso in wiring.jumpered_to(mosi, {"loopback"})


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


def prepare_miso(ad3, wiring, board_cfg, instance, dios, miso_level):
    loopback = looped(wiring, board_cfg, instance)
    if loopback and miso_level:
        pytest.skip("MISO follows MOSI with the loopback jumper")
    if not loopback:
        ad3.dio.drive(dios["miso"], miso_level)
    return loopback


def largest_payload(spi_cfg):
    return bytes((i * 11 + 5) & 0xFF for i in range(spi_cfg["max_transfer"]))


@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.matrix("spi.transfer")
@pytest.mark.board_params("miso_level", "spi.miso_levels")
@scenario("spi.feature", "Every payload is clocked out on MOSI and MISO is read back at the mode and clock")
def test_transfer(instance, mode, baud, variant, cs, miso_level):
    pass


@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.matrix("spi.sessions")
@scenario("spi.feature", "A continued transfer keeps the chip select low for the next one")
def test_continued_session(instance, mode, variant):
    pass


@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.matrix("spi.receive_only")
@scenario("spi.feature", "A receive-only transfer right after the open clocks out zeros")
def test_receive_only_first(instance, variant):
    pass


@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.matrix("spi.largest")
@scenario("spi.feature", "The largest transfer and the receive lengths around it come back")
def test_largest_transfer(instance, variant):
    pass


@pytest.mark.board_params("instance", "spi.instances")
@pytest.mark.board_params("baud", "spi.open_bauds")
@scenario("spi.feature", "A baud outside the SPI clock limits is refused")
def test_open_bauds(instance, baud):
    pass


@pytest.mark.board_params("instance", "spi.instances")
@scenario("spi.feature", "Invalid opens are refused")
def test_open_errors(instance):
    pass


@pytest.mark.board_params("instance", "spi.instances")
@scenario("spi.feature", "Transfers on a closed instance and malformed transfers are refused")
def test_transfer_errors(instance):
    pass


@given("the clock, chip select, MOSI and MISO of the instance are wired to DIOs", target_fixture="dios")
def instance_wired(need, wiring, instance):
    return spi_dios(need, wiring, instance)


@given(
    "the AD3 drives MISO to the MISO level unless the loopback jumper ties it to MOSI, which a high MISO level skips",
    target_fixture="loopback",
)
def miso_at_level(ad3, wiring, board_cfg, instance, dios, miso_level):
    return prepare_miso(ad3, wiring, board_cfg, instance, dios, miso_level)


@given("the AD3 drives MISO high unless the loopback jumper ties it to MOSI", target_fixture="loopback")
def miso_high(ad3, wiring, board_cfg, instance, dios):
    return prepare_miso(ad3, wiring, board_cfg, instance, dios, 0 if looped(wiring, board_cfg, instance) else 1)


@given("the AD3 drives MISO low")
def miso_low(ad3, dios):
    ad3.dio.drive(dios["miso"], 0)


@given("the largest payload: the max transfer of bytes counting up by 11 from 5", target_fixture="payload")
def payload_largest(spi_cfg):
    return largest_payload(spi_cfg)


@when("the instance is opened with the variant, the baud and the mode, with its chip select if the chip select is gpio")
def open_for_transfer(fw, spi_cfg, instance, mode, baud, variant, cs):
    open_spi(fw, spi_cfg, instance, variant, cs == "gpio", baud=baud, mode=mode)


@when("the instance is opened with the variant at the session baud and the mode")
def open_for_session(fw, spi_cfg, instance, mode, variant):
    open_spi(fw, spi_cfg, instance, variant, baud=spi_cfg["session_baud"], mode=mode)


@when("the instance is opened with the variant at 1000000 baud")
def open_at_1_mhz(fw, spi_cfg, instance, variant):
    open_spi(fw, spi_cfg, instance, variant, baud=1000000)


@when(
    "the logic analyzer is armed for 0.2 s on the falling chip select, unless it cannot sample 4 times per clock at the session baud",
    target_fixture="capture",
)
def session_armed(ad3, spi_cfg, dios):
    baud = spi_cfg["session_baud"]
    duration = 0.2
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / duration)
    if rate < 4 * baud:
        pytest.skip("the session does not fit the logic analyzer buffer")
    return ad3.logic.arm(rate, int(duration * rate), trigger=(dios["cs"], "falling"), pretrigger=0.01)


@when("the instance transfers 12 34, continuing the session")
def xfer_continued(fw, instance):
    fw.spi.xfer(instance["index"], b"\x12\x34", continue_=True)


@when("the instance transfers 56, ending the session")
def xfer_ended(fw, instance):
    fw.spi.xfer(instance["index"], b"\x56", continue_=False)


@when("the capture completes within 3 s", target_fixture="result")
def capture_completes(capture):
    return capture.wait(timeout=3.0)


@when("the instance is opened with its pins")
def open_plain(fw, instance):
    fw.spi.open(instance["index"], clk=instance["clk"], mosi=instance["mosi"], miso=instance["miso"])


@when("the instance is opened at the baud, which may be refused", target_fixture="refusal")
def open_at_baud(fw, instance, baud):
    try:
        fw.spi.open(instance["index"], clk=instance["clk"], mosi=instance["mosi"], miso=instance["miso"], baud=baud)
    except FirmwareError as error:
        return error
    return None


@then(
    "every payload of the board file comes back as the payload with the loopback jumper and at the MISO level otherwise and, where "
    "the logic analyzer samples 4 times per clock, decodes on MOSI and MISO with the clock idle at CPOL, at the expected clock and "
    "the chip select released"
)
def every_payload(fw, ad3, board_cfg, spi_cfg, instance, mode, baud, cs, miso_level, dios, loopback):
    use_cs = cs == "gpio"
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


@then("the chip select rises once after the trigger")
def one_session(dios, result):
    cs_bits = result.channel(dios["cs"])
    assert analysis.edge_count(cs_bits[result.trigger_index or 0 :], "rising") == 1, "one session: CS rises once"


@then("MOSI decodes as 12 34 56 at the mode")
def session_decoded(dios, result, mode):
    frames = result.spi(dios["clk"], dios["mosi"], None, dios["cs"], mode)
    assert analysis.spi_join(frames)[0] == b"\x12\x34\x56"


@then("a receive-only transfer of 4 bytes returns 4 zero bytes with the loopback jumper and 4 0xff bytes otherwise")
def receive_only(fw, instance, loopback):
    assert fw.spi.xfer(instance["index"], b"", rx=4) == (b"\x00" * 4 if loopback else b"\xff" * 4)


@then("a transfer of the payload returns it with the loopback jumper and 0xff bytes otherwise")
def largest_comes_back(fw, instance, loopback, payload):
    assert fw.spi.xfer(instance["index"], payload) == (payload if loopback else b"\xff" * len(payload))


@then("a transfer of the first 8 bytes of the payload receiving none returns nothing")
def send_only(fw, instance, payload):
    assert fw.spi.xfer(instance["index"], payload[:8], rx=0) == b""


@then(
    "a transfer of the first 2 bytes of the payload receiving 6 returns those and 4 zero bytes with the loopback jumper and 6 0xff bytes "
    "otherwise"
)
def receive_more(fw, instance, loopback, payload):
    assert fw.spi.xfer(instance["index"], payload[:2], rx=6) == (payload[:2] + b"\x00" * 4 if loopback else b"\xff" * 6)


@then('it was refused with "range" exactly when the baud is outside spiclk/256 .. spiclk/2')
def refused_outside(board_cfg, instance, baud, refusal):
    fits = expect.spi_baud_fits(board_cfg.clock("spi", instance["index"]), baud)
    if refusal is not None:
        assert refusal.reason == "range" and not fits, f"ERR {refusal.reason} although {baud} Hz fits"
        return
    assert fits, f"{baud} Hz accepted outside the limits"


@then("an accepted open closes again")
def accepted_closes(fw, instance, refusal):
    if refusal is None:
        fw.spi.close(instance["index"])


@then(
    "opening the instance without MISO, with both dma and sync, with mode 4, at baud 0, with clock and MISO swapped, with the terminal "
    "pin or with its clock as chip select fails with usage, usage, range, range, pin, busy and busy"
)
def open_refused(fw, board_cfg, instance):
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


@then("the instance opens synchronously with its pins and its chip select")
def opens_sync(fw, instance):
    pins = {"clk": instance["clk"], "mosi": instance["mosi"], "miso": instance["miso"]}
    fw.spi.open(instance["index"], **pins, cs=instance["cs"], sync=True)


@then('a transfer of 00 on the instance fails with "notopen"')
def xfer_not_open(fw, instance):
    with pytest.raises(FirmwareError) as error:
        fw.spi.xfer(instance["index"], b"\x00")
    assert error.value.reason == "notopen"


@then(
    "transfers with no data, a malformed receive length, odd hex, one byte over the max transfer, a receive length over it or continue=2 "
    "fail with usage, usage, usage, range, range and range"
)
def xfer_refused(fw, spi_cfg, instance):
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
