"""SPI master extensions (PROTOCOL.md "SPI master"): the bit order (`lsb=1`), frame sizes (`bits=`, a
`hal::SpiDataSizeConfiguratorStm` on `hal::SpiMasterStmDma`), an 8-bit master reopened after a 16-bit one and the
hardware slave select (`nss=`).

The master is observed with the logic analyzer (`tests.spi_ext.instances`; an entry with `option` is reached only
through that option's jumpers). Words are decoded with `spiwords` at the frame size and compared with the driver's
frame model (`spiwords.spi_frames`: frames above 8 bits take two buffer bytes). MISO is a static AD3 level, or
follows MOSI where the loopback jumper ties them (`--with loopback`). `nss=` is a known gap (DESIGN B.14): the
masters initialise SPI_NSS_SOFT, so NSS is never driven and `test_hardware_nss` is an expected failure.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import expect
from hal_st_validation.spiwords import BITS_MAX, BITS_MIN, spi_bytes, spi_decode_words, spi_frames, spi_join_words

pytestmark = [pytest.mark.uses_option("loopback"), pytest.mark.uses_option("spiloop")]

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


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spi_ext.instances")
@pytest.mark.board_params("lsb", "spi_ext.lsb")
@pytest.mark.board_params("variant", values=VARIANTS)
def test_bit_order(fw, ad3, need, wiring, board_cfg, ext_cfg, instance, lsb, variant):
    """`lsb=1` sends and receives the least significant bit first (`Config::msbFirst`), on every driver."""
    dios, loop = prepare(ad3, need, wiring, board_cfg, instance)
    clock = open_master(fw, board_cfg, ext_cfg, instance, variant, lsb=bool(lsb))
    payload = bytes.fromhex(ext_cfg["payload"])
    received, result = capture_transfer(fw, ad3, instance["index"], payload, clock, (dios["cs"], "falling"))
    wanted = payload if loop else b"\xff" * len(payload)
    mosi, miso = decode(result, dios, msb_first=not lsb)
    assert bytes(mosi) == payload
    assert bytes(miso) == wanted
    assert received == wanted


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spi_ext.instances")
@pytest.mark.board_params("bits", values=list(range(BITS_MIN, BITS_MAX + 1)))
@pytest.mark.constraint(valid=listed_size)
def test_frame_sizes(fw, ad3, need, wiring, board_cfg, ext_cfg, instance, bits):
    """`bits=<n>` clocks n-bit frames (`spiwords.spi_frames`); the received frames come back right aligned."""
    dios, loop = prepare(ad3, need, wiring, board_cfg, instance)
    clock = open_master(fw, board_cfg, ext_cfg, instance, bits=bits)
    payload = bytes.fromhex(ext_cfg["payload"])
    frames = spi_frames(payload, bits)
    received, result = capture_transfer(fw, ad3, instance["index"], payload, clock, (dios["cs"], "falling"))
    wanted = frames if loop else [(1 << bits) - 1] * len(frames)
    mosi, miso = decode(result, dios, bits)
    assert mosi == frames
    assert miso == wanted
    assert received == spi_bytes(wanted, bits)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spi_ext.instances")
def test_8_bit_after_16_bit_reopen(fw, ad3, need, wiring, board_cfg, ext_cfg, instance):
    """A master reopened at 8 bits after a 16-bit open runs 8-bit frames. Every open builds new DMA streams, whose
    constructor resets the data widths, so this cannot catch a width that sticks on a live GPDMA channel (B.4a is
    proved by review; its 32-bit encoding by test_dma.py::test_wave_32_bit)."""
    if 16 not in instance["bits"]:
        pytest.skip(f"{instance['name']} has no 16-bit frames")
    dios, loop = prepare(ad3, need, wiring, board_cfg, instance)
    open_master(fw, board_cfg, ext_cfg, instance, bits=16)
    fw.spi.xfer(instance["index"], b"\x34\x12\x78\x56")
    fw.spi.close(instance["index"])
    clock = open_master(fw, board_cfg, ext_cfg, instance)
    payload = bytes.fromhex(ext_cfg["payload"])
    received, result = capture_transfer(fw, ad3, instance["index"], payload, clock, (dios["cs"], "falling"))
    wanted = payload if loop else b"\xff" * len(payload)
    mosi, miso = decode(result, dios)
    assert bytes(mosi) == payload
    assert received == wanted


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spi_ext.instances")
@pytest.mark.board_params("variant", values=VARIANTS)
def test_hardware_nss(fw, ad3, need, wiring, board_cfg, ext_cfg, instance, variant):
    """`nss=<pin>`: NSS low while the master clocks, high after the transfer. Known gap (B.14): the masters
    initialise SPI_NSS_SOFT, so the pin (pulled up by the AD3 here) never goes low."""
    dios, _ = prepare(ad3, need, wiring, board_cfg, instance, select="nss")
    ad3.dio.pull(up=[dios["nss"]])
    clock = open_master(fw, board_cfg, ext_cfg, instance, variant, select="nss")
    payload = bytes.fromhex(ext_cfg["payload"])
    _, result = capture_transfer(fw, ad3, instance["index"], payload, clock, (dios["clk"], "either"))
    nss = result.channel(dios["nss"])
    edges = analysis.edges(result.channel(dios["clk"]))
    assert edges, "no clock captured"
    assert all(nss[edge.index] == 0 for edge in edges), "NSS must be low while the master clocks"
    assert nss[-1] == 1, "NSS must be released after the transfer"


@pytest.mark.board_params("instance", "spi_ext.instances")
def test_open_frame_sizes(fw, instance):
    """`bits` 4..16 needs `dma=1` (8 is the default and needs nothing); a limited instance (WBA55 SPI3,
    `IS_SPI_LIMITED_INSTANCE`) takes 8 and 16 only; anything else is `ERR unsupported`, outside 4..16 `ERR range`."""
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
    fw.spi.open(index, **pins, bits=DEFAULT_BITS, lsb=True)
    fw.spi.close(index)


@pytest.mark.board_params("instance", "spi_ext.instances")
def test_open_slave_select(fw, instance):
    """`nss` must offer the instance's slave select (`ERR pin`) and excludes the GPIO chip select `cs` (`ERR usage`)."""
    index = instance["index"]
    pins = {key: instance[key] for key in ("clk", "mosi", "miso")}
    for options, reason in (
        ({"nss": instance["nss"], "cs": instance["cs"]}, "usage"),
        ({"nss": instance["clk"]}, "pin"),
    ):
        with pytest.raises(FirmwareError) as error:
            fw.spi.open(index, **pins, **options)
        assert error.value.reason == reason, options
    fw.spi.open(index, **pins, nss=instance["nss"], dma=True)
    with pytest.raises(FirmwareError) as error:
        fw.gpio.cfg(instance["nss"], "out")
    assert error.value.reason == "busy", "the open master holds its NSS pin"
