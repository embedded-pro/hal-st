"""UART (`hal::UartStm`, `UartStmDma`, `UartStmDuplexDma`, `SynchronousUartStm`) against the AD3 protocol UART.

Wiring set `bundle1`: TX, RX, RTS and CTS of every `tests.uart.instances` entry on DIOs; the logic analyzer also
records the firmware TX line to decode the frames and measure the bit rate. USART1 is the terminal, so the
instances under test are LPUART1 (both boards) and USART2 (NUCLEO-WBA55CG). A receive overrun drops bytes
(`UartStm` clears ORE and goes on), so a streaming failure names it.

Scenarios: features/uart.feature.
"""

from __future__ import annotations

import statistics
import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.protocol import format_command
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

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


def stall(fw, ad3, need, uart_cfg, instance, variant):
    """Open with flow control and CTS held off, then send: the driver never completes. Returns the CTS DIO."""
    _, cts = open_with_flow(fw, ad3, need, uart_cfg, instance, "rtscts", variant)
    payload = bytes(range(uart_cfg["stall_payload"]))
    transfer = expect.uart_transfer_time(len(payload), uart_cfg["flow_baud"])
    with pytest.raises(FirmwareError) as error:
        fw.uart.send(instance["index"], payload, cmd_timeout=3.0 + transfer)
    assert error.value.reason == "timeout", "uart.send must answer ERR timeout when the driver never completes"
    return cts, payload


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


def fw_pin(fw, key, value):
    return fw.pin(value) if key in ("tx", "rx", "rts", "cts") else value


@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.transfer")
@pytest.mark.constraint(valid=variant_of_instance)
@scenario("uart.feature", "Payloads cross both ways and the TX line runs at the baud rate")
def test_transfer(instance, baud, parity, variant):
    pass


@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.large")
@pytest.mark.constraint(valid=variant_of_instance)
@scenario("uart.feature", "The largest payloads cross both ways")
def test_large_payloads(instance, baud, variant):
    pass


@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.large")
@pytest.mark.constraint(valid=variant_of_instance)
@scenario("uart.feature", "Both directions stream at once without loss or reordering")
def test_full_duplex_stream(instance, baud, variant):
    pass


@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.flow")
@pytest.mark.constraint(valid=variant_of_instance)
@scenario("uart.feature", "CTS holds the transmitter and RTS is asserted while the firmware can receive")
def test_flow_control(instance, flow, variant):
    pass


@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.stall")
@pytest.mark.constraint(valid=variant_of_instance)
@scenario("uart.feature", "uart.send answers ERR timeout while CTS is held, and the stalled data leaves once it is released")
def test_send_timeout_while_cts_held(instance, variant):
    pass


@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.stall")
@pytest.mark.constraint(valid=variant_of_instance)
@scenario("uart.feature", "An instance closed during a stalled send opens again and works")
def test_close_during_stalled_send(instance, variant):
    pass


@pytest.mark.board_params("instance", "uart.instances")
@scenario("uart.feature", "The instance reopens with other settings")
def test_reopen_with_other_settings(instance):
    pass


@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.board_params("baud", "uart.open_bauds")
@scenario("uart.feature", "uart.open accepts exactly the baud rates the baud-rate register fits")
def test_open_bauds(instance, baud):
    pass


@pytest.mark.board_params("instance", "uart.instances")
@scenario("uart.feature", "Invalid options are refused with their reason")
def test_open_errors(instance):
    pass


@pytest.mark.board_params("instance", "uart.instances")
@scenario("uart.feature", "Only an instance with default pins opens without pins")
def test_default_pins(instance):
    pass


@scenario("uart.feature", "Only one UART is open at a time")
def test_one_uart_at_a_time():
    pass


@pytest.mark.board_params("instance", "uart.instances")
@scenario("uart.feature", "uart.send needs an open instance, and malformed or out-of-range send and receive commands are refused")
def test_send_and_receive_limits(instance):
    pass


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


