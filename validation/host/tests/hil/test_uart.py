"""UART (`hal::UartStm`, `UartStmDma`, `UartStmDuplexDma`, `SynchronousUartStm`) against the AD3 protocol UART.

Wiring set `bundle1`: TX, RX, RTS and CTS of every `tests.uart.instances` entry on DIOs; the logic analyzer also
records the firmware TX line to decode the frames and measure the bit rate. USART1 is the terminal, so the
instances under test are LPUART1 (both boards) and USART2 (NUCLEO-WBA55CG). A receive overrun drops bytes
(`UartStm` clears ORE and goes on), so a streaming failure names it.
"""

from __future__ import annotations

import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.protocol import format_command
from ad3_waveforms_bench.terminal import FirmwareError

from hal_st_validation import expect
from hal_st_validation.firmware import settle

OVERRUN_HINT = "bytes missing: a receive overrun drops them"


@pytest.fixture
def uart_cfg(board_cfg):
    return board_cfg.param("uart")


def variant_of_instance(values):
    """The variant must be one the instance offers; `sync=1` takes `parity=none` only; asynchronous drivers
    take `flow=rtscts` only."""
    instance, variant = values.get("instance"), values.get("variant")
    if instance is not None and variant is not None and variant not in instance["variants"]:
        return False
    if variant == "sync" and values.get("parity", "none") != "none":
        return False
    return variant in (None, "sync") or values.get("flow", "rtscts") == "rtscts"


def options_of(uart_cfg, variant):
    driver = uart_cfg["variants"][variant]
    return {"dma": driver["dma"], "duplex": driver["duplex"], "sync": driver["sync"]}


def open_pair(fw, ad3, need, uart_cfg, instance, baud, parity="none", variant="interrupt", **extra):
    """Open the firmware UART and point the AD3 UART at its pins; returns the DIO of the firmware TX line."""
    ad3_rx = need.dio(instance["tx"])
    ad3_tx = need.dio(instance["rx"])
    fw.uart.open(
        instance["index"],
        lp=instance["lp"],
        tx=instance["tx"],
        rx=instance["rx"],
        baud=baud,
        parity=parity,
        **options_of(uart_cfg, variant),
        **extra,
    )
    ad3.uart.configure(tx=ad3_tx, rx=ad3_rx, baud=baud, parity=parity)
    ad3.uart.flush()
    fw.uart.recv(instance["index"])
    return ad3_rx


def firmware_to_ad3(fw, ad3, index, payload, baud, parity="none"):
    ad3.uart.flush()
    transfer = expect.uart_transfer_time(len(payload), baud, parity)
    fw.uart.send(index, payload, cmd_timeout=2.0 + transfer)
    received = ad3.uart.read(len(payload), timeout=transfer + 1.0)
    assert received == payload
    assert ad3.uart.parity_errors == 0


def ad3_to_firmware(fw, ad3, index, payload, baud, parity="none"):
    fw.uart.recv(index)
    ad3.uart.write(payload)
    wait_ms = min(10000, int(expect.uart_transfer_time(len(payload), baud, parity) * 1000) + 500)
    assert fw.uart.recv(index, timeout=wait_ms, len=len(payload)) == payload


def measured_bit_rate(bits, starts, rate, baud):
    """Bit rate from each frame's start-bit edge to the edge that starts data bit 7 (8 bit times for 0x55)."""
    per_bit = rate / baud
    edges = [edge.index for edge in analysis.edges(bits)]
    times = []
    for start in starts:
        target = start + 8 * per_bit
        closest = min(edges, key=lambda index: abs(index - target))
        if abs(closest - target) < per_bit / 2:
            times.append((closest - start) / 8 / rate)
    assert times, "no complete frames to measure"
    return 1 / statistics.fmean(times)


