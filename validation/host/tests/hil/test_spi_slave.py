"""SPI slave (`hal::SpiSlaveStmDma`, PROTOCOL.md "SPI slave"), clocked by the AD3 SPI master and, with
`--with spiloop`, by the board's own SPI master on the other instance.

The AD3 master runs mode 0, MSB first, with its chip select on the slave's NSS (`tests.spis.instances`; an entry
with `option` is reached only through that option's jumpers). Slave and master send different payloads, so each
side checks what the other received; results above 128 bytes are compared by CRC. Every case may run with the
spiloop jumpers fitted: they tie the pins to the other SPI instance, which stays unconfigured.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation.firmware import quiesce, settle
from hal_st_validation.groups.spis import SPIS_HEX_MAX, SpisResult
from hal_st_validation.patterns import crc_text, generate

pytestmark = pytest.mark.uses_option("spiloop")

DIRECTIONS = ["duplex", "send", "receive"]
WORD = 4


@pytest.fixture
def spis_cfg(board_cfg):
    return board_cfg.param("spis")


def skip_without_option(wiring, entry):
    option = entry.get("option")
    if option and not wiring.has(option):
        pytest.skip(f"{entry['name']} is wired with --with {option} only")


def open_slave(fw, instance):
    fw.spis.open(instance["index"], instance["clk"], instance["miso"], instance["mosi"], instance["nss"])
    return instance["index"]


def ad3_master(ad3, need, wiring, instance, freq):
    """The AD3 SPI master on the slave's pins."""
    skip_without_option(wiring, instance)
    dios = {key: need.dio(instance[key]) for key in ("clk", "miso", "mosi", "nss")}
    ad3.spi.configure(dios["clk"], dios["mosi"], dios["miso"], dios["nss"], freq, mode=0)


def expect_received(fw, index, data):
    if len(data) > SPIS_HEX_MAX:
        assert fw.spis.result(index, out="crc") == SpisResult(True, length=len(data), crc=crc_text(data))
    else:
        assert fw.spis.result(index) == SpisResult(True, rx=data)


def exchange(fw, ad3, index, slave_tx, master_tx):
    """One full-duplex transfer of equal lengths, checked on both sides."""
    fw.spis.arm(index, slave_tx)
    assert ad3.spi.transfer(master_tx) == slave_tx
    expect_received(fw, index, master_tx)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spis.instances")
@pytest.mark.board_params("freq", "spis.freqs")
@pytest.mark.board_params("length", "spis.lengths")
@pytest.mark.board_params("direction", values=DIRECTIONS)
def test_ad3_master(fw, ad3, need, wiring, instance, freq, length, direction):
    """Full duplex, send only (`rx=0`) and receive only (`-` with `rx=<n>`); the slave payload is generated in
    firmware (`len=`, `pattern=prbs`)."""
    ad3_master(ad3, need, wiring, instance, freq)
    index = open_slave(fw, instance)
    slave_tx = generate(length, "prbs", length)
    master_tx = generate(length, "inc", 0x40)
    if direction == "receive":
        fw.spis.arm(index, rx=length)
    else:
        fw.spis.arm(index, len=length, pattern="prbs", seed=length, rx=0 if direction == "send" else None)
    master_rx = ad3.spi.transfer(master_tx)
    if direction != "receive":
        assert master_rx == slave_tx
    expect_received(fw, index, b"" if direction == "send" else master_tx)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spis.instances")