@pytest.mark.usefixtures("reset_afterwards")
@pytest.mark.board_params("instance", "uart.instances")
@pytest.mark.matrix("uart.swap")
@pytest.mark.constraint(valid=variant_of_instance)
@scenario("uart.feature", "swap=1 exchanges the functions of the two pins")
def test_swap(instance, variant):
    pass


@pytest.mark.usefixtures("reset_afterwards")
@pytest.mark.board_params("instance", "uart.instances")
@scenario("uart.feature", "swap=1 does not survive a close")
def test_swap_does_not_survive_close(instance):
    pass


@given("the instance is open against the AD3 at the baud rate with the parity and the variant", target_fixture="tx_dio")
def open_with_parity(fw, ad3, need, uart_cfg, instance, baud, parity, variant):
    return open_pair(fw, ad3, need, uart_cfg, instance, baud, parity, variant)


@given("the instance is open against the AD3 at the baud rate with the variant")
def open_with_variant(fw, ad3, need, uart_cfg, instance, baud, variant):
    open_pair(fw, ad3, need, uart_cfg, instance, baud, variant=variant)


@given(
    "the instance is open against the AD3 at the flow baud rate with the variant and the flow control on the RTS and CTS pins it uses, "
    "CTS driven high first if it uses it",
    target_fixture="flow_dios",
)
def open_with_flow_control(fw, ad3, need, uart_cfg, instance, flow, variant):
    return open_with_flow(fw, ad3, need, uart_cfg, instance, flow, variant)


@given(
    "whether the baud rate fits the baud-rate register at the kernel clock of the instance and the baud rate limit of the family",
    target_fixture="fits",
)
def baud_fits(board_cfg, instance, baud):
    lp = bool(instance["lp"])
    clock = board_cfg.clock("uart", expect.uart_clock_name(instance["index"], lp))
    return expect.uart_baud_fits(clock, baud, lp, expect.uart_baud_max(board_cfg.family))


@given("the first two of the instances, unless the board has only one", target_fixture="two_instances")
def first_two_instances(uart_cfg):
    instances = uart_cfg["instances"]
    if len(instances) < 2:
        pytest.skip("one UART under test on this board")
    return instances[:2]


@given(
    parsers.parse(
        "the instance is open with the variant and swap=1 at {baud_rate:d} baud against the AD3 receiving on the DIO of its RX pin "
        "and transmitting on the DIO of its TX pin"
    )
)
def open_swapped(fw, ad3, need, uart_cfg, instance, variant, baud_rate):
    tx_line, rx_line = need.dio(instance["rx"]), need.dio(instance["tx"])
    fw.uart.open(
        instance["index"],
        lp=instance["lp"],
        tx=instance["tx"],
        rx=instance["rx"],
        baud=baud_rate,
        swap=True,
        **options_of(uart_cfg, variant),
    )
    ad3.uart.configure(tx=rx_line, rx=tx_line, baud=baud_rate)
    ad3.uart.flush()
    fw.uart.recv(instance["index"])


@when(parsers.parse("the host waits {seconds:g} s"))
def host_waits(seconds):
    time.sleep(seconds)


@when("the instance is opened against the AD3 at the flow baud rate with the variant")
def reopen_at_flow_baud(fw, ad3, need, uart_cfg, instance, variant):
    open_pair(fw, ad3, need, uart_cfg, instance, uart_cfg["flow_baud"], variant=variant)


@when("the first of them is opened on its TX and RX pins")
def open_first(fw, two_instances):
    first = two_instances[0]
    fw.uart.open(first["index"], lp=first["lp"], tx=first["tx"], rx=first["rx"])


@when("the instance is opened on its TX and RX pins")
def open_on_pins(fw, instance):
    fw.uart.open(instance["index"], lp=instance["lp"], tx=instance["tx"], rx=instance["rx"])