def check_tx_waveform(fw, ad3, uart_cfg, index, dio, baud, parity):
    """Decode the firmware TX line: the bytes, the parity and stop bits, and the bit rate."""
    payload = b"\x55" * 16
    duration = expect.uart_transfer_time(len(payload), baud, parity) * 1.5 + 2e-3
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / duration)
    if rate < 16 * baud:
        pytest.skip(f"{baud} baud does not fit the logic analyzer buffer at 16 samples per bit")
    capture = ad3.logic.arm(rate, int(duration * rate), trigger=(dio, "falling"), pretrigger=0.01)
    fw.uart.send(index, payload)
    result = capture.wait(timeout=duration + 2.0)
    decoded = result.uart(dio, baud, parity, 1)
    assert bytes(byte.value for byte in decoded) == payload
    assert not any(byte.parity_error or byte.framing_error for byte in decoded), "parity or framing error on the wire"
    measured = measured_bit_rate(result.channel(dio), [byte.start_index for byte in decoded], result.rate, baud)
    assert measured == pytest.approx(baud, rel=uart_cfg["bit_rate_tolerance"])


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.transfer")
@pytest.mark.constraint(valid=variant_of_instance)
def test_transfer(fw, ad3, need, uart_cfg, instance, baud, parity, variant):
    tx_dio = open_pair(fw, ad3, need, uart_cfg, instance, baud, parity, variant)
    for text in uart_cfg["payloads"]:
        payload = bytes.fromhex(text)
        firmware_to_ad3(fw, ad3, instance["index"], payload, baud, parity)
        ad3_to_firmware(fw, ad3, instance["index"], payload, baud, parity)
    check_tx_waveform(fw, ad3, uart_cfg, instance["index"], tx_dio, baud, parity)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.large")
@pytest.mark.constraint(valid=variant_of_instance)
def test_large_payloads(fw, ad3, need, board_cfg, uart_cfg, instance, baud, variant):
    open_pair(fw, ad3, need, uart_cfg, instance, baud, variant=variant)
    prefix = format_command("uart.send", instance["index"])
    size = min(uart_cfg["large_payload"], expect.max_hex_payload(board_cfg.terminal.max_command_length, prefix))
    firmware_to_ad3(fw, ad3, instance["index"], bytes(range(256))[:size], baud)
    inbound = uart_cfg["large_payload_to_firmware"]
    ad3_to_firmware(fw, ad3, instance["index"], bytes((i * 7) & 0xFF for i in range(inbound)), baud)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.large")
@pytest.mark.constraint(valid=variant_of_instance)
def test_full_duplex_stream(fw, ad3, need, uart_cfg, instance, baud, variant):
    """Both directions at once, round after round: nothing may be lost or reordered."""
    open_pair(fw, ad3, need, uart_cfg, instance, baud, variant=variant)
    size = uart_cfg["stream_size"]
    transfer = expect.uart_transfer_time(size, baud)
    for round_number in range(uart_cfg["stream_rounds"]):
        outbound = bytes((round_number * 31 + i) & 0xFF for i in range(size))
        inbound = bytes((round_number * 17 + 3 * i) & 0xFF for i in range(size))
        pending = fw.terminal.begin(format_command("uart.send", instance["index"], outbound), timeout=2.0 + transfer)
        try:
            ad3.uart.write(inbound)
            received = ad3.uart.read(size, timeout=transfer + 1.0)
        finally:
            response = settle(pending)
        assert response is not None and response.ok, f"round {round_number}: uart.send {response}"
        assert received == outbound, f"round {round_number}: firmware to AD3"
        assert fw.uart.recv(instance["index"], timeout=int(transfer * 1000) + 500, len=size) == inbound, (
            f"round {round_number}: AD3 to firmware ({OVERRUN_HINT})"
        )