def test_unarmed_clocks_then_transfer(fw, ad3, need, wiring, spis_cfg, instance):
    """Frames clocked while nothing is armed do not lead the next transfer (B.2: SPE off between transfers, the
    STM32WB RX FIFO drained before arming)."""
    ad3_master(ad3, need, wiring, instance, spis_cfg["freqs"][0])
    index = open_slave(fw, instance)
    exchange(fw, ad3, index, b"\x10\x11\x12\x13", b"\xa0\xa1\xa2\xa3")
    ad3.spi.transfer(b"\xde\xad\xbe\xef")
    exchange(fw, ad3, index, b"\x20\x21\x22\x23", b"\xb0\xb1\xb2\xb3")


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spis.instances")
def test_master_clocks_more_than_armed(fw, ad3, need, wiring, spis_cfg, instance):
    """Armed for 4 bytes, clocked for 8: done with the first 4 (B.2); the next transfer is exact."""
    ad3_master(ad3, need, wiring, instance, spis_cfg["freqs"][0])
    index = open_slave(fw, instance)
    slave_tx = b"\x31\x32\x33\x34"
    master_tx = bytes(range(0xC0, 0xC0 + 2 * WORD))
    fw.spis.arm(index, slave_tx)
    assert ad3.spi.transfer(master_tx)[:WORD] == slave_tx
    expect_received(fw, index, master_tx[:WORD])
    exchange(fw, ad3, index, b"\x41\x42\x43\x44", b"\xd0\xd1\xd2\xd3")


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spis.instances")
def test_underrun(fw, ad3, need, wiring, spis_cfg, instance):
    """Clocks with no transfer armed complete nothing (`done=0` at once); an armed transfer afterwards is exact."""
    ad3_master(ad3, need, wiring, instance, spis_cfg["freqs"][0])
    index = open_slave(fw, instance)
    ad3.spi.transfer(b"\x01\x02\x03\x04")
    assert fw.spis.result(index) == SpisResult(False)
    exchange(fw, ad3, index, b"\x51\x52\x53\x54", b"\xe0\xe1\xe2\xe3")


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spis.instances")
def test_short_transfer_and_cancel(fw, ad3, need, wiring, spis_cfg, instance):
    """Armed for 8 bytes, clocked for 4: `done=0` after the wait; `spis.cancel` stops it (`cancelled=1`) and the
    next transfer is exact."""
    ad3_master(ad3, need, wiring, instance, spis_cfg["freqs"][0])
    index = open_slave(fw, instance)
    slave_tx = bytes(range(0x60, 0x60 + 2 * WORD))
    fw.spis.arm(index, slave_tx)
    assert ad3.spi.transfer(b"\x71\x72\x73\x74") == slave_tx[:WORD]
    assert fw.spis.result(index, wait=100) == SpisResult(False)
    assert fw.spis.cancel(index) is True
    assert fw.spis.result(index) == SpisResult(False)
    assert fw.spis.cancel(index) is False
    exchange(fw, ad3, index, b"\x81\x82\x83\x84", b"\xf0\xf1\xf2\xf3")


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spis.instances")
def test_result_waits_for_the_transfer(fw, ad3, need, wiring, spis_cfg, instance):
    """`spis.result` armed and not done answers when the transfer completes."""
    ad3_master(ad3, need, wiring, instance, spis_cfg["freqs"][0])
    index = open_slave(fw, instance)
    fw.spis.arm(index, b"\x91\x92")
    pending = fw.spis.begin_result(index, wait=5000)
    try:
        assert ad3.spi.transfer(b"\x93\x94") == b"\x91\x92"
    finally:
        response = settle(pending)
    assert response is not None and SpisResult.parse(response) == SpisResult(True, rx=b"\x93\x94")


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spis.instances")
def test_second_result_while_waiting_is_busy(fw, ad3, need, wiring, spis_cfg, instance):
    """A `spis.result` while another waits answers `ERR busy` at once; the waiting one still answers `done=1`."""
    ad3_master(ad3, need, wiring, instance, spis_cfg["freqs"][0])
    index = open_slave(fw, instance)
    fw.spis.arm(index, b"\xa1\xa2")
    pending = fw.spis.begin_result(index, wait=5000)
    fw.terminal.send_nowait(f"spis.result {index}")
    busy = settle(pending)
    fw.terminal.history.clear()
    ad3.spi.transfer(b"\xa3\xa4")
    quiesce(fw.terminal, quiet=0.3)
    assert busy is not None and busy.reason == "busy"
    assert "OK done=1 rx=a3a4" in fw.terminal.history


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spis.instances")
def test_rejected_arm_keeps_the_armed_payload(fw, ad3, need, wiring, spis_cfg, instance):
    """An `spis.arm` while a transfer is armed answers `ERR busy` before it parses its payload: the transmit DMA
    still reads the armed payload, longer than any SPI FIFO here, and the master receives it unchanged."""
    ad3_master(ad3, need, wiring, instance, spis_cfg["freqs"][0])
    index = open_slave(fw, instance)
    length = 8 * WORD
    slave_tx = generate(length, "prbs", 11)
    master_tx = generate(length, "inc", 0x30)
    fw.spis.arm(index, slave_tx)
    for line in (
        f"spis.arm {index} {generate(length, 'inc', 0xA0).hex()}",
        f"spis.arm {index} 0102 rx=1",
        f"spis.arm {index} - len={length} pattern=const seed=255",
    ):
        assert fw.terminal.command(line, check=False).reason == "busy", line
    assert ad3.spi.transfer(master_tx) == slave_tx
    expect_received(fw, index, master_tx)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "spis.instances")
def test_reopen_after_close(fw, ad3, need, wiring, spis_cfg, instance):
    ad3_master(ad3, need, wiring, instance, spis_cfg["freqs"][0])
    index = open_slave(fw, instance)
    exchange(fw, ad3, index, b"\xb1\xb2\xb3\xb4", b"\x0b\x1b\x2b\x3b")
    fw.spis.close(index)
    open_slave(fw, instance)
    exchange(fw, ad3, index, b"\xc1\xc2\xc3\xc4", b"\x0c\x1c\x2c\x3c")