@when(parsers.parse("the instance is opened on its TX and RX pins at {baud_rate:d} baud with swap=1 and closed"))
def open_swapped_and_close(fw, instance, baud_rate):
    pins = {"lp": instance["lp"], "tx": instance["tx"], "rx": instance["rx"], "baud": baud_rate}
    fw.uart.open(instance["index"], swap=True, **pins)
    fw.uart.close(instance["index"])


@when(parsers.parse("the instance is opened on its TX and RX pins at {baud_rate:d} baud"))
def open_plain(fw, instance, baud_rate):
    pins = {"lp": instance["lp"], "tx": instance["tx"], "rx": instance["rx"], "baud": baud_rate}
    fw.uart.open(instance["index"], **pins)


@when(
    parsers.parse(
        "the AD3 UART runs at {baud_rate:d} baud, transmitting on the DIO of the RX pin and receiving on the DIO of the TX pin, "
        "and is emptied"
    )
)
def ad3_listens(ad3, need, instance, baud_rate):
    ad3.uart.configure(tx=need.dio(instance["rx"]), rx=need.dio(instance["tx"]), baud=baud_rate)
    ad3.uart.flush()


@then("each of the payloads goes from the firmware to the AD3 and then from the AD3 to the firmware")
def payloads_cross_both_ways(fw, ad3, uart_cfg, instance, baud, parity):
    for text in uart_cfg["payloads"]:
        payload = bytes.fromhex(text)
        firmware_to_ad3(fw, ad3, instance["index"], payload, baud, parity)
        ad3_to_firmware(fw, ad3, instance["index"], payload, baud, parity)


@then(
    "16 bytes 55 sent by the firmware decode from its TX line without parity or framing errors, at the baud rate within the bit rate "
    "tolerance, unless the logic analyzer cannot record them at 16 samples per bit"
)
def tx_line_decodes(fw, ad3, uart_cfg, instance, tx_dio, baud, parity):
    check_tx_waveform(fw, ad3, uart_cfg, instance["index"], tx_dio, baud, parity)


@then(
    parsers.parse(
        "a payload counting up from 00, as long as one uart.send command line can carry but at most the large payload size and "
        "{byte_values:d} bytes, goes from the firmware to the AD3"
    )
)
def largest_to_ad3(fw, ad3, board_cfg, uart_cfg, instance, baud, byte_values):
    prefix = format_command("uart.send", instance["index"])
    size = min(uart_cfg["large_payload"], expect.max_hex_payload(board_cfg.terminal.max_command_length, prefix))
    firmware_to_ad3(fw, ad3, instance["index"], bytes(range(byte_values))[:size], baud)


@then("a payload of the large payload size to the firmware, counting in steps of 7, goes from the AD3 to the firmware")
def large_to_firmware(fw, ad3, uart_cfg, instance, baud):
    inbound = uart_cfg["large_payload_to_firmware"]
    ad3_to_firmware(fw, ad3, instance["index"], bytes((i * 7) & 0xFF for i in range(inbound)), baud)


@then(
    "in each of the stream rounds, while the firmware sends a block of the stream size the AD3 sends another one, uart.send succeeds "
    "and each block arrives unchanged at the other end"
)
def stream_rounds_cross(fw, ad3, uart_cfg, instance, baud):
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


@then(parsers.parse('RTS reads low and "{data}" goes from the AD3 to the firmware, if the flow control uses RTS'))
def rts_asserted(fw, ad3, uart_cfg, instance, flow_dios, data):
    rts, _ = flow_dios
    if rts is not None:
        assert ad3.dio.read(rts) == 0, "RTS must be asserted (low) while the firmware can receive"
        ad3_to_firmware(fw, ad3, instance["index"], bytes.fromhex(data), uart_cfg["flow_baud"])