def open_with_flow(fw, ad3, need, uart_cfg, instance, flow, variant):
    """Open with `flow`; CTS (when used) starts deasserted (high). Returns the RTS and CTS DIOs (None if unused)."""
    uses_rts, uses_cts = flow in ("rts", "rtscts"), flow in ("cts", "rtscts")
    rts = need.dio(instance["rts"]) if uses_rts else None
    cts = need.dio(instance["cts"]) if uses_cts else None
    if cts is not None:
        ad3.dio.drive(cts, 1)
    open_pair(
        fw,
        ad3,
        need,
        uart_cfg,
        instance,
        uart_cfg["flow_baud"],
        variant=variant,
        rts=instance["rts"] if uses_rts else None,
        cts=instance["cts"] if uses_cts else None,
        flow=flow,
    )
    return rts, cts


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.flow")
@pytest.mark.constraint(valid=variant_of_instance)
def test_flow_control(fw, ad3, need, uart_cfg, instance, flow, variant):
    """CTS deasserted (high) holds the firmware's transmitter; RTS is asserted (low) while it can receive. A
    stalled send is released before anything else happens, so `sync=1` (which blocks the firmware) recovers."""
    baud = uart_cfg["flow_baud"]
    rts, cts = open_with_flow(fw, ad3, need, uart_cfg, instance, flow, variant)
    payload = bytes.fromhex("0123456789abcdef")
    if rts is not None:
        assert ad3.dio.read(rts) == 0, "RTS must be asserted (low) while the firmware can receive"
        ad3_to_firmware(fw, ad3, instance["index"], payload, baud)
    if cts is None:
        firmware_to_ad3(fw, ad3, instance["index"], payload, baud)
        return
    pending = fw.terminal.begin(format_command("uart.send", instance["index"], payload), timeout=5.0)
    try:
        held = ad3.uart.read(len(payload), timeout=0.2)
    finally:
        ad3.dio.drive(cts, 0)
    try:
        released = ad3.uart.read(len(payload), timeout=1.0)
    finally:
        response = settle(pending)
    assert held == b"", "data sent while CTS is deasserted"
    assert released == payload
    assert response is not None and response.ok, f"uart.send {response}"


def stall(fw, ad3, need, uart_cfg, instance, variant):
    """Open with flow control and CTS held off, then send: the driver never completes. Returns the CTS DIO."""
    _, cts = open_with_flow(fw, ad3, need, uart_cfg, instance, "rtscts", variant)
    payload = bytes(range(uart_cfg["stall_payload"]))
    transfer = expect.uart_transfer_time(len(payload), uart_cfg["flow_baud"])
    with pytest.raises(FirmwareError) as error:
        fw.uart.send(instance["index"], payload, cmd_timeout=3.0 + transfer)
    assert error.value.reason == "timeout", "uart.send must answer ERR timeout when the driver never completes"
    return cts, payload


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.stall")
@pytest.mark.constraint(valid=variant_of_instance)
def test_send_timeout_while_cts_held(fw, ad3, need, uart_cfg, instance, variant):
    """PROTOCOL: `uart.send` answers `ERR timeout` if the driver never completes; releasing CTS lets the stalled
    data out and the instance keeps working."""
    try:
        cts, payload = stall(fw, ad3, need, uart_cfg, instance, variant)
    finally:
        ad3.dio.drive(need.dio(instance["cts"]), 0)
    assert ad3.uart.read(len(payload), timeout=1.0) == payload, "the stalled data must leave once CTS is asserted"
    firmware_to_ad3(fw, ad3, instance["index"], b"\xa5\x5a", uart_cfg["flow_baud"])


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.stall")
@pytest.mark.constraint(valid=variant_of_instance)
def test_close_during_stalled_send(fw, ad3, need, uart_cfg, instance, variant):
    """Closing during a stalled send and opening again gives a working instance (each open rebuilds it)."""
    try:
        stall(fw, ad3, need, uart_cfg, instance, variant)
        fw.uart.close(instance["index"])
    finally:
        ad3.dio.drive(need.dio(instance["cts"]), 0)
    time.sleep(0.05)
    open_pair(fw, ad3, need, uart_cfg, instance, uart_cfg["flow_baud"], variant=variant)
    fw.system.ping()
    payload = bytes.fromhex(uart_cfg["payloads"][-1])
    firmware_to_ad3(fw, ad3, instance["index"], payload, uart_cfg["flow_baud"])
    ad3_to_firmware(fw, ad3, instance["index"], payload, uart_cfg["flow_baud"])


