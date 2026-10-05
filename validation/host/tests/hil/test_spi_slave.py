"""SPI slave (`hal::SpiSlaveStmDma`, PROTOCOL.md "SPI slave"), clocked by the AD3 SPI master and, with
`--with spiloop`, by the board's own SPI master on the other instance.

The AD3 master runs mode 0, MSB first, with its chip select on the slave's NSS (`tests.spis.instances`; an entry
with `option` is reached only through that option's jumpers). Slave and master send different payloads, so each
side checks what the other received; results above 128 bytes are compared by CRC. Every case may run with the
spiloop jumpers fitted: they tie the pins to the other SPI instance, which stays unconfigured.

Scenarios: features/spi_slave.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.firmware import quiesce, settle
from hal_st_validation.groups.spis import SPIS_HEX_MAX, SpisResult
from hal_st_validation.patterns import crc_text, generate

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


@pytest.mark.board_params("instance", "spis.instances")
@pytest.mark.board_params("freq", "spis.freqs")
@pytest.mark.board_params("length", "spis.lengths")
@pytest.mark.board_params("direction", values=DIRECTIONS)
@scenario("spi_slave.feature", "The slave transfers in full duplex, send only and receive only")
def test_ad3_master(instance, freq, length, direction):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "Frames clocked while nothing is armed do not lead the next transfer")
def test_unarmed_clocks_then_transfer(instance):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "A master clocking more than armed completes the armed transfer")
def test_master_clocks_more_than_armed(instance):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "Clocks with nothing armed complete nothing")
def test_underrun(instance):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "A short transfer stays pending until it is cancelled")
def test_short_transfer_and_cancel(instance):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "A waiting result answers when the transfer completes")
def test_result_waits_for_the_transfer(instance):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "A second result while one waits is busy")
def test_second_result_while_waiting_is_busy(instance):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "A rejected arm keeps the armed payload")
def test_rejected_arm_keeps_the_armed_payload(instance):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "The slave reopens after a close")
def test_reopen_after_close(instance):
    pass


@pytest.mark.board_params("loop", "spis.loops")
@pytest.mark.board_params("variant", "spis.loop_variants")
@pytest.mark.board_params("length", "spis.loop_lengths")
@scenario("spi_slave.feature", "The board's own master transfers with the slave on the other instance")
def test_loop(loop, variant, length):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "Invalid opens are refused")
def test_open_errors(instance):
    pass


@pytest.mark.board_params("instance", "spis.instances")
@scenario("spi_slave.feature", "Invalid arms and results are refused")
def test_arm_and_result_errors(instance):
    pass


@given("the AD3 SPI master is configured at the frequency on the pins of the slave")
def master_at_frequency(ad3, need, wiring, instance, freq):
    ad3_master(ad3, need, wiring, instance, freq)


@given("the AD3 SPI master is configured at the first frequency on the pins of the slave")
def master_at_first_frequency(ad3, need, wiring, spis_cfg, instance):
    ad3_master(ad3, need, wiring, instance, spis_cfg["freqs"][0])


@given("the slave is opened", target_fixture="index")
@when("the slave is opened", target_fixture="index")
def slave_opened(fw, instance):
    return open_slave(fw, instance)


@given("the slave sends the length of PRBS bytes seeded with the length", target_fixture="slave_tx")
def slave_payload(length):
    return generate(length, "prbs", length)


@given("the AD3 master sends the length of bytes counting up from 0x40", target_fixture="master_tx")
def master_payload(length):
    return generate(length, "inc", 0x40)


@when("the slave is armed for the direction, generating its payload in firmware unless it only receives")
def armed_for_direction(fw, index, length, direction):
    if direction == "receive":
        fw.spis.arm(index, rx=length)
    else:
        fw.spis.arm(index, len=length, pattern="prbs", seed=length, rx=0 if direction == "send" else None)


@when("the AD3 master transfers its payload", target_fixture="master_rx")
def master_transfers(ad3, master_tx):
    return ad3.spi.transfer(master_tx)


@then("the AD3 master received the payload of the slave, unless the slave only receives")
def master_received(slave_tx, direction, master_rx):
    if direction != "receive":
        assert master_rx == slave_tx


@then("the slave received the payload of the AD3 master, nothing if the slave only sends")
def slave_received(fw, index, direction, master_tx):
    expect_received(fw, index, b"" if direction == "send" else master_tx)


@given(parsers.parse('the slave sending "{slave_hex}" and the AD3 master sending "{master_hex}" exchange them'))
@when(parsers.parse('the slave sending "{slave_hex}" and the AD3 master sending "{master_hex}" exchange them'))
@then(parsers.parse('the slave sending "{slave_hex}" and the AD3 master sending "{master_hex}" exchange them'))
def exchanged(fw, ad3, index, slave_hex, master_hex):
    exchange(fw, ad3, index, bytes.fromhex(slave_hex), bytes.fromhex(master_hex))


@when(parsers.parse('the AD3 master clocks "{clocked_hex}"'))
def master_clocks(ad3, clocked_hex):
    ad3.spi.transfer(bytes.fromhex(clocked_hex))


@when(parsers.parse('the slave is armed with "{armed_hex}"'), target_fixture="slave_tx")
def armed_with(fw, index, armed_hex):
    slave_tx = bytes.fromhex(armed_hex)
    fw.spis.arm(index, slave_tx)
    return slave_tx


@when("the slave is armed with 8 bytes counting up from 0x60", target_fixture="slave_tx")
def armed_with_8_bytes(fw, index):
    slave_tx = bytes(range(0x60, 0x60 + 2 * WORD))
    fw.spis.arm(index, slave_tx)
    return slave_tx


@when("the slave is armed with 32 PRBS bytes seeded with 11", target_fixture="slave_tx")
def armed_with_prbs(fw, index):
    slave_tx = generate(8 * WORD, "prbs", 11)
    fw.spis.arm(index, slave_tx)
    return slave_tx


@then("the AD3 master clocking 8 bytes counting up from 0xc0 receives the armed bytes first", target_fixture="master_tx")
def master_clocks_more(ad3, slave_tx):
    master_tx = bytes(range(0xC0, 0xC0 + 2 * WORD))
    assert ad3.spi.transfer(master_tx)[:WORD] == slave_tx
    return master_tx


@then("the slave received the first 4 bytes of the AD3 master")
def slave_received_first_word(fw, index, master_tx):
    expect_received(fw, index, master_tx[:WORD])


@then("the slave reports no completed transfer")
def nothing_done(fw, index):
    assert fw.spis.result(index) == SpisResult(False)


@then(parsers.parse('the AD3 master clocking "{clocked_hex}" receives the first 4 armed bytes'))
def master_clocks_less(ad3, slave_tx, clocked_hex):
    assert ad3.spi.transfer(bytes.fromhex(clocked_hex)) == slave_tx[:WORD]


@then(parsers.parse("the slave reports no completed transfer after waiting {wait_ms:d} ms"))
def nothing_done_after_wait(fw, index, wait_ms):
    assert fw.spis.result(index, wait=wait_ms) == SpisResult(False)


@then("cancelling the slave transfer reports a cancelled transfer")
def cancelled(fw, index):
    assert fw.spis.cancel(index) is True


@then("cancelling it again reports nothing to cancel")
def nothing_to_cancel(fw, index):
    assert fw.spis.cancel(index) is False


@when(
    parsers.parse(
        'the AD3 master clocks "{clocked_hex}" and receives "{expected_hex}" while a result waiting up to {wait_ms:d} ms is pending'
    ),
    target_fixture="response",
)
def clocked_while_waiting(fw, ad3, index, clocked_hex, expected_hex, wait_ms):
    pending = fw.spis.begin_result(index, wait=wait_ms)
    try:
        assert ad3.spi.transfer(bytes.fromhex(clocked_hex)) == bytes.fromhex(expected_hex)
    finally:
        response = settle(pending)
    return response


@then(parsers.parse('the pending result answers that the transfer is done, having received "{received_hex}"'))
def pending_answered(response, received_hex):
    assert response is not None and SpisResult.parse(response) == SpisResult(True, rx=bytes.fromhex(received_hex))


@when(parsers.parse("a result waiting up to {wait_ms:d} ms is requested"), target_fixture="pending")
def result_requested(fw, index, wait_ms):
    return fw.spis.begin_result(index, wait=wait_ms)


@when("a second result is sent without waiting for its answer")
def second_result_sent(fw, index):
    fw.terminal.send_nowait(f"spis.result {index}")


@when("the first answer is settled as the answer to the pending result", target_fixture="busy")
def first_answer_settled(pending):
    return settle(pending)


@when("the terminal history is cleared")
def history_cleared(fw):
    fw.terminal.history.clear()


@when(parsers.parse("the terminal goes quiet for {quiet:g} s"))
def terminal_quiet(fw, quiet):
    quiesce(fw.terminal, quiet=quiet)


@then('that answer is "ERR busy"')
def answer_busy(busy):
    assert busy is not None and busy.reason == "busy"


@then(parsers.parse('the terminal history holds "{line}"'))
def history_holds(fw, line):
    assert line in fw.terminal.history


@then(
    "a second arm with a hex payload, with a payload and a receive length and with a generated payload each answers "
    '"ERR busy" before it parses the payload'
)
def second_arm_busy(fw, index):
    length = 8 * WORD
    for line in (
        f"spis.arm {index} {generate(length, 'inc', 0xA0).hex()}",
        f"spis.arm {index} 0102 rx=1",
        f"spis.arm {index} - len={length} pattern=const seed=255",
    ):
        assert fw.terminal.command(line, check=False).reason == "busy", line


@then("the AD3 master clocking 32 bytes counting up from 0x30 receives the armed payload", target_fixture="master_tx")
def master_receives_armed(ad3, slave_tx):
    master_tx = generate(8 * WORD, "inc", 0x30)
    assert ad3.spi.transfer(master_tx) == slave_tx
    return master_tx


@then("the slave received the bytes of the AD3 master")
def slave_received_all(fw, index, master_tx):
    expect_received(fw, index, master_tx)


@when("the slave is closed")
def slave_closed(fw, index):
    fw.spis.close(index)


@given("the slave of the loop is opened")
def loop_slave_opened(fw, loop):
    open_slave(fw, loop["slave"])


@given("the master of the loop is opened with its chip select at the loop baud with the driver of the variant")
def loop_master_opened(fw, board_cfg, spis_cfg, loop, variant):
    master = loop["master"]
    driver = board_cfg.param("spi.variants")[variant]
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


@when("the slave of the loop is armed with the length of PRBS bytes seeded with 7", target_fixture="slave_tx")
def loop_slave_armed(fw, loop, length):
    slave_tx = generate(length, "prbs", 7)
    fw.spis.arm(loop["slave"]["index"], slave_tx)
    return slave_tx


@then(
    "the master of the loop transferring the length of bytes counting up from 0x80 receives the armed payload",
    target_fixture="master_tx",
)
def loop_master_transfers(fw, loop, length, slave_tx):
    master_tx = generate(length, "inc", 0x80)
    assert fw.spi.xfer(loop["master"]["index"], master_tx) == slave_tx
    return master_tx


@then("the slave of the loop reports that it received the bytes of the master")
def loop_slave_received(fw, loop, master_tx):
    assert fw.spis.result(loop["slave"]["index"]) == SpisResult(True, rx=master_tx)


@then(
    "opening the slave without its slave select, with clock and MISO swapped or with the clock as slave select fails with "
    "usage, pin and pin"
)
def open_refused(fw, instance):
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


@then('opening the slave with a chip select or with an extra argument fails with "usage"')
def open_usage(fw, instance):
    index = instance["index"]
    for line in (f"spis.open {index} cs=PA0", f"spis.open {index} extra"):
        with pytest.raises(FirmwareError) as error:
            fw.terminal.command(line)
        assert error.value.reason == "usage", line


@then('opening the slave again fails with "busy"')
def open_twice(fw, instance):
    with pytest.raises(FirmwareError) as error:
        open_slave(fw, instance)
    assert error.value.reason == "busy"


@then('opening the SPI master on the instance fails with "busy"')
def master_shares(fw, instance):
    with pytest.raises(FirmwareError) as error:
        fw.spi.open(instance["index"], clk=instance["clk"], mosi=instance["mosi"], miso=instance["miso"])
    assert error.value.reason == "busy", "the SPI master and slave share the instance"


@then("the SPI master opens on the instance and closes again")
def master_opens(fw, instance):
    fw.spi.open(instance["index"], clk=instance["clk"], mosi=instance["mosi"], miso=instance["miso"])
    fw.spi.close(instance["index"])


@then(parsers.parse('arming the slave with 01 fails with "{reason}"'))
def arm_refused(fw, instance, reason):
    with pytest.raises(FirmwareError) as error:
        fw.spis.arm(instance["index"], b"\x01")
    assert error.value.reason == reason


@then("every malformed or out-of-range spis.arm, spis.result and spis.cancel fails with its reason")
def commands_refused(fw, index):
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


@then("with nothing armed the slave reports no completed transfer at once")
def nothing_armed(fw, index):
    assert fw.spis.result(index) == SpisResult(False), "nothing armed: done=0 at once"


@when("the slave is armed to receive one byte more than the hex output holds")
def armed_beyond_hex(fw, index):
    fw.spis.arm(index, rx=SPIS_HEX_MAX + 1)


@then('a result without waiting fails with "range"')
def hex_result_refused(fw, index):
    with pytest.raises(FirmwareError) as error:
        fw.spis.result(index, wait=0)
    assert error.value.reason == "range", "hex output holds 128 bytes"


@then(parsers.parse("a CRC result waiting {wait_ms:d} ms reports no completed transfer"))
def crc_nothing_done(fw, index, wait_ms):
    assert fw.spis.result(index, wait=wait_ms, out="crc") == SpisResult(False)


@then('arming the slave with 0102 and a receive length fails with "busy"')
def arm_with_rx_busy(fw, index):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(f"spis.arm {index} 0102 rx=1")
    assert error.value.reason == "busy", "busy before the payload: the armed DMA reads it"


@when("the slave is armed to send 01 02 only")
def armed_send_only(fw, index):
    fw.spis.arm(index, b"\x01\x02", rx=0)