@then(parsers.parse('"{data}" goes from the firmware to the AD3, if the flow control does not use CTS'))
def sends_without_cts(fw, ad3, uart_cfg, instance, flow_dios, data):
    _, cts = flow_dios
    if cts is None:
        firmware_to_ad3(fw, ad3, instance["index"], bytes.fromhex(data), uart_cfg["flow_baud"])


@then(
    parsers.parse(
        '"{data}" sent by the firmware does not reach the AD3 within {hold_s:g} s, reaches it within {release_s:g} s once the AD3 '
        "drives CTS low, and uart.send succeeds, if the flow control uses CTS"
    )
)
def cts_holds_transmitter(fw, ad3, instance, flow_dios, data, hold_s, release_s):
    _, cts = flow_dios
    if cts is None:
        return
    payload = bytes.fromhex(data)
    pending = fw.terminal.begin(format_command("uart.send", instance["index"], payload), timeout=5.0)
    try:
        held = ad3.uart.read(len(payload), timeout=hold_s)
    finally:
        ad3.dio.drive(cts, 0)
    try:
        released = ad3.uart.read(len(payload), timeout=release_s)
    finally:
        response = settle(pending)
    assert held == b"", "data sent while CTS is deasserted"
    assert released == payload
    assert response is not None and response.ok, f"uart.send {response}"


@then(
    "a send of the stall payload, by the instance open against the AD3 at the flow baud rate with the variant and RTS/CTS flow control "
    'while the AD3 drives CTS high, answers "timeout", and afterwards the AD3 drives CTS low',
    target_fixture="stalled_payload",
)
def send_stalls(fw, ad3, need, uart_cfg, instance, variant):
    try:
        _, payload = stall(fw, ad3, need, uart_cfg, instance, variant)
    finally:
        ad3.dio.drive(need.dio(instance["cts"]), 0)
    return payload


@then(
    "a send of the stall payload, by the instance open against the AD3 at the flow baud rate with the variant and RTS/CTS flow control "
    'while the AD3 drives CTS high, answers "timeout" and the instance is closed, and afterwards the AD3 drives CTS low'
)
def send_stalls_and_close(fw, ad3, need, uart_cfg, instance, variant):
    try:
        stall(fw, ad3, need, uart_cfg, instance, variant)
        fw.uart.close(instance["index"])
    finally:
        ad3.dio.drive(need.dio(instance["cts"]), 0)


@then(parsers.parse("the stalled payload reaches the AD3 within {seconds:g} s"))
def stalled_payload_leaves(ad3, stalled_payload, seconds):
    assert ad3.uart.read(len(stalled_payload), timeout=seconds) == stalled_payload, "the stalled data must leave once CTS is asserted"


@then(parsers.parse('"{data}" goes from the firmware to the AD3 at the flow baud rate'))
def sends_at_flow_baud(fw, ad3, uart_cfg, instance, data):
    firmware_to_ad3(fw, ad3, instance["index"], bytes.fromhex(data), uart_cfg["flow_baud"])


@then("the board answers ping")
def answers_ping(fw):
    fw.system.ping()


@then("the last of the payloads goes from the firmware to the AD3 and then from the AD3 to the firmware at the flow baud rate")
def last_payload_at_flow_baud(fw, ad3, uart_cfg, instance):
    payload = bytes.fromhex(uart_cfg["payloads"][-1])
    firmware_to_ad3(fw, ad3, instance["index"], payload, uart_cfg["flow_baud"])
    ad3_to_firmware(fw, ad3, instance["index"], payload, uart_cfg["flow_baud"])


@then(
    parsers.parse("the last of the payloads goes from the firmware to the AD3 and then from the AD3 to the firmware at {baud_rate:d} baud")
)
def last_payload_both_ways(fw, ad3, uart_cfg, instance, baud_rate):
    payload = bytes.fromhex(uart_cfg["payloads"][-1])
    firmware_to_ad3(fw, ad3, instance["index"], payload, baud_rate)
    ad3_to_firmware(fw, ad3, instance["index"], payload, baud_rate)


