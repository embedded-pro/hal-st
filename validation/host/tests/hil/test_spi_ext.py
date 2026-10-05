"""SPI master extensions (PROTOCOL.md "SPI master"): the bit order (`lsb=1`), frame sizes (`bits=`, a
`hal::SpiDataSizeConfiguratorStm` on `hal::SpiMasterStmDma`), an 8-bit master reopened after a 16-bit one and the
hardware slave select (`nss=`).

The master is observed with the logic analyzer (`tests.spi_ext.instances`; an entry with `option` is reached only
through that option's jumpers). Words are decoded with `spiwords` at the frame size and compared with the driver's
frame model (`spiwords.spi_frames`: frames above 8 bits take two buffer bytes). MISO is a static AD3 level, or
follows MOSI where the loopback jumper ties them (`--with loopback`). `nss=` is a known gap (DESIGN B.14): the
masters initialise SPI_NSS_SOFT, so NSS is never driven and `test_hardware_nss` is an expected failure.

Scenarios: features/spi_ext.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, scenario, then, when

from hal_st_validation import expect
from hal_st_validation.spiwords import BITS_MAX, BITS_MIN, spi_bytes, spi_decode_words, spi_frames, spi_join_words

VARIANTS = ["interrupt", "dma", "sync"]
LIMITED_BITS = (8, 16)
DEFAULT_BITS = 8


@pytest.fixture
def ext_cfg(board_cfg):
    return board_cfg.param("spi_ext")


def skip_without_option(wiring, entry):
    option = entry.get("option")
    if option and not wiring.has(option):
        pytest.skip(f"{entry['name']} is wired with --with {option} only")


def looped(wiring, board_cfg, instance):
    """MISO tied to MOSI by the loopback jumper."""
    mosi, miso = (board_cfg.resolve_pin(instance[key]) for key in ("mosi", "miso"))
    return miso in wiring.jumpered_to(mosi, {"loopback"})


def prepare(ad3, need, wiring, board_cfg, instance, select="cs"):
    """The DIOs of the instance; MISO driven high unless the loopback jumper drives it."""
    skip_without_option(wiring, instance)
    dios = {key: need.dio(instance[key]) for key in ("clk", "mosi", "miso", select)}
    loop = looped(wiring, board_cfg, instance)
    if not loop:
        ad3.dio.drive(dios["miso"], 1)
    return dios, loop


def open_master(fw, board_cfg, ext_cfg, instance, variant="dma", select="cs", **options):
    driver = board_cfg.param("spi.variants")[variant]
    fw.spi.open(
        instance["index"],
        clk=instance["clk"],
        mosi=instance["mosi"],
        miso=instance["miso"],
        baud=ext_cfg["baud"],
        dma=driver["dma"],
        sync=driver["sync"],
        **{select: instance[select]},
        **options,
    )
    return expect.spi_clock(board_cfg.clock("spi", instance["index"]), ext_cfg["baud"])


def capture_transfer(fw, ad3, index, payload, clock, trigger):
    """`spi.xfer` of `payload` under the logic analyzer: the reply and the capture."""
    duration = len(payload) * 8 / clock * 4 + 200e-6
    rate = min(ad3.logic.clock_hz, 20 * clock, ad3.logic.buffer_size / duration)
    if rate < 4 * clock:
        pytest.skip("the transfer does not fit the logic analyzer buffer")
    pending = ad3.logic.arm(rate, int(duration * rate), trigger=trigger, pretrigger=0.05)
    received = fw.spi.xfer(index, payload)
    return received, pending.wait(timeout=2.0)


def listed_size(values):
    """The frame size is one the instance lists (`bits`)."""
    return "instance" not in values or "bits" not in values or values["bits"] in values["instance"]["bits"]


def decode(result, dios, bits=DEFAULT_BITS, msb_first=True):
    lines = [result.channel(dios[key]) for key in ("clk", "mosi", "miso", "cs")]
    return spi_join_words(spi_decode_words(*lines, mode=0, bits=bits, msb_first=msb_first))


@pytest.mark.board_params("instance", "spi_ext.instances")
@pytest.mark.board_params("lsb", "spi_ext.lsb")
@pytest.mark.board_params("variant", values=VARIANTS)
@scenario("spi_ext.feature", "The bit order holds on every driver")
def test_bit_order(instance, lsb, variant):
    pass


@pytest.mark.board_params("instance", "spi_ext.instances")
@pytest.mark.board_params("bits", values=list(range(BITS_MIN, BITS_MAX + 1)))
@pytest.mark.constraint(valid=listed_size)
@scenario("spi_ext.feature", "Every listed frame size clocks frames of that size")
def test_frame_sizes(instance, bits):
    pass


@pytest.mark.board_params("instance", "spi_ext.instances")
@scenario("spi_ext.feature", "A master reopened at 8 bits after a 16-bit open runs 8-bit frames")
def test_8_bit_after_16_bit_reopen(instance):
    pass


@pytest.mark.board_params("instance", "spi_ext.instances")
@pytest.mark.board_params("variant", values=VARIANTS)
@scenario("spi_ext.feature", "The hardware slave select is low while the master clocks")
def test_hardware_nss(instance, variant):
    pass


@pytest.mark.board_params("instance", "spi_ext.instances")
@scenario("spi_ext.feature", "Frame sizes the driver or the instance cannot run are refused")
def test_open_frame_sizes(instance):
    pass


@pytest.mark.board_params("instance", "spi_ext.instances")
@scenario("spi_ext.feature", "The hardware slave select must be the instance's and excludes the chip select")
def test_open_slave_select(instance):
    pass


@given(
    "the clock, MOSI, MISO and chip select of the instance are wired to DIOs, MISO driven high unless the loopback jumper ties it to MOSI",
    target_fixture="wired",
)
def wired_with_cs(ad3, need, wiring, board_cfg, instance):
    return prepare(ad3, need, wiring, board_cfg, instance)


@given(
    "the clock, MOSI, MISO and NSS of the instance are wired to DIOs, MISO driven high unless the loopback jumper ties it to MOSI",
    target_fixture="wired",
)
def wired_with_nss(ad3, need, wiring, board_cfg, instance):
    return prepare(ad3, need, wiring, board_cfg, instance, select="nss")


@given("the instance has 16-bit frames")
def has_16_bit(instance):
    if 16 not in instance["bits"]:
        pytest.skip(f"{instance['name']} has no 16-bit frames")


@given("the AD3 pulls NSS up")
def nss_pulled_up(ad3, wired):
    dios, _ = wired
    ad3.dio.pull(up=[dios["nss"]])


@when("the master is opened with the variant and the bit order", target_fixture="clock")
def open_bit_order(fw, board_cfg, ext_cfg, instance, lsb, variant):
    return open_master(fw, board_cfg, ext_cfg, instance, variant, lsb=bool(lsb))


@when("the master is opened with the frame size", target_fixture="clock")
def open_frame_size(fw, board_cfg, ext_cfg, instance, bits):
    return open_master(fw, board_cfg, ext_cfg, instance, bits=bits)


@when("the master is opened with 16-bit frames")
def open_16_bit(fw, board_cfg, ext_cfg, instance):
    open_master(fw, board_cfg, ext_cfg, instance, bits=16)


@when("the master transfers 34 12 78 56")
def xfer_16_bit(fw, instance):
    fw.spi.xfer(instance["index"], b"\x34\x12\x78\x56")


@when("the master is closed")
def master_closed(fw, instance):
    fw.spi.close(instance["index"])


@when("the master is opened", target_fixture="clock")
def open_default(fw, board_cfg, ext_cfg, instance):
    return open_master(fw, board_cfg, ext_cfg, instance)


@when("the master is opened with the variant and the hardware slave select", target_fixture="clock")
def open_nss(fw, board_cfg, ext_cfg, instance, variant):
    return open_master(fw, board_cfg, ext_cfg, instance, variant, select="nss")


@when(
    "the payload is transferred while the logic analyzer records from the falling chip select, unless it cannot sample 4 times per clock",
    target_fixture="transferred",
)
def transferred_on_cs(fw, ad3, ext_cfg, instance, wired, clock):
    dios, _ = wired
    payload = bytes.fromhex(ext_cfg["payload"])
    return capture_transfer(fw, ad3, instance["index"], payload, clock, (dios["cs"], "falling"))


@when(
    "the payload is transferred while the logic analyzer records from the first clock edge, unless it cannot sample 4 times per clock",
    target_fixture="transferred",
)
def transferred_on_clk(fw, ad3, ext_cfg, instance, wired, clock):
    dios, _ = wired
    payload = bytes.fromhex(ext_cfg["payload"])
    return capture_transfer(fw, ad3, instance["index"], payload, clock, (dios["clk"], "either"))


@then("MOSI decodes, in the bit order, as the payload and MISO as the payload with the loopback jumper and 0xff bytes otherwise")
def decoded_in_bit_order(ext_cfg, lsb, wired, transferred):
    dios, loop = wired
    _, result = transferred
    payload = bytes.fromhex(ext_cfg["payload"])
    wanted = payload if loop else b"\xff" * len(payload)
    mosi, miso = decode(result, dios, msb_first=not lsb)
    assert bytes(mosi) == payload
    assert bytes(miso) == wanted


@then("the master received the payload with the loopback jumper and 0xff bytes otherwise")
def received_payload(ext_cfg, wired, transferred):
    _, loop = wired
    received, _ = transferred
    payload = bytes.fromhex(ext_cfg["payload"])
    wanted = payload if loop else b"\xff" * len(payload)
    assert received == wanted


@then(
    "MOSI decodes, at the frame size, as the frames of the payload and MISO as those frames with the loopback jumper and all ones otherwise"
)
def decoded_frames(ext_cfg, bits, wired, transferred):
    dios, loop = wired
    _, result = transferred
    payload = bytes.fromhex(ext_cfg["payload"])
    frames = spi_frames(payload, bits)
    wanted = frames if loop else [(1 << bits) - 1] * len(frames)
    mosi, miso = decode(result, dios, bits)
    assert mosi == frames
    assert miso == wanted


@then("the master received the MISO frames as buffer bytes")
def received_frames(ext_cfg, bits, wired, transferred):
    _, loop = wired
    received, _ = transferred
    payload = bytes.fromhex(ext_cfg["payload"])
    frames = spi_frames(payload, bits)
    wanted = frames if loop else [(1 << bits) - 1] * len(frames)
    assert received == spi_bytes(wanted, bits)


@then("MOSI decodes as the payload")
def decoded_8_bit(ext_cfg, wired, transferred):
    dios, _ = wired
    _, result = transferred
    payload = bytes.fromhex(ext_cfg["payload"])
    mosi, _ = decode(result, dios)
    assert bytes(mosi) == payload


@then("the clock was captured")
def clock_captured(wired, transferred):
    dios, _ = wired
    _, result = transferred
    edges = analysis.edges(result.channel(dios["clk"]))
    assert edges, "no clock captured"


@then("NSS is low at every clock edge")
def nss_low_while_clocking(wired, transferred):
    dios, _ = wired
    _, result = transferred
    nss = result.channel(dios["nss"])
    edges = analysis.edges(result.channel(dios["clk"]))
    assert all(nss[edge.index] == 0 for edge in edges), "NSS must be low while the master clocks"


@then("NSS is high at the end of the capture")
def nss_released(wired, transferred):
    dios, _ = wired
    _, result = transferred
    nss = result.channel(dios["nss"])
    assert nss[-1] == 1, "NSS must be released after the transfer"


@then(
    "every frame size from the smallest to the largest opens with dma and closes again, unless the instance is limited and the size is "
    'not 8 or 16, which fails with "unsupported"'
)
def every_frame_size(fw, instance):
    index = instance["index"]
    pins = {key: instance[key] for key in ("clk", "mosi", "miso")}
    for bits in range(BITS_MIN, BITS_MAX + 1):
        accepted = not instance.get("limited") or bits in LIMITED_BITS
        try:
            fw.spi.open(index, **pins, dma=True, bits=bits)
        except FirmwareError as error:
            assert error.reason == "unsupported" and not accepted, f"bits={bits}: ERR {error.reason}"
            continue
        assert accepted, f"bits={bits} accepted on a limited instance"
        fw.spi.close(index)


@then(
    "opening with dma one size below the smallest or above the largest, with 16 bits without dma, synchronously or with lsb=2 fails with "
    "range, range, unsupported, unsupported and range"
)
def frame_size_refused(fw, instance):
    index = instance["index"]
    pins = {key: instance[key] for key in ("clk", "mosi", "miso")}
    for options, reason in (
        ({"dma": True, "bits": BITS_MIN - 1}, "range"),
        ({"dma": True, "bits": BITS_MAX + 1}, "range"),
        ({"bits": 16}, "unsupported"),
        ({"sync": True, "bits": 16}, "unsupported"),
        ({"lsb": 2}, "range"),
    ):
        with pytest.raises(FirmwareError) as error:
            fw.spi.open(index, **pins, **options)
        assert error.value.reason == reason, options


@then("the instance opens at the default frame size least significant bit first and closes again")
def opens_default_lsb(fw, instance):
    index = instance["index"]
    pins = {key: instance[key] for key in ("clk", "mosi", "miso")}
    fw.spi.open(index, **pins, bits=DEFAULT_BITS, lsb=True)
    fw.spi.close(index)


@then('opening with both the slave select and the chip select or with the clock as slave select fails with "usage" and "pin"')
def slave_select_refused(fw, instance):
    index = instance["index"]
    pins = {key: instance[key] for key in ("clk", "mosi", "miso")}
    for options, reason in (
        ({"nss": instance["nss"], "cs": instance["cs"]}, "usage"),
        ({"nss": instance["clk"]}, "pin"),
    ):
        with pytest.raises(FirmwareError) as error:
            fw.spi.open(index, **pins, **options)
        assert error.value.reason == reason, options


@when("the instance is opened with dma and its slave select")
def open_with_nss(fw, instance):
    index = instance["index"]
    pins = {key: instance[key] for key in ("clk", "mosi", "miso")}
    fw.spi.open(index, **pins, nss=instance["nss"], dma=True)


@then('configuring the slave select pin as a GPIO output fails with "busy"')
def nss_held(fw, instance):
    with pytest.raises(FirmwareError) as error:
        fw.gpio.cfg(instance["nss"], "out")
    assert error.value.reason == "busy", "the open master holds its NSS pin"
