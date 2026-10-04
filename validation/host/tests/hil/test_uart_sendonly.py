"""Send-only UART (`hal::SynchronousUartStmSendOnly`, `uart.open ... sendonly=1`) against the AD3 protocol UART.

Wiring set `bundle1`: TX, RTS and CTS of every `tests.uart_sendonly.instances` entry on DIOs; the AD3 UART also
needs a transmit DIO, the instance's `rx` pin, which the firmware leaves alone. STM32WB55 builds LPUART1 through the
`SyncLpUart` constructors; STM32WBA55 has none, so its LPUART answers `ERR unsupported` and USART2 is the instance.

`SendData` returns once the last byte has left the data register (TXE), not the wire (TC): `uart.send` answers while
the last frame is still shifting out, and a `uart.close` that follows at once may cut that frame
(`test_close_right_after_send`).
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

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


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart_sendonly.instances")
@pytest.mark.board_params("baud", "uart_sendonly.bauds")
def test_transmit(fw, ad3, need, sendonly_cfg, instance, baud):
    listen(ad3, need, instance, baud)
    open_sendonly(fw, instance, baud=baud)
    for text in sendonly_cfg["payloads"]:
        payload = bytes.fromhex(text)
        assert send_and_receive(fw, ad3, instance["index"], payload, baud) == payload, f"{baud} Bd"
    assert ad3.uart.parity_errors == 0


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart_sendonly.instances")
def test_receive_returns_nothing(fw, ad3, need, instance):
    """`ReceiveData` of the send-only driver fails at once: bytes on the `rx` line never arrive."""
    listen(ad3, need, instance, 115200)
    open_sendonly(fw, instance, rx=instance["rx"])
    ad3.uart.write(b"\x55\xaa")
    assert fw.uart.recv(instance["index"], timeout=100, len=2) == b""


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart_sendonly.instances")
def test_rts_only_mapping(fw, ad3, need, sendonly_cfg, instance):
    """`flow=rts` maps RTS and nothing else: the RTS pin is driven by the UART (it follows neither AD3 pull) and a
    deasserted CTS does not hold the transmitter."""
    baud = 115200
    rts, cts = need.dio(instance["rts"]), need.dio(instance["cts"])
    listen(ad3, need, instance, baud)
    ad3.dio.drive(cts, 1)
    open_sendonly(fw, instance, baud=baud, flow="rts", rts=instance["rts"])
    ad3.dio.pull(up=[rts])
    pulled_up = ad3.dio.read(rts)
    ad3.dio.pull(down=[rts])
    pulled_down = ad3.dio.read(rts)
    assert pulled_up == pulled_down, "RTS must be driven by the UART"
    payload = bytes.fromhex(sendonly_cfg["payloads"][-1])
    assert send_and_receive(fw, ad3, instance["index"], payload, baud) == payload, "CTS deasserted must not hold the data"


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart_sendonly.instances")
def test_close_right_after_send(fw, ad3, need, sendonly_cfg, instance):
    """`uart.close` right after `uart.send`: every frame but the last arrives intact; the last one may be cut (the
    driver waits for TXE, not TC)."""
    cut = sendonly_cfg["cut"]
    baud = cut["baud"]
    rx = listen(ad3, need, instance, baud)
    ad3.dio.pull(up=[rx])
    open_sendonly(fw, instance, baud=baud)
    payload = bytes((0x30 + i) & 0x7F for i in range(cut["payload"]))
    fw.uart.send(instance["index"], payload, cmd_timeout=2.0 + expect.uart_transfer_time(len(payload), baud))
    fw.uart.close(instance["index"])
    received = ad3.uart.read(len(payload), timeout=expect.uart_transfer_time(len(payload), baud) + 1.0)
    assert received[: len(payload) - 1] == payload[:-1]
    assert len(received) <= len(payload)


@pytest.mark.board_params("instance", "uart_sendonly.instances")
def test_rx_is_optional(fw, instance):
    open_sendonly(fw, instance)
    assert fw.uart.recv(instance["index"]) == b""
    fw.uart.close(instance["index"])
    open_sendonly(fw, instance, rx=instance["rx"])


@pytest.mark.board_params("instance", "uart_sendonly.instances")
def test_open_errors(fw, instance):
    """`sendonly` excludes the other driver selections and the options `SynchronousUartStmSendOnly` lacks."""
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
    expect_error("usage", fw.uart.open, instance["index"], lp=instance["lp"], sendonly=True, rx=instance["rx"])


@pytest.mark.board_params("instance", "uart_sendonly.unsupported")
def test_unsupported_instance(fw, instance):
    """LPUARTs without send-only constructors (STM32WBA)."""
    expect_error("unsupported", open_sendonly, fw, instance)


@pytest.mark.board_params("instance", "uart_sendonly.instances")
def test_default_pins(fw, instance):
    """Without pins the defaults of the instance apply (LPUART1), as for the other drivers."""
    if not instance.get("default_pins"):
        pytest.skip("the instance has no default pins")
    fw.uart.open(instance["index"], lp=instance["lp"], sendonly=True)