@pytest.mark.ad3
@pytest.mark.board_params("instance", "uart.instances")
def test_reopen_with_other_settings(fw, ad3, need, uart_cfg, instance):
    for baud, parity in uart_cfg["reopen_settings"]:
        open_pair(fw, ad3, need, uart_cfg, instance, baud, parity)
        firmware_to_ad3(fw, ad3, instance["index"], b"\x5a\xa5", baud, parity)
        fw.uart.close(instance["index"])
        time.sleep(0.01)


@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.board_params("baud", "uart.open_bauds")
def test_open_bauds(fw, board_cfg, instance, baud):
    """`uart.open` succeeds exactly where the divider fits the baud-rate register at the kernel clock (and below
    the HAL limit of the family)."""
    lp = bool(instance["lp"])
    clock = board_cfg.clock("uart", expect.uart_clock_name(instance["index"], lp))
    fits = expect.uart_baud_fits(clock, baud, lp, expect.uart_baud_max(board_cfg.family))
    try:
        fw.uart.open(instance["index"], lp=lp, tx=instance["tx"], rx=instance["rx"], baud=baud)
    except FirmwareError as error:
        assert error.reason == "range" and not fits, f"ERR {error.reason} although {baud} Bd fits"
        return
    assert fits, f"{baud} Bd accepted outside the limits"
    fw.uart.close(instance["index"])


def open_errors(instance):
    pins = {"tx": instance["tx"], "rx": instance["rx"]}
    cases = [
        ({**pins, "parity": "mark"}, "usage"),
        ({**pins, "flow": "sideways"}, "usage"),
        ({**pins, "baud": "fast"}, "usage"),
        ({**pins, "baud": 299}, "range"),
        ({**pins, "baud": 12000001}, "range"),
        ({**pins, "dma": 1, "sync": 1}, "usage"),
        ({**pins, "dma": 1, "duplex": 1}, "usage"),
        ({**pins, "flow": "rtscts"}, "usage"),
        ({**pins, "flow": "rtscts", "rts": instance["rts"]}, "usage"),
        ({**pins, "rts": instance["rts"]}, "usage"),
        ({**pins, "flow": "rts"}, "usage"),
        ({**pins, "flow": "rts", "rts": instance["rts"]}, "unsupported"),
        ({**pins, "flow": "cts", "cts": instance["cts"]}, "unsupported"),
        ({**pins, "sync": 1, "parity": "even"}, "unsupported"),
        ({**pins, "sync": 1, "swap": 1}, "unsupported"),
        ({"tx": instance["rx"], "rx": instance["tx"]}, "pin"),
        ({"tx": instance["tx"]}, "usage"),
    ]
    if instance["lp"]:
        cases += [({**pins, "duplex": 1}, "unsupported"), ({**pins, "sync": 1}, "unsupported")]
    return cases


@pytest.mark.board_params("instance", "uart.instances")
def test_open_errors(fw, instance):
    for options, reason in open_errors(instance):
        with pytest.raises(FirmwareError) as error:
            fw.command("uart.open", instance["index"], lp=instance["lp"], **{key: fw_pin(fw, key, value) for key, value in options.items()})
        assert error.value.reason == reason, options
    with pytest.raises(FirmwareError) as error:
        fw.command("uart.open", instance["index"], lp=instance["lp"], stop=2)
    assert error.value.reason == "usage", "no stop key"


def fw_pin(fw, key, value):
    return fw.pin(value) if key in ("tx", "rx", "rts", "cts") else value


@pytest.mark.board_params("instance", "uart.instances")
def test_default_pins(fw, instance):
    """Only LPUART1 has default pins; every other instance needs `tx` and `rx`."""
    if instance.get("default_pins"):
        fw.uart.open(instance["index"], lp=instance["lp"])
        fw.uart.close(instance["index"])
        return
    with pytest.raises(FirmwareError) as error:
        fw.uart.open(instance["index"], lp=instance["lp"])
    assert error.value.reason == "usage"


