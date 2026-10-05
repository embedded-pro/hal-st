"""Send-only UART (`hal::SynchronousUartStmSendOnly`, `uart.open ... sendonly=1`) against the AD3 protocol UART.

Wiring set `bundle1`: TX, RTS and CTS of every `tests.uart_sendonly.instances` entry on DIOs; the AD3 UART also
needs a transmit DIO, the instance's `rx` pin, which the firmware leaves alone. STM32WB55 builds LPUART1 through the
`SyncLpUart` constructors; STM32WBA55 has none, so its LPUART answers `ERR unsupported` and USART2 is the instance.

`SendData` returns once the last byte has left the data register (TXE), not the wire (TC): `uart.send` answers while
the last frame is still shifting out, and a `uart.close` that follows at once may cut that frame
(`test_close_right_after_send`).

Scenarios: features/uart_sendonly.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import expect


@pytest.fixture
def sendonly_cfg(board_cfg):
    cfg = board_cfg.param("uart_sendonly", None)
    if cfg is None:
        pytest.skip("tests.uart_sendonly not configured")
    return cfg


def open_sendonly(fw, instance, **options):
    fw.uart.open(instance["index"], lp=instance["lp"], tx=instance["tx"], sendonly=True, **options)


def listen(ad3, need, instance, baud):
    """Point the AD3 UART at the firmware TX pin; returns its DIO."""
    rx = need.dio(instance["tx"])
    ad3.uart.configure(tx=need.dio(instance["rx"]), rx=rx, baud=baud)
    ad3.uart.flush()
    return rx


def send_and_receive(fw, ad3, index, payload, baud):
    transfer = expect.uart_transfer_time(len(payload), baud)
    fw.uart.send(index, payload, cmd_timeout=2.0 + transfer)
    return ad3.uart.read(len(payload), timeout=transfer + 1.0)


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


@pytest.mark.usefixtures("sendonly_cfg")
@pytest.mark.board_params("instance", "uart_sendonly.instances")
@pytest.mark.board_params("baud", "uart_sendonly.bauds")
@scenario("uart_sendonly.feature", "Every payload leaves at every baud rate")
def test_transmit(instance, baud):
    pass


@pytest.mark.board_params("instance", "uart_sendonly.instances")
@scenario("uart_sendonly.feature", "The send-only driver receives nothing")
def test_receive_returns_nothing(instance):
    pass


@pytest.mark.usefixtures("sendonly_cfg")
@pytest.mark.board_params("instance", "uart_sendonly.instances")
@scenario("uart_sendonly.feature", "flow=rts drives RTS asserted and does not hold transmission")
def test_rts_only_mapping(instance):
    pass


@pytest.mark.usefixtures("sendonly_cfg")
@pytest.mark.board_params("instance", "uart_sendonly.instances")
@scenario("uart_sendonly.feature", "A close right after a send may cut only the last frame")
def test_close_right_after_send(instance):
    pass


@pytest.mark.board_params("instance", "uart_sendonly.instances")
@scenario("uart_sendonly.feature", "The RX pin is optional")
def test_rx_is_optional(instance):
    pass


@pytest.mark.board_params("instance", "uart_sendonly.instances")
@scenario("uart_sendonly.feature", "Options the send-only driver lacks are refused")
def test_open_errors(instance):
    pass


@pytest.mark.board_params("instance", "uart_sendonly.unsupported")
@scenario("uart_sendonly.feature", "An LPUART without send-only constructors is refused")
def test_unsupported_instance(instance):
    pass


@pytest.mark.board_params("instance", "uart_sendonly.instances")
@scenario("uart_sendonly.feature", "Without pins the default pins apply")
def test_default_pins(instance):
    pass


@given("the AD3 listens at the baud rate", target_fixture="ad3_rx")
def listens_at_baud(ad3, need, instance, baud):
    return listen(ad3, need, instance, baud)


@given(parsers.parse("the AD3 listens at {baud_rate:d} baud"), target_fixture="ad3_rx")
def listens_at(ad3, need, instance, baud_rate):
    return listen(ad3, need, instance, baud_rate)


@given("the AD3 listens at the cut baud rate", target_fixture="ad3_rx")
def listens_at_cut_baud(ad3, need, sendonly_cfg, instance):
    return listen(ad3, need, instance, sendonly_cfg["cut"]["baud"])


@given("the RTS pin of the instance is wired to a DIO", target_fixture="rts_dio")
def rts_wired(need, instance):
    return need.dio(instance["rts"])


@given("the AD3 pulls its receive DIO up")
def receive_pulled_up(ad3, ad3_rx):
    ad3.dio.pull(up=[ad3_rx])


@given("the instance is opened send-only at the baud rate")
def opened_at_baud(fw, instance, baud):
    open_sendonly(fw, instance, baud=baud)


@given("the instance is opened send-only with its RX pin")
def opened_with_rx(fw, instance):
    open_sendonly(fw, instance, rx=instance["rx"])


@given(parsers.parse("the instance is opened send-only at {baud_rate:d} baud with flow=rts on its RTS pin"))
def opened_with_rts(fw, instance, baud_rate):
    open_sendonly(fw, instance, baud=baud_rate, flow="rts", rts=instance["rts"])


@given("the instance is opened send-only at the cut baud rate")
def opened_at_cut_baud(fw, sendonly_cfg, instance):
    open_sendonly(fw, instance, baud=sendonly_cfg["cut"]["baud"])


@given("the instance has default pins")
def has_default_pins(instance):
    if not instance.get("default_pins"):
        pytest.skip("the instance has no default pins")


@when(parsers.parse("the AD3 writes {data}"))
def ad3_writes(ad3, data):
    ad3.uart.write(bytes.fromhex(data))


@when(
    parsers.parse(
        "the instance sends a payload of the cut payload size, counting up from {first:x} masked to {bits:d} bits, and is closed "
        "right after uart.send answers"
    ),
    target_fixture="cut_payload",
)
def send_and_close(fw, sendonly_cfg, instance, first, bits):
    cut = sendonly_cfg["cut"]
    baud = cut["baud"]
    payload = bytes((first + i) & ((1 << bits) - 1) for i in range(cut["payload"]))
    fw.uart.send(instance["index"], payload, cmd_timeout=2.0 + expect.uart_transfer_time(len(payload), baud))
    fw.uart.close(instance["index"])
    return payload


@when("the instance is opened send-only")
def opened(fw, instance):
    open_sendonly(fw, instance)


@when("the instance is closed")
def closed(fw, instance):
    fw.uart.close(instance["index"])


@then("each of the payloads sent by the firmware at the baud rate arrives unchanged at the AD3")
def payloads_arrive(fw, ad3, sendonly_cfg, instance, baud):
    for text in sendonly_cfg["payloads"]:
        payload = bytes.fromhex(text)
        assert send_and_receive(fw, ad3, instance["index"], payload, baud) == payload, f"{baud} Bd"


@then("the AD3 saw no parity errors")
def no_parity_errors(ad3):
    assert ad3.uart.parity_errors == 0


@then(parsers.parse("uart.recv of {count:d} bytes within {timeout_ms:d} ms returns nothing"))
def receives_nothing(fw, instance, count, timeout_ms):
    assert fw.uart.recv(instance["index"], timeout=timeout_ms, len=count) == b""


@then("the RTS DIO reads 0 with the AD3 pulling it up and with the AD3 pulling it down, the pulls turned off afterwards")
def rts_driven_low(ad3, rts_dio):
    ad3.dio.pull(up=[rts_dio])
    pulled_up = ad3.dio.read(rts_dio)
    ad3.dio.pull(down=[rts_dio])
    pulled_down = ad3.dio.read(rts_dio)
    ad3.dio.pull()
    assert pulled_up == pulled_down == 0, "RTS must be driven and asserted (receiver ready) by the UART"


@then(
    parsers.parse(
        "the last of the payloads sent by the firmware at {baud_rate:d} baud arrives unchanged at the AD3, flow=rts not blocking it"
    )
)
def last_payload_not_blocked(fw, ad3, sendonly_cfg, instance, baud_rate):
    payload = bytes.fromhex(sendonly_cfg["payloads"][-1])
    assert send_and_receive(fw, ad3, instance["index"], payload, baud_rate) == payload, "flow=rts must not block transmission"


@then("the AD3 reads every byte of the payload but the last unchanged, and no more bytes than the payload has")
def all_but_last_frame(ad3, sendonly_cfg, cut_payload):
    payload = cut_payload
    baud = sendonly_cfg["cut"]["baud"]
    received = ad3.uart.read(len(payload), timeout=expect.uart_transfer_time(len(payload), baud) + 1.0)
    assert received[: len(payload) - 1] == payload[:-1]
    assert len(received) <= len(payload)


@then("uart.recv returns nothing")
def recv_empty(fw, instance):
    assert fw.uart.recv(instance["index"]) == b""


@then("the instance opens send-only with its RX pin")
def opens_with_rx(fw, instance):
    open_sendonly(fw, instance, rx=instance["rx"])


@then("opening the instance send-only with each of the option sets it lacks fails with the reason of the set")
def options_refused(fw, instance):
    for options, reason in (
        ({"dma": True}, "usage"),
        ({"duplex": True}, "usage"),
        ({"sync": True}, "usage"),
        ({"flow": "rts"}, "usage"),
        ({"parity": "even"}, "unsupported"),
        ({"swap": True}, "unsupported"),
        ({"flow": "cts", "cts": instance["cts"]}, "unsupported"),
        ({"flow": "rtscts", "rts": instance["rts"], "cts": instance["cts"]}, "unsupported"),
        ({"rts": instance["tx"], "flow": "rts"}, "pin"),
    ):
        expect_error(reason, open_sendonly, fw, instance, **options)


@then(parsers.parse('opening the instance send-only on its RX pin without its TX pin fails with "{reason}"'))
def tx_needed(fw, instance, reason):
    expect_error(reason, fw.uart.open, instance["index"], lp=instance["lp"], sendonly=True, rx=instance["rx"])


@then(parsers.parse('opening the instance send-only fails with "{reason}"'))
def sendonly_refused(fw, instance, reason):
    expect_error(reason, open_sendonly, fw, instance)


@then("the instance opens send-only without pins")
def opens_on_default_pins(fw, instance):
    fw.uart.open(instance["index"], lp=instance["lp"], sendonly=True)
