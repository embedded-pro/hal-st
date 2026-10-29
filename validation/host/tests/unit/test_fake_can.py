"""The `groups.can` model (the nominal bit timing of `FdCanStm::CalculateNominalBitTiming`, the bit rates
validation/firmware/CanFactory.cpp accepts, the bit rate of an explicit timing and the acceptance filter), the board
file's `tests.can` against it, the fake `can` group (argument order and reasons, one controller at a time on its pins,
loopback and virtual-bus frames) and the wrapper."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareTerminal

from hal_st_validation.can_peer import SLCAN_BITRATES
from hal_st_validation.config import load_board
from hal_st_validation.fake_firmware import G474_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware
from hal_st_validation.groups.can import (
    TIMING_MAX,
    BitTiming,
    CanFrame,
    bitrate_achievable,
    filter_accepts,
    nominal_bit_timing,
    timing_bitrate,
)

CLOCK = 24_000_000


def make(family="stm32g474", can_bus=None):
    fake = FakeFirmware(family=family, can_bus=can_bus)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    return terminal, fake, Firmware(terminal, G474_PINS)


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


@pytest.mark.parametrize(
    ("bitrate", "expected"),
    [
        (1_000_000, BitTiming(1, 20, 3, 3)),
        (500_000, BitTiming(2, 20, 3, 3)),
        (125_000, BitTiming(8, 20, 3, 3)),
        (750_000, BitTiming(2, 13, 2, 2)),
        (800_000, BitTiming(2, 13, 1, 1)),
        (1_875, BitTiming(512, 21, 3, 3)),
    ],
)
def test_nominal_bit_timing_takes_the_most_quanta_that_hit_the_rate(bitrate, expected):
    timing = nominal_bit_timing(CLOCK, bitrate)
    assert timing == expected
    assert CLOCK / (timing.prescaler * timing.quanta) == bitrate


def test_achievable_bit_rates():
    assert all(bitrate_achievable(CLOCK, rate) for rate in (1_000_000, 800_000, 750_000, 500_000, 250_000, 100_000, 1_875))
    # 24 MHz / 123457 is no whole number; 1000 bit/s needs a prescaler above 512; 1536 bit/s needs 15625 quanta.
    assert not any(bitrate_achievable(CLOCK, rate) for rate in (123_457, 83_333, 1_000, 1_536))
    with pytest.raises(ValueError):
        nominal_bit_timing(CLOCK, 0)


def test_timing_bitrate():
    assert timing_bitrate(CLOCK, 13, 2, 1, 3) == 500_000
    assert timing_bitrate(CLOCK, 40, 7, 4, 1) == 500_000
    assert timing_bitrate(CLOCK, 19, 4, 4, 1) == 1_000_000


def test_filter_matches_one_id_type():
    assert filter_accepts(None, 0x1FFFFFFF, True)
    standard = (0x120, 0x7F0, False, True)
    assert filter_accepts(standard, 0x12F, False) and not filter_accepts(standard, 0x130, False)
    assert not filter_accepts(standard, 0x120, True), "match: the other id type is rejected"
    assert filter_accepts((0x120, 0x7F0, False, False), 0x1ABCDEF0, True), "match 0: the other id type passes unfiltered"
    extended = (0x1ABC0000, 0x1FFF0000, True, True)
    assert filter_accepts(extended, 0x1ABCFFFF, True) and not filter_accepts(extended, 0x0ABC0000, True)
    assert not filter_accepts(extended, 0x6AF, False)
    assert filter_accepts((0x1ABC0000, 0x1FFF0000, True, False), 0x6AF, False)


def test_board_file_parameters_follow_the_model():
    """Bit rates the kernel clock reaches, timings within the field limits at CANable rates, filter cases the model
    splits as listed and instance pins with their function."""
    board = load_board("nucleo_g474re")
    clock = board.clock("fdcan")
    can = board.param("can")
    fake = FakeFirmware(family=board.family)
    assert clock == fake.spec.fdcan_clock == CLOCK
    for rate in can["frames"]["bitrate"] + [can["bus"]["bitrate"], can["bus"]["wrong_bitrate"]]:
        assert bitrate_achievable(clock, rate) and rate in SLCAN_BITRATES, rate
    assert can["bus"]["wrong_bitrate"] != can["bus"]["bitrate"]
    for timing in can["timings"]:
        assert all(1 <= value <= limit for value, limit in zip(timing, TIMING_MAX)) and timing[2] <= timing[1], timing
        assert timing_bitrate(clock, *timing) in SLCAN_BITRATES, timing
    for case in can["filters"]:
        ident, mask, ext, match = case["filter"]
        can_filter = (ident, mask, bool(ext), bool(match))
        assert all(filter_accepts(can_filter, frame_id, bool(frame_ext)) for frame_id, frame_ext in case["accepted"]), case["name"]
        assert not any(filter_accepts(can_filter, frame_id, bool(frame_ext)) for frame_id, frame_ext in case["rejected"]), case["name"]
    assert [instance["index"] for instance in can["instances"]] == [1, 2, 3]
    for instance in can["instances"] + [can]:
        assert fake.supports("canRx", instance["index"], board.resolve_pin(instance["rx"])), instance
        assert fake.supports("canTx", instance["index"], board.resolve_pin(instance["tx"])), instance
    assert (can["index"], board.resolve_pin(can["rx"]), board.resolve_pin(can["tx"])) == fake.spec.default_can


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("can.open", "usage"),
        ("can.open 1 2", "usage"),
        ("can.open 1 bogus=1", "usage"),
        ("can.open 0", "range"),
        ("can.open 4", "range"),
        ("can.open 0 rx=nosuch", "range"),
        ("can.open 1 rx=nosuch bitrate=0", "pin"),
        ("can.open 1 bitrate=0 filter=x", "range"),
        ("can.open 1 bitrate=x", "usage"),
        ("can.open 1 bitrate=1000001", "range"),
        ("can.open 1 bitrate=123457", "range"),
        ("can.open 1 loopback=2", "range"),
        ("can.open 1 recover=on", "usage"),
        ("can.open 1 filter=0x800,0x7ff,0 timing=13,2,1", "range"),
        ("can.open 1 filter=0x800,0x7ff,2", "usage"),
        ("can.open 1 filter=0x120,0x7f0", "usage"),
        ("can.open 1 filter=0x20000000,0,1", "range"),
        ("can.open 1 bitrate=500000 timing=13,2,1,3", "usage"),
        ("can.open 1 timing=13,2,1", "usage"),
        ("can.open 1 timing=13,x,1,3", "usage"),
        ("can.open 1 timing=0,2,1,3", "range"),
        ("can.open 1 timing=13,2,3,3", "range"),
        ("can.open 1 timing=13,2,1,513", "range"),
        ("can.open 1 timing=256,128,128,512 rx=PA11", "usage"),
        ("can.open 2", "usage"),
        ("can.open 1 rx=PA11", "usage"),
        ("can.open 1 rx=PB12 tx=PA12", "pin"),
        ("can.open 1 rx=PD0 tx=PD1", "pin"),
        ("can.open 3 rx=PB3 tx=PB4", "ok"),
        ("can.open 1 timing=256,128,128,512", "ok"),
        ("can.send 1 0x123 01", "notopen"),
        ("can.send 4 0x123 01", "range"),
        ("can.close 1", "notopen"),
    ],
)
def test_argument_order(line, expected):
    terminal, _, _ = make()
    assert reason(terminal, line) == expected


def test_one_controller_on_its_pins():
    terminal, fake, fw = make()
    fw.can.open(1, loopback=True)
    assert {pin for pin, (owner, _) in fake.claims.items() if owner == ("can", "1")} == {"PA11", "PA12"}
    assert reason(terminal, "can.open 2 rx=PB12 tx=PB13") == "busy"
    assert reason(terminal, "can.open 1") == "busy"
    assert reason(terminal, "gpio.cfg PA11 in") == "busy"
    assert reason(terminal, "can.send 2 0x1 01") == "notopen"
    for line, expected in (
        ("can.send 1 0x800 00", "range"),
        ("can.send 1 0x20000000 00 ext=1", "range"),
        ("can.send 1 0x100 00 ext=2", "range"),
        ("can.send 1 0x100 000102030405060708", "range"),
        ("can.send 1 0x100 0", "usage"),
        ("can.send 1 0x100", "usage"),
        ("can.send 1 0x100 00 rtr=1", "usage"),
    ):
        assert reason(terminal, line) == expected, line
    fw.can.close(1)
    assert fw.open_instances == []
    assert reason(terminal, "gpio.cfg PA11 in") == "ok"
    assert reason(terminal, "can.open 1") == "busy", "PA11 is a GPIO now"


def test_loopback_receives_its_own_frames_through_the_filter():
    _, fake, fw = make()
    fw.can.open(1, loopback=True, filter=(0x120, 0x7F0, False))
    fw.can.send(1, 0x12F, b"")
    fw.can.send(1, 0x130, b"\x01")
    fw.can.send(1, 0x120, b"\x02", ext=True)
    fw.can.send(1, 0x125, bytes(range(8)))
    events = fw.terminal.drain_events("can")
    assert [event.raw for event in events] == ["EVT can index=1 id=303 ext=0 data=", "EVT can index=1 id=293 ext=0 data=0001020304050607"]
    assert [fw.can.frame(event) for event in events] == [CanFrame(1, 0x12F, False, b""), CanFrame(1, 0x125, False, bytes(range(8)))]
    fw.can.close(1)
    fw.can.open(1)
    fw.can.send(1, 0x7FF, b"\xaa")
    assert fw.can.frames(1) == [], "normal mode without a bus receives nothing"


def test_virtual_bus_round_trip():
    can = pytest.importorskip("can")
    terminal, fake, fw = make(can_bus="unit-fake-can")
    other = can.Bus(interface="virtual", channel="unit-fake-can")
    try:
        fw.can.open(1, filter=(0x1ABC0000, 0x1FFF0000, True))
        fw.can.send(1, 0x1ABC0001, b"\x01", ext=True)
        message = other.recv(0.5)
        assert (message.arbitration_id, message.is_extended_id, bytes(message.data)) == (0x1ABC0001, True, b"\x01")
        assert fw.can.frames(1) == [], "normal mode does not receive its own frames"
        other.send(can.Message(arbitration_id=0x6AF, is_extended_id=False, data=b"\x02"))
        other.send(can.Message(arbitration_id=0x1ABCFFFF, is_extended_id=True, data=b"\x03"))
        assert fw.can.wait_frame(1, timeout=0.5) == CanFrame(1, 0x1ABCFFFF, True, b"\x03")
        assert fw.can.frames(1) == []
        fw.can.close(1)
        fw.can.open(1, loopback=True)
        other.send(can.Message(arbitration_id=0x10, is_extended_id=False, data=b"\x04"))
        fw.can.send(1, 0x11, b"\x05")
        assert fw.can.frames(1) == [CanFrame(1, 0x11, False, b"\x05")], "loopback ignores the RX pin"
        message = other.recv(0.5)
        assert (message.arbitration_id, bytes(message.data)) == (0x11, b"\x05"), "loopback still drives the TX pin"
        fw.system.reset()
        assert fake.group("can").buses == {}
        assert reason(terminal, "can.send 1 0x1 01") == "notopen"
    finally:
        other.shutdown()


def test_errors_and_frames_of_the_wrapper():
    _, fake, fw = make()
    fw.can.open(1, rx="can1rx", tx="can1tx", timing=(13, 2, 1, 3), filter=(0x120, 0x7F0, False, False), loopback=True, recover=False)
    assert fake.received[-1] == "can.open 1 rx=PA11 tx=PA12 timing=13,2,1,3 filter=0x120,0x7f0,0,0 loopback=1 recover=0"
    assert fw.open_instances == [("can", 1)]
    fake.event("EVT can index=1 error=ackError")
    fake.event("EVT can index=1 id=291 ext=0 data=0102")
    fake.event("EVT can index=2 error=busOff")
    assert fw.can.errors(1) == ["ackError"]
    fake.event("EVT can index=1 id=291 ext=1 data=")
    assert fw.can.wait_frame(1, lambda frame: frame.ext, timeout=0.5) == CanFrame(1, 0x123, True, b"")
    pending = fw.can.begin_send(1, 0x7FF, b"\x01", cmd_timeout=1.0)
    assert pending.wait().ok
    assert fake.received[-1] == "can.send 1 0x7ff 01"
    fw.can.send(1, 0x1ABCDEF0, b"", ext=True)
    assert fake.received[-1] == "can.send 1 0x1abcdef0 - ext=1"
    fw.can.close(1)
    assert fw.open_instances == []


@pytest.mark.parametrize("family", ["stm32wb55", "stm32wba55"])
def test_unsupported_off_the_g474(family):
    terminal, _, _ = make(family)
    for line in ("can.open 1", "can.send 1 0x123 01", "can.close 1"):
        assert reason(terminal, line) == "unsupported"