def test_one_uart_at_a_time(fw, uart_cfg):
    instances = uart_cfg["instances"]
    if len(instances) < 2:
        pytest.skip("one UART under test on this board")
    first, second = instances[:2]
    fw.uart.open(first["index"], lp=first["lp"], tx=first["tx"], rx=first["rx"])
    with pytest.raises(FirmwareError) as error:
        fw.uart.open(second["index"], lp=second["lp"], tx=second["tx"], rx=second["rx"])
    assert error.value.reason == "busy"


@pytest.mark.board_params("instance", "uart.instances")
def test_send_and_receive_limits(fw, uart_cfg, instance):
    with pytest.raises(FirmwareError) as error:
        fw.uart.send(instance["index"], b"\x55")
    assert error.value.reason == "notopen"
    fw.uart.open(instance["index"], lp=instance["lp"], tx=instance["tx"], rx=instance["rx"])
    for line, reason in (
        (f"uart.send {instance['index']} -", "usage"),
        (f"uart.send {instance['index']} 5", "usage"),
        (f"uart.send {instance['index']} " + "00" * (uart_cfg["large_payload"] + 1), "range"),
        (f"uart.recv {instance['index']} timeout=10001", "range"),
        (f"uart.recv {instance['index']} len=0", "range"),
        (f"uart.recv {instance['index']} len=257", "range"),
    ):
        with pytest.raises(FirmwareError) as error:
            fw.terminal.command(line)
        assert error.value.reason == reason, line


# These run last and reset the board, so a swap that survived a close cannot leak into other tests.
@pytest.fixture
def reset_afterwards(fw, board_cfg):
    yield
    boot_timeout = board_cfg.param("system.boot_timeout", 5.0)
    fw.terminal.drain_events()
    try:
        fw.system.ping()
        fw.system.reset(timeout=boot_timeout)
    except Exception:  # noqa: BLE001 - the board may be rebooting right now
        fw.system.wait_boot(timeout=boot_timeout)


@pytest.mark.ad3
@pytest.mark.resets_board
@pytest.mark.usefixtures("reset_afterwards")
@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.swap")
@pytest.mark.constraint(valid=variant_of_instance)
def test_swap(fw, ad3, need, uart_cfg, instance, variant):
    """`swap=1` exchanges the functions of the two pins: the firmware transmits on its `rx` pin."""
    baud = 115200
    tx_line, rx_line = need.dio(instance["rx"]), need.dio(instance["tx"])
    fw.uart.open(
        instance["index"], lp=instance["lp"], tx=instance["tx"], rx=instance["rx"], baud=baud, swap=True, **options_of(uart_cfg, variant)
    )
    ad3.uart.configure(tx=rx_line, rx=tx_line, baud=baud)
    ad3.uart.flush()
    fw.uart.recv(instance["index"])
    payload = bytes.fromhex(uart_cfg["payloads"][-1])
    firmware_to_ad3(fw, ad3, instance["index"], payload, baud)
    ad3_to_firmware(fw, ad3, instance["index"], payload, baud)


@pytest.mark.ad3
@pytest.mark.resets_board
@pytest.mark.usefixtures("reset_afterwards")
@pytest.mark.board_params("instance", "uart.instances")
def test_swap_does_not_survive_close(fw, ad3, need, uart_cfg, instance):
    """After a `swap=1` session is closed, a plain open transmits on its `tx` pin again."""
    baud = 115200
    pins = {"lp": instance["lp"], "tx": instance["tx"], "rx": instance["rx"], "baud": baud}
    fw.uart.open(instance["index"], swap=True, **pins)
    fw.uart.close(instance["index"])
    fw.uart.open(instance["index"], **pins)
    ad3.uart.configure(tx=need.dio(instance["rx"]), rx=need.dio(instance["tx"]), baud=baud)
    ad3.uart.flush()
    payload = bytes.fromhex(uart_cfg["payloads"][-1])
    firmware_to_ad3(fw, ad3, instance["index"], payload, baud)