@then(parsers.parse("the last of the payloads goes from the firmware to the AD3 at {baud_rate:d} baud"))
def last_payload_to_ad3(fw, ad3, uart_cfg, instance, baud_rate):
    payload = bytes.fromhex(uart_cfg["payloads"][-1])
    firmware_to_ad3(fw, ad3, instance["index"], payload, baud_rate)


@then(
    parsers.parse(
        'with each of the reopen settings in turn, the instance is opened against the AD3 at its baud rate and parity, "{data}" goes from '
        "the firmware to the AD3, the instance is closed and {pause_ms:d} ms pass"
    )
)
def reopens_with_settings(fw, ad3, need, uart_cfg, instance, data, pause_ms):
    for baud_rate, parity_setting in uart_cfg["reopen_settings"]:
        open_pair(fw, ad3, need, uart_cfg, instance, baud_rate, parity_setting)
        firmware_to_ad3(fw, ad3, instance["index"], bytes.fromhex(data), baud_rate, parity_setting)
        fw.uart.close(instance["index"])
        time.sleep(pause_ms / 1000)


@then(
    parsers.parse(
        "opening the instance on its TX and RX pins at the baud rate succeeds and the instance closes if the baud rate fits, "
        'and fails with "{reason}" otherwise'
    )
)
def open_baud(fw, instance, baud, fits, reason):
    lp = bool(instance["lp"])
    try:
        fw.uart.open(instance["index"], lp=lp, tx=instance["tx"], rx=instance["rx"], baud=baud)
    except FirmwareError as error:
        assert error.reason == reason and not fits, f"ERR {error.reason} although {baud} Bd fits"
        return
    assert fits, f"{baud} Bd accepted outside the limits"
    fw.uart.close(instance["index"])


@then("opening the instance with each of the invalid option sets fails with the reason of the set")
def open_options_refused(fw, instance):
    for options, reason in open_errors(instance):
        with pytest.raises(FirmwareError) as error:
            fw.command("uart.open", instance["index"], lp=instance["lp"], **{key: fw_pin(fw, key, value) for key, value in options.items()})
        assert error.value.reason == reason, options


@then(parsers.parse('opening the instance with stop={stop_bits:d} fails with "{reason}"'))
def stop_key_refused(fw, instance, stop_bits, reason):
    with pytest.raises(FirmwareError) as error:
        fw.command("uart.open", instance["index"], lp=instance["lp"], stop=stop_bits)
    assert error.value.reason == reason, "no stop key"


@then("the instance opens without pins and closes, if it has default pins")
def opens_on_default_pins(fw, instance):
    if instance.get("default_pins"):
        fw.uart.open(instance["index"], lp=instance["lp"])
        fw.uart.close(instance["index"])


@then(parsers.parse('opening the instance without pins fails with "{reason}", if it has no default pins'))
def needs_pins(fw, instance, reason):
    if instance.get("default_pins"):
        return
    with pytest.raises(FirmwareError) as error:
        fw.uart.open(instance["index"], lp=instance["lp"])
    assert error.value.reason == reason


@then(parsers.parse('opening the second on its TX and RX pins fails with "{reason}"'))
def second_refused(fw, two_instances, reason):
    second = two_instances[1]
    with pytest.raises(FirmwareError) as error:
        fw.uart.open(second["index"], lp=second["lp"], tx=second["tx"], rx=second["rx"])
    assert error.value.reason == reason


@then(parsers.parse('sending {data} on the instance fails with "{reason}"'))
def send_refused(fw, instance, data, reason):
    with pytest.raises(FirmwareError) as error:
        fw.uart.send(instance["index"], bytes.fromhex(data))
    assert error.value.reason == reason


@then("each malformed or out-of-range uart.send and uart.recv command line of the instance fails with its reason")
def limits_refused(fw, uart_cfg, instance):
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
