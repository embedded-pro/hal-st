"""CAN (`hal::FdCanStm`, NUCLEO-G474RE) over the link `--can-mode` selects:

- `loopback`: the FDCAN external loopback mode (`loopback=1`); needs no wiring;
- `bus`: FDCAN1 through a 3.3 V transceiver to a CANable (`--can-peer`); every frame goes from the firmware to the
  CANable and back.

The `test_bus_*` tests need the CANable whatever `--can-mode` is: a missing acknowledge (CANable listen-only),
receive errors and bus off (CANable at a wrong bit rate), the bus-off recovery and the TX pin in loopback mode.

Scenarios: features/can.feature.
"""

from __future__ import annotations

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.can_peer import CanPeer
from hal_st_validation.firmware import settle
from hal_st_validation.groups.can import ERROR_NAMES, timing_bitrate

SEND_TIMEOUT = 3.0


@pytest.fixture
def can(board_cfg):
    return board_cfg.param("can")


@pytest.fixture
def controller(can):
    return {"index": can["index"], "rx": can["rx"], "tx": can["tx"]}


@pytest.fixture
def peer(request, link) -> CanPeer | None:
    return request.getfixturevalue("can_peer") if link == "bus" else None


@pytest.fixture
def no_fake(pytestconfig):
    if pytestconfig.getoption("--fake"):
        pytest.skip("the fake bus does not model bit errors")


def make_frame(can, ext, dlc):
    ids = can["ids"]["ext" if ext else "std"]
    return {"id": ids[dlc % len(ids)], "ext": bool(ext), "data": bytes((dlc * 16 + i) & 0xFF for i in range(dlc))}


def matches(frame):
    return lambda received: received.id == frame["id"] and received.ext == frame["ext"]


def open_can(fw, controller, link=None, **options):
    """The controller on its pins (an `rx`/`tx` option replaces one); in loopback mode on the loopback link."""
    if link == "loopback":
        options["loopback"] = True
    pins = {"rx": controller["rx"], "tx": controller["tx"]}
    pins.update((key, options.pop(key)) for key in ("rx", "tx") if key in options)
    fw.can.open(controller["index"], **pins, **options)


def attach(peer, bitrate, listen_only=False):
    bitrate = int(round(bitrate))
    if not peer.supports(bitrate, listen_only):
        pytest.skip(f"{peer} cannot run at {bitrate} bit/s{' listen-only' if listen_only else ''}")
    peer.configure(bitrate, listen_only)


def comes_back(fw, controller, frame):
    received = fw.can.wait_frame(controller["index"], matches(frame), timeout=1.0)
    assert received.data == frame["data"]


def transfer(fw, controller, peer, frame):
    """Loopback: the controller receives its own frame. Bus: the CANable receives it and sends it back."""
    fw.can.send(controller["index"], frame["id"], frame["data"], ext=frame["ext"])
    if peer is not None:
        received = peer.receive(timeout=1.0, predicate=matches(frame))
        assert received is not None, f"{peer} did not receive the frame"
        assert received.data == frame["data"]
        peer.send(frame["id"], frame["data"], ext=frame["ext"])
    comes_back(fw, controller, frame)


def send_on_link(fw, controller, peer, frame_id, frame_ext, data):
    if peer is None:
        fw.can.send(controller["index"], frame_id, data, ext=bool(frame_ext))
    else:
        peer.send(frame_id, data, ext=bool(frame_ext))


def begin_send(fw, controller, frame):
    return fw.can.begin_send(controller["index"], frame["id"], frame["data"], ext=frame["ext"], cmd_timeout=SEND_TIMEOUT)