@pytest.mark.requires_option("spiloop")
@pytest.mark.board_params("loop", "spis.loops")
@pytest.mark.board_params("variant", "spis.loop_variants")
@pytest.mark.board_params("length", "spis.loop_lengths")
def test_loop(fw, board_cfg, spis_cfg, loop, variant, length):
    """The board's master (`spi.*`, the variant's driver) against the slave on the other instance; the master's
    GPIO chip select drives the slave's NSS through the jumpers."""
    master, slave = loop["master"], loop["slave"]
    driver = board_cfg.param("spi.variants")[variant]
    open_slave(fw, slave)
    fw.spi.open(
        master["index"],
        clk=master["clk"],
        mosi=master["mosi"],
        miso=master["miso"],
        cs=master["cs"],
        baud=spis_cfg["loop_baud"],
        dma=driver["dma"],
        sync=driver["sync"],
    )
    slave_tx = generate(length, "prbs", 7)
    master_tx = generate(length, "inc", 0x80)
    fw.spis.arm(slave["index"], slave_tx)
    assert fw.spi.xfer(master["index"], master_tx) == slave_tx
    assert fw.spis.result(slave["index"]) == SpisResult(True, rx=master_tx)


@pytest.mark.board_params("instance", "spis.instances")
def test_open_errors(fw, instance):
    index = instance["index"]
    pins = {key: instance[key] for key in ("clk", "miso", "mosi", "nss")}
    cases = [
        ({key: pins[key] for key in ("clk", "miso", "mosi")}, "usage"),
        ({**pins, "clk": pins["miso"], "miso": pins["clk"]}, "pin"),
        ({**pins, "nss": pins["clk"]}, "pin"),
    ]
    for options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.command("spis.open", index, **{key: fw.pin(pin) for key, pin in options.items()})
        assert error.value.reason == reason, options
    for line in (f"spis.open {index} cs=PA0", f"spis.open {index} extra"):
        with pytest.raises(FirmwareError) as error:
            fw.terminal.command(line)
        assert error.value.reason == "usage", line
    open_slave(fw, instance)
    with pytest.raises(FirmwareError) as error:
        open_slave(fw, instance)
    assert error.value.reason == "busy"
    with pytest.raises(FirmwareError) as error:
        fw.spi.open(index, clk=pins["clk"], mosi=pins["mosi"], miso=pins["miso"])
    assert error.value.reason == "busy", "the SPI master and slave share the instance"
    fw.spis.close(index)
    fw.spi.open(index, clk=pins["clk"], mosi=pins["mosi"], miso=pins["miso"])
    fw.spi.close(index)


@pytest.mark.board_params("instance", "spis.instances")
def test_arm_and_result_errors(fw, instance):
    index = instance["index"]
    with pytest.raises(FirmwareError) as error:
        fw.spis.arm(index, b"\x01")
    assert error.value.reason == "notopen"
    open_slave(fw, instance)
    for line, reason in (
        (f"spis.arm {index} -", "usage"),
        (f"spis.arm {index} - rx=0", "usage"),
        (f"spis.arm {index} 0102 rx=1", "usage"),
        (f"spis.arm {index} 0102 len=2", "usage"),
        (f"spis.arm {index} - seed=1", "usage"),
        (f"spis.arm {index} - len=1025", "range"),
        (f"spis.arm {index} - rx=1025", "range"),
        (f"spis.result {index} out=bin", "usage"),
        (f"spis.result {index} wait=10001", "range"),
        (f"spis.cancel {index} 1", "usage"),
    ):
        with pytest.raises(FirmwareError) as error:
            fw.terminal.command(line)
        assert error.value.reason == reason, line
    assert fw.spis.result(index) == SpisResult(False), "nothing armed: done=0 at once"
    fw.spis.arm(index, rx=SPIS_HEX_MAX + 1)
    with pytest.raises(FirmwareError) as error:
        fw.spis.result(index, wait=0)
    assert error.value.reason == "range", "hex output holds 128 bytes"
    assert fw.spis.result(index, wait=0, out="crc") == SpisResult(False)
    with pytest.raises(FirmwareError) as error:
        fw.spis.arm(index, b"\x01")
    assert error.value.reason == "busy"
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(f"spis.arm {index} 0102 rx=1")
    assert error.value.reason == "busy", "busy before the payload: the armed DMA reads it"
    assert fw.spis.result(index, wait=50, out="crc") == SpisResult(False)
    assert fw.spis.cancel(index) is True
    assert fw.spis.cancel(index) is False
    fw.spis.arm(index, b"\x01\x02", rx=0)
    fw.spis.close(index)