def refused(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


@pytest.mark.matrix("can.frames")
@scenario("can.feature", "A frame of the id type and data length crosses the link at the bit rate")
def test_frames(link, bitrate, ext, dlc):
    pass


@pytest.mark.board_params("timing", "can.timings")
@scenario("can.feature", "Frames cross the link at the bit rate of an explicit timing")
def test_explicit_timing(link, timing):
    pass


@pytest.mark.board_params("case", "can.filters")
@scenario("can.feature", "The acceptance filter passes the frames it matches and drops the others")
def test_acceptance_filter(link, case):
    pass


@pytest.mark.board_params("instance", "can.instances")
@scenario("can.feature", "Every FDCAN instance carries frames on its pins")
def test_instances(instance):
    pass


@scenario("can.feature", "A frame cannot be sent before the controller is open")
def test_send_requires_open():
    pass


@pytest.mark.board_params(
    "options,reason",
    values=[
        [{"bitrate": 123457}, "range"],
        [{"bitrate": 1000}, "range"],
        [{"bitrate": 0}, "range"],
        [{"bitrate": 1000001}, "range"],
        [{"bitrate": 500000, "timing": (13, 2, 1, 3)}, "usage"],
        [{"timing": (0, 2, 1, 3)}, "range"],
        [{"timing": (257, 2, 1, 3)}, "range"],
        [{"timing": (13, 129, 1, 3)}, "range"],
        [{"timing": (13, 2, 3, 3)}, "range"],
        [{"timing": (13, 2, 1, 513)}, "range"],
        [{"filter": (0x800, 0x7FF, False)}, "range"],
        [{"filter": (0x20000000, 0, True)}, "range"],
        [{"loopback": 2}, "range"],
        [{"rx": "PB12"}, "pin"],
        [{"tx": "PA15"}, "pin"],
        [{"rx": "PD0"}, "pin"],
    ],
)
@scenario("can.feature", "Invalid options are refused with their reason")
def test_open_errors(options, reason):
    pass


@pytest.mark.board_params(
    "option,text",
    values=[
        ["timing", "13,2,1"],
        ["timing", "13,2,1,3,4"],
        ["timing", "13,x,1,3"],
        ["filter", "0x120,0x7f0"],
        ["filter", "0x120,0x7f0,0,1,1"],
        ["filter", "0x120,0x7f0,2"],
        ["filter", "0x120,0x7f0,0,2"],
        ["recover", "on"],
    ],
)
@scenario("can.feature", "A malformed timing or filter is refused with usage")
def test_malformed_options(option, text):
    pass


@pytest.mark.board_params(
    "arguments,reason",
    values=[
        ["0x800 00", "range"],
        ["0x20000000 00 ext=1", "range"],
        ["0x100 00 ext=2", "range"],
        ["0x100 000102030405060708", "range"],
        ["0x100 0", "usage"],
        ["0x100 zz", "usage"],
        ["0x100", "usage"],
        ["0x100 00 rtr=1", "usage"],
    ],
)
@scenario("can.feature", "A frame the controller cannot send is refused with its reason")
def test_send_errors(arguments, reason):
    pass


@scenario("can.feature", "FDCAN1 opens on its default pins, the other instances need theirs")
def test_default_pins():
    pass


@scenario("can.feature", "One controller is open at a time")
def test_one_controller():
    pass


@scenario("can.feature", "A frame nobody acknowledges fails with ackError")
def test_bus_no_acknowledge(can_peer):
    pass


@scenario("can.feature", "Frames at a wrong bit rate are reported as receive errors")
def test_bus_receive_errors(can_peer):
    pass


@pytest.mark.board_params("recover", "can.recover")
@scenario("can.feature", "Bus off fails the pending frame and ends by itself only with the automatic recovery")
def test_bus_off_and_recovery(can_peer, recover):
    pass


@scenario("can.feature", "A frame sent in loopback mode still leaves on the TX pin")
def test_bus_loopback_drives_tx(can_peer):
    pass


@given("the CANable runs at the bit rate if the link is the bus")
def canable_at_bitrate(peer, bitrate):
    if peer is not None:
        attach(peer, bitrate)


@given("the controller is open on the link at the bit rate")
def open_at_bitrate(fw, controller, link, bitrate):
    open_can(fw, controller, link, bitrate=bitrate)


@given("the CANable runs at the bit rate of the timing if the link is the bus")
def canable_at_timing_bitrate(board_cfg, peer, timing):
    if peer is not None:
        attach(peer, timing_bitrate(board_cfg.clock("fdcan"), *timing))


@given("the controller is open on the link with the timing")
def open_with_timing(fw, controller, link, timing):
    open_can(fw, controller, link, timing=tuple(timing))


@given("the CANable runs at the bus bit rate if the link is the bus")
def canable_at_bus_bitrate_on_bus(can, peer):
    if peer is not None:
        attach(peer, can["bus"]["bitrate"])


@given("the controller is open on the link at the bus bit rate with the filter of the case")
def open_with_filter(fw, can, controller, link, case):
    ident, mask, ext, match = case["filter"]
    open_can(fw, controller, link, bitrate=can["bus"]["bitrate"], filter=(ident, mask, bool(ext), bool(match)))


@given("the controller is the instance", target_fixture="controller")
def controller_is_instance(instance):
    return instance


@given("the controller is open in loopback mode at the bus bit rate")
def open_in_loopback(fw, can, controller):
    open_can(fw, controller, "loopback", bitrate=can["bus"]["bitrate"])


@given("the bus is not the fake bus")
def real_bus(no_fake):
    pass


@given("the CANable's bit rate can be set")
def canable_adjustable(can_peer):
    if not can_peer.adjustable:
        pytest.skip(f"{can_peer} has a fixed bit rate")


@given("the CANable runs listen-only at the bus bit rate")
def canable_listen_only(can, can_peer):
    attach(can_peer, can["bus"]["bitrate"], listen_only=True)


@given("the CANable runs at the bus bit rate")
def canable_at_bus_bitrate(can, can_peer):
    attach(can_peer, can["bus"]["bitrate"])


@given("the CANable runs at the wrong bit rate")
def canable_at_wrong_bitrate(can, can_peer):
    attach(can_peer, can["bus"]["wrong_bitrate"])


@given("the controller is open at the bus bit rate")
def open_at_bus_bitrate(fw, can, controller):
    open_can(fw, controller, bitrate=can["bus"]["bitrate"])


@given("the controller is open at the bus bit rate with the recovery setting")
def open_with_recovery(fw, can, controller, recover):
    open_can(fw, controller, bitrate=can["bus"]["bitrate"], recover=recover)


@when(parsers.parse('each rejected frame of the case is sent on the link with data "{data}"'))
def send_rejected(fw, controller, peer, case, data):
    for frame_id, frame_ext in case["rejected"]:
        send_on_link(fw, controller, peer, frame_id, frame_ext, bytes.fromhex(data))


@when(
    parsers.parse(
        "the firmware sends the standard frame of {length:d} bytes in the background while the CANable waits up to {seconds:g} s for it"
    ),
    target_fixture="sent",
)
def send_watched(fw, can, controller, can_peer, length, seconds):
    frame = make_frame(can, 0, length)
    pending = begin_send(fw, controller, frame)
    try:
        received = can_peer.receive(timeout=seconds, predicate=matches(frame))
    finally:
        response = settle(pending)
    return {"frame": frame, "received": received, "response": response}


@when(parsers.parse("the CANable sends a standard frame with id 0x{ident:x} and {length:d} zero bytes"))
def canable_sends_zeros(can_peer, ident, length):
    can_peer.send(ident, bytes(length))


@when(
    parsers.parse(
        "the firmware sends a standard frame with id 0x{ident:x} and {length:d} zero bytes in the background while the controller "
        "reports busOff within {seconds:g} s"
    ),
    target_fixture="failed_send",
)
def send_until_bus_off(fw, controller, ident, length, seconds):
    frame = {"id": ident, "ext": False, "data": bytes(length)}
    pending = begin_send(fw, controller, frame)
    try:
        fw.terminal.wait_event("can", lambda event: event.get("error") == "busOff", timeout=seconds)
    finally:
        response = settle(pending)
    return {"frame": frame, "response": response}


@when("the CANable switches to the bus bit rate")
def canable_back_at_bus_bitrate(can, can_peer):
    attach(can_peer, can["bus"]["bitrate"])


@then("the frame of the id type and data length crosses the link")
def frame_crosses(fw, can, controller, peer, ext, dlc):
    transfer(fw, controller, peer, make_frame(can, ext, dlc))


@then(parsers.parse("the standard and then the extended frame of {length:d} bytes cross the link"))
def both_id_types_cross(fw, can, controller, peer, length):
    for ext in (0, 1):
        transfer(fw, controller, peer, make_frame(can, ext, length))


@then(parsers.parse("the standard and then the extended frame of {length:d} bytes come back to the controller"))
def both_id_types_come_back(fw, can, controller, length):
    for ext in (0, 1):
        transfer(fw, controller, None, make_frame(can, ext, length))


@then(
    parsers.parse(
        'each accepted frame of the case, sent on the link with data "{data}", arrives at the firmware within {seconds:g} s with its id '
        "type and that data"
    )
)
def accepted_frames_arrive(fw, controller, peer, case, data, seconds):
    payload = bytes.fromhex(data)
    for frame_id, frame_ext in case["accepted"]:
        send_on_link(fw, controller, peer, frame_id, frame_ext, payload)
        received = fw.can.wait_frame(controller["index"], lambda f, i=frame_id: f.id == i, timeout=seconds)
        assert (received.ext, received.data) == (bool(frame_ext), payload)


@then(parsers.parse("after {ms:d} ms no other frame has arrived at the firmware"))
def no_frame_leaked(fw, controller, ms):
    fw.system.delay(ms)
    leaked = fw.can.frames(controller["index"])
    assert not leaked, f"frames outside the filter were received: {leaked}"


@then(parsers.parse('sending a frame with id 0x{ident:x} and data "{data}" on the controller fails with "{reason}"'))
def send_unopened_refused(fw, controller, ident, data, reason):
    refused(reason, fw.can.send, controller["index"], ident, bytes.fromhex(data))


@then("opening the controller on its pins with the options fails with the reason")
def open_options_refused(fw, controller, options, reason):
    refused(reason, open_can, fw, controller, **options)


@then(parsers.parse('opening the controller on its pins with the raw option fails with "{reason}"'))
def raw_option_refused(fw, controller, option, text, reason):
    rx, tx = fw.pin(controller["rx"]), fw.pin(controller["tx"])
    refused(reason, fw.command, "can.open", controller["index"], rx=rx, tx=tx, **{option: text})


@then("sending the arguments on the controller fails with the reason")
def send_arguments_refused(fw, controller, arguments, reason):
    refused(reason, fw.terminal.command, f"can.send {controller['index']} {arguments}")


@then(parsers.parse("FDCAN {number:d} opens in loopback mode without pins"))
def opens_on_default_pins(fw, number):
    fw.can.open(number, loopback=True)


@then(parsers.parse('configuring can1rx or can1tx as a GPIO input fails with "{reason}"'))
def default_pins_held(fw, reason):
    for alias in ("can1rx", "can1tx"):
        refused(reason, fw.gpio.cfg, alias, "in")


@then(parsers.parse("FDCAN {number:d} closes again"))
def closes(fw, number):
    fw.can.close(number)


@then(parsers.parse('opening FDCAN {number:d} on the RX pin of the controller only fails with "{reason}"'))
def single_pin_refused(fw, controller, number, reason):
    refused(reason, fw.can.open, number, rx=controller["rx"])


@then(parsers.parse('opening FDCAN {number:d} without pins fails with "{reason}"'))
def no_default_pins(fw, number, reason):
    refused(reason, fw.can.open, number, loopback=True)


@then(parsers.parse('opening the second instance of tests.can.instances in loopback mode on its pins fails with "{reason}"'))
def other_instance_refused(fw, can, reason):
    other = can["instances"][1]
    refused(reason, fw.can.open, other["index"], rx=other["rx"], tx=other["tx"], loopback=True)


@then(parsers.parse('opening the controller again fails with "{reason}"'))
def second_open_refused(fw, can, controller, reason):
    refused(reason, open_can, fw, controller, "loopback", bitrate=can["bus"]["bitrate"])


@then("the CANable has received the frame with its data")
def canable_saw_frame(sent):
    frame, received = sent["frame"], sent["received"]
    assert received is not None and received.data == frame["data"], "the CANable did not see the frame"


@then(parsers.parse('can.send has answered "{first}" or "{second}"'))
def send_failed(sent, first, second):
    response = sent["response"]
    assert response is not None and response.reason in (first, second), f"unacknowledged send answered {response}"


@then("can.send has answered OK")
def send_succeeded(sent):
    response = sent["response"]
    assert response is not None and response.ok, f"can.send answered {response}"


@then("the frame has come back to the controller")
def frame_came_back(fw, controller, sent):
    comes_back(fw, controller, sent["frame"])


@then(parsers.parse('the controller has reported "{error_name}"'))
def error_reported(fw, controller, error_name):
    errors = fw.can.errors(controller["index"])
    assert error_name in errors, f"no {error_name} reported: {errors}"


@then(parsers.parse("after {ms:d} ms the controller has reported at least one error, all of them known CAN error names"))
def receive_errors_reported(fw, controller, ms):
    fw.system.delay(ms)
    errors = fw.can.errors(controller["index"])
    assert errors, "no error reported for frames at a wrong bit rate"
    assert set(errors) <= set(ERROR_NAMES)


@then(parsers.parse('can.send of that frame has answered "{reason}"'))
def bus_off_send_failed(failed_send, reason):
    response = failed_send["response"]
    assert response is not None and response.reason == reason, f"the send in flight at bus off answered {response}"


@then(parsers.parse("after {ms:d} ms the CANable has received no frame with the id and id type of the failed frame"))
def failed_frame_not_resent(can_peer, failed_send, ms):
    received = can_peer.receive(timeout=ms / 1000, predicate=matches(failed_send["frame"]))
    assert received is None, "the frame that failed at bus off was sent after the recovery"


@then("the CANable has received the frame and can.send has answered OK if the recovery setting is on, and neither if it is off")
def recovery_as_set(sent, recover):
    received, response = sent["received"], sent["response"]
    if recover:
        assert received is not None and received.data == sent["frame"]["data"], "no frame after the automatic bus-off recovery"
        assert response is not None and response.ok, f"can.send after the recovery answered {response}"
    else:
        assert received is None, "the controller left bus off although recover=0"
        assert response is not None and not response.ok, f"can.send in bus off answered {response}"
