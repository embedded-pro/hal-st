"""I2C master (`hal::I2cStm`) through the `i2c` group, against the `i2cs` LL target scaffold on the other instance.

Standalone cases (`tests.i2c.instances`, bundle1, no option): each instance on its own pins with the MCU pull-ups
(`pull=up`), enough for Standard mode on a short bus: the default and computed TIMINGR, address NACK and its
recovery, the zero-length probe, arbitration loss (the AD3 holds SDA low before `i2c.open`, or pulls it low inside
the second address bit after a START) and the argument errors. Nobody answers on these buses, so every transfer
ends in an address NACK.

`--with i2c` cases (`tests.i2c.loop`): the master and the target on the jumpered rows with 4.7 kOhm pull-ups and the
24LC256 at 0x50: Fast mode, rise time (where a scope sits on the rows), data NACK at every position, the address
NACK after the other direction (B.1h), RELOAD lengths, repeated START, continued sessions in both directions (B.1d),
clock stretching, a bus error from the target's misplaced STOP (B.1e), the general call against an idle I2cStm
(B.1i), close while the bus is held, and both instance directions.

The module is marked `uses_option("i2c")`: the standalone cases also run with the option fitted (they then share the
rows with the pull-ups and the EEPROM, which answers neither 0x10 nor 0x7f). Checks of the physical bus (levels,
timing, rise time) skip under `--fake`; the fake answers the protocol.

Scenarios: features/i2c.feature.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.groups.i2c import DEFAULT_TIMING, I2C_HEX_MAX
from hal_st_validation.i2c import (
    arbitration_window,
    arm_on_start,
    arm_scope,
    expected_timing,
    i2c_decode,
    inject_on_start,
    mode_limits,
    rise_times,
    scl_timing,
)
from hal_st_validation.patterns import crc_text, generate

# Scope levels of the 30-70 % rise time on a 3.3 V bus.
VDD = 3.3
WRITE_READ_LENGTHS = [1, 2, 64, 128]
# (direction, byte position) of the target's clock stretch: the address phase, a written byte, a read byte.
STRETCH_POSITIONS = [("write", 0), ("write", 2), ("read", 0), ("read", 1)]


@pytest.fixture
def i2c_cfg(board_cfg) -> dict[str, Any]:
    return board_cfg.param("i2c")


@pytest.fixture
def bench(request) -> None:
    """The case measures the physical bus, which the fake does not model."""
    if request.config.getoption("--fake"):
        pytest.skip("measures the physical bus: nothing to measure with --fake")


@pytest.fixture
def loop(need, i2c_cfg) -> dict[str, dict[str, Any]]:
    """`tests.i2c.loop`, after the gate check of its four pins."""
    loop = i2c_cfg["loop"]
    need.unloaded(loop["master"]["scl"], loop["master"]["sda"], loop["target"]["scl"], loop["target"]["sda"])
    return loop


def expect_error(reason, call, *args, **options):
    with pytest.raises(FirmwareError) as error:
        call(*args, **options)
    assert error.value.reason == reason, (args, options)


def reason(fw, line):
    response = fw.terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


def open_standalone(fw, need, instance, **options):
    need.unloaded(instance["scl"], instance["sda"])
    return fw.i2c.open(instance["index"], instance["scl"], instance["sda"], pull="up", **options)


def bus_dios(need, instance) -> tuple[int, int | None]:
    """SCL's DIO (required) and SDA's when `observed` lists it."""
    scl = need.dio(instance["scl"])
    sda = need.dio(instance["sda"]) if "sda" in instance.get("observed", ["scl", "sda"]) else None
    return scl, sda


def arm_bus(ad3, scl: int, sda: int | None, freq: int, seconds: float):
    """The logic analyzer at 100 samples per SCL period (or what the buffer holds for `seconds`), started on a START
    when SDA is observed, else on the first SCL falling edge."""
    rate = min(ad3.logic.clock_hz, 100 * freq, ad3.logic.buffer_size / seconds)
    samples = int(rate * seconds)
    if sda is None:
        return ad3.logic.arm(rate, samples, trigger=(scl, "falling"), pretrigger=0.02)
    return arm_on_start(ad3, scl, sda, rate, samples, pretrigger=0.02)


def check_scl(capture, scl: int, freq: int, tolerance) -> None:
    """Frequency within `tolerance` of nominal, and the Standard/Fast mode tLOW/tHIGH minima (one sample of slack)."""
    timing = scl_timing(capture.channel(scl), capture.rate)
    limits = mode_limits(freq)
    slack = 1 / capture.rate
    assert tolerance[0] <= timing.freq / freq <= tolerance[1], f"{timing.freq:.0f} Hz for {freq} Hz"
    assert timing.tlow_min + slack >= limits.tlow_min, f"tLOW {timing.tlow_min * 1e9:.0f} ns"
    assert timing.thigh_min + slack >= limits.thigh_min, f"tHIGH {timing.thigh_min * 1e9:.0f} ns"


def open_loop(fw, loop, freq: int | None = None, mode: str = "regs") -> tuple[int, int, int]:
    """Master and target of `tests.i2c.loop`: returns (master index, target index, target address)."""
    master, target = loop["master"], loop["target"]
    fw.i2c.open(master["index"], master["scl"], master["sda"], freq=freq)
    address = fw.i2cs.open(target["index"], target["scl"], target["sda"], addr=target["addr"], mode=mode)
    return master["index"], target["index"], address


def _stretched_transfer(fw, loop, direction: str, position: int, us: int, arm: Callable[[], Any] = lambda: None) -> Any:
    """One stretched transfer, `arm()` (its result returned) called right before it. A read's pointer is written
    first, unstretched and with a STOP, so the read is a transfer of its own."""
    master, target, address = open_loop(fw, loop, freq=100_000)
    fw.i2cs.regs(target, 0, generate(8))
    if direction == "read":
        assert fw.i2c.write(master, address, b"\x00").result == "complete"
    fw.i2cs.cfg(target, stretch=us, stretchat=position)
    capture = arm()
    if direction == "write":
        assert fw.i2c.write(master, address, bytes([0x80]) + generate(4)).result == "complete"
    else:
        assert fw.i2c.read(master, address, 4).data == generate(4)
    return capture


def hex_bytes(text: str) -> bytes:
    return bytes(int(byte, 16) for byte in text.split())


# standalone, bundle1, pull=up


@pytest.mark.board_params("instance", "i2c.instances")
@scenario("i2c.feature", "Open reports the default, the computed and the given TIMINGR")
def test_open_replies_timing(instance):
    pass


@pytest.mark.usefixtures("bench")
@pytest.mark.board_params("instance", "i2c.instances")
@scenario("i2c.feature", "The default TIMINGR runs Standard mode on the bus")
def test_default_timing_in_spec(instance):
    pass


@pytest.mark.usefixtures("bench")
@pytest.mark.board_params("instance", "i2c.instances")
@pytest.mark.board_params("freq", "i2c.freqs.standalone")
@scenario("i2c.feature", "SCL runs at the requested frequency with the mode's tLOW and tHIGH")
def test_timing(instance, freq):
    pass


@pytest.mark.board_params("instance", "i2c.instances")
@pytest.mark.board_params("direction", values=["write", "read"])
@scenario("i2c.feature", "An address NACK is reported and recovers")
def test_address_nack_recovers(instance, direction):
    pass


@pytest.mark.usefixtures("bench")
@pytest.mark.board_params("instance", "i2c.instances")
@scenario("i2c.feature", "The NACKed address is on the bus, followed by a STOP")
def test_address_nack_on_bus(instance):
    pass


@pytest.mark.board_params("instance", "i2c.instances")
@scenario("i2c.feature", "A write without data is an address probe")
def test_zero_length_probe(instance):
    pass


@pytest.mark.usefixtures("bench")
@pytest.mark.board_params("instance", "i2c.instances")
@pytest.mark.board_params("direction", values=["write", "read"])
@scenario("i2c.feature", "SDA held low before the open loses arbitration, and the instance recovers")
def test_arbitration_lost_held(instance, direction):
    pass


@pytest.mark.usefixtures("bench")
@pytest.mark.board_params("instance", "i2c.instances")
@scenario("i2c.feature", "SDA pulled low inside an address bit loses arbitration, and the instance recovers")
def test_arbitration_lost_triggered(instance):
    pass


@scenario("i2c.feature", "Malformed, out-of-range and conflicting commands are refused")
def test_errors():
    pass


# --with i2c: master and target on the jumpered rows


@pytest.mark.usefixtures("bench", "ad3_released", "loop")
@scenario("i2c.feature", "The external pull-ups win against the MCU pull-down")
def test_option_bus_pullups():
    pass


@pytest.mark.usefixtures("bench", "ad3_released")
@pytest.mark.board_params("instance", "i2c.instances")
@scenario("i2c.feature", "Without the option nothing on the board pulls the I2C pins up")
def test_no_onboard_pullups(instance):
    pass


@pytest.mark.usefixtures("bench", "loop")
@pytest.mark.board_params("freq", "i2c.freqs.option")
@scenario("i2c.feature", "SCL runs at the requested frequency with the external pull-ups")
def test_option_timing(freq):
    pass


@pytest.mark.usefixtures("bench", "loop")
@pytest.mark.board_params("freq", "i2c.freqs.option")
@scenario("i2c.feature", "SCL and SDA rise within the limit of the mode")
def test_rise_time(freq):
    pass


@pytest.mark.usefixtures("loop")
@scenario("i2c.feature", "The target acknowledges its address")
def test_target_answers():
    pass


@pytest.mark.usefixtures("loop")
@pytest.mark.board_params("position", "i2c.nack_positions")
@scenario("i2c.feature", "A data NACK counts the acknowledged bytes")
def test_data_nack(position):
    pass


@pytest.mark.usefixtures("loop")
@pytest.mark.board_params("from_end", values=[1, 0])
@scenario("i2c.feature", "A data NACK of the next-to-last or the last byte")
def test_data_nack_at_the_end(from_end):
    pass


@pytest.mark.usefixtures("loop")
@scenario("i2c.feature", "An address NACK after a transfer in the other direction")
def test_address_nack_after_other_direction():
    pass


@pytest.mark.usefixtures("loop")
@pytest.mark.board_params("length", values=WRITE_READ_LENGTHS)
@scenario("i2c.feature", "The register file reads back what was written")
def test_write_read_regs(length):
    pass


@pytest.mark.usefixtures("loop")
@pytest.mark.board_params("length", "i2c.payload_lengths")
@scenario("i2c.feature", "Writes and reads of every length arrive whole")
def test_reload_lengths(length):
    pass


@pytest.mark.usefixtures("loop")
@scenario("i2c.feature", "A repeated START reads back the register")
def test_repeated_start():
    pass


@pytest.mark.usefixtures("bench", "loop")
@scenario("i2c.feature", "The repeated START is on the bus")
def test_repeated_start_on_bus():
    pass


@pytest.mark.usefixtures("loop")
@scenario("i2c.feature", "A continued write is one transaction")
def test_continue_session():
    pass


@pytest.mark.usefixtures("loop")
@scenario("i2c.feature", "A continued read is one transfer")
def test_receive_continue_session():
    pass


@pytest.mark.usefixtures("loop")
@pytest.mark.board_params("us", "i2c.stretch_us")
@pytest.mark.board_params("direction,position", values=STRETCH_POSITIONS)
@scenario("i2c.feature", "A clock-stretched transfer completes")
def test_clock_stretch_completes(us, direction, position):
    pass


@pytest.mark.usefixtures("bench", "loop")
@pytest.mark.board_params("us", "i2c.stretch_us")
@pytest.mark.board_params("direction,position", values=STRETCH_POSITIONS)
@scenario("i2c.feature", "The clock stretch is on the bus")
def test_clock_stretch_on_bus(us, direction, position):
    pass


@pytest.mark.usefixtures("loop")
@scenario("i2c.feature", "A misplaced STOP is a bus error, and the instance recovers")
def test_bus_error():
    pass


@pytest.mark.usefixtures("loop")
@scenario("i2c.feature", "An idle I2cStm does not answer the general call")
def test_general_call_does_not_hang():
    pass


@pytest.mark.usefixtures("loop")
@scenario("i2c.feature", "Close releases a bus the master holds")
def test_close_releases_held_bus():
    pass


@pytest.mark.usefixtures("loop")
@scenario("i2c.feature", "Each instance is master against the other as target")
def test_both_instances():
    pass


@given("the logic analyser DIO of the SCL pin of the instance and that of SDA if it is observed", target_fixture="dios")
def standalone_bus_dios(need, instance):
    return bus_dios(need, instance)


@given("the logic analyser DIOs of the SCL and SDA pins of the instance", target_fixture="dios")
def standalone_dios(need, instance):
    return need.dio(instance["scl"]), need.dio(instance["sda"])


@given("the logic analyser DIO of the SDA pin of the instance", target_fixture="sda_dio")
def standalone_sda_dio(need, instance):
    return need.dio(instance["sda"])


@when("the instance is opened standalone with the MCU pull-ups", target_fixture="opened")
def open_instance(fw, need, instance):
    return open_standalone(fw, need, instance)


@when("the instance is opened standalone at the frequency with the MCU pull-ups", target_fixture="opened")
def open_instance_at_freq(fw, need, instance, freq):
    return open_standalone(fw, need, instance, freq=freq)


@when(parsers.parse("the instance is opened standalone at {bus_freq:d} Hz with the MCU pull-ups"), target_fixture="opened")
def open_instance_at(fw, need, instance, bus_freq):
    return open_standalone(fw, need, instance, freq=bus_freq)


@when("the instance is closed")
def close_instance(fw, instance):
    fw.i2c.close(instance["index"])


@when(parsers.parse("the logic analyser is armed for {window_us:d} us of the bus at {bus_freq:d} Hz"), target_fixture="capture")
def arm_for_us(ad3, dios, window_us, bus_freq):
    scl, sda = dios
    return arm_bus(ad3, scl, sda, bus_freq, window_us / 1e6)


@when(parsers.parse("the logic analyser is armed for {window_ms:d} ms of the bus at {bus_freq:d} Hz"), target_fixture="capture")
def arm_for_ms(ad3, dios, window_ms, bus_freq):
    scl, sda = dios
    # The master holds SCL low after the write until the host's `i2c.read` arrives: the window spans that round trip.
    return arm_bus(ad3, scl, sda, bus_freq, window_ms / 1e3)


@when(parsers.parse("the logic analyser is armed for {periods:d} SCL periods at the frequency"), target_fixture="capture")
def arm_for_periods(ad3, dios, freq, periods):
    scl, sda = dios
    return arm_bus(ad3, scl, sda, freq, periods / freq)


@when(parsers.parse("the instance writes {payload} to the absent address"))
def write_absent(fw, i2c_cfg, instance, payload):
    fw.i2c.write(instance["index"], i2c_cfg["absent_addr"], hex_bytes(payload))


@when("the instance sends a zero-length write to the absent address", target_fixture="reply")
def probe_absent(fw, i2c_cfg, instance):
    return fw.i2c.write(instance["index"], i2c_cfg["absent_addr"])


@then("it reports the default TIMINGR of the driver's Config")
def default_timing(opened):
    assert opened.timing == DEFAULT_TIMING


@then(
    "opened standalone at every standalone and option frequency of the board file, it reports the I2cTiming value for "
    "the reported kernel clock, and closes"
)
def computed_timings(fw, need, i2c_cfg, instance):
    for freq in sorted({*i2c_cfg["freqs"]["standalone"], *i2c_cfg["freqs"]["option"]}):
        opened = open_standalone(fw, need, instance, freq=freq)
        assert opened.timing == expected_timing(opened.kernel, freq)
        fw.i2c.close(instance["index"])


@then(parsers.parse("opened standalone with the TIMINGR {timingr:x}, it reports that TIMINGR"))
def given_timing(fw, need, instance, timingr):
    assert open_standalone(fw, need, instance, timing=timingr).timing == timingr


@then(parsers.parse('a write of {payload} from the instance to the absent address answers "{result}"'))
def write_absent_answers(fw, i2c_cfg, instance, payload, result):
    assert fw.i2c.write(instance["index"], i2c_cfg["absent_addr"], hex_bytes(payload)).result == result


@then("the SCL capture, within 2 s, runs at up to 101 % of 100 kHz with the tLOW and tHIGH minima of Standard mode")
def scl_in_spec(capture, dios):
    check_scl(capture.wait(timeout=2.0), dios[0], 100_000, (0.0, 1.01))


@then("the SCL capture, within 2 s, runs within the tolerance of the frequency with the tLOW and tHIGH minima of the mode")
def scl_within_tolerance(i2c_cfg, capture, dios, freq):
    check_scl(capture.wait(timeout=2.0), dios[0], freq, i2c_cfg["tolerance"])


@then(
    parsers.parse(
        'twice in a row, a transfer in the direction to the absent address answers "{result}" with no byte sent, '
        'and the instance reports only the "{hook}" hook'
    )
)
def address_nack_twice(fw, i2c_cfg, instance, direction, result, hook):
    index, absent = instance["index"], i2c_cfg["absent_addr"]
    for _ in range(2):
        if direction == "write":
            reply = fw.i2c.write(index, absent, b"\x01\x02")
            assert (reply.sent, reply.result) == (0, result)
        else:
            assert fw.i2c.read(index, absent, 2).result == result
        assert fw.i2c.hooks(index) == [hook]


@then(
    "a write of 0x55 and then a read of 1 byte from the absent address, each captured for 400 us at 100000 Hz, "
    "each start with the absent address and its R/W bit, NACKed, without data and followed by a STOP"
)
def address_nack_on_bus(fw, ad3, i2c_cfg, instance, dios):
    scl, sda = dios
    for read in (False, True):
        capture = arm_bus(ad3, scl, sda, 100_000, 400e-6)
        if read:
            fw.i2c.read(instance["index"], i2c_cfg["absent_addr"], 1)
        else:
            fw.i2c.write(instance["index"], i2c_cfg["absent_addr"], b"\x55")
        samples = capture.wait(timeout=2.0)
        transfers = i2c_decode(samples.channel(scl), samples.channel(sda))
        assert transfers, "no transfer decoded"
        first = transfers[0]
        assert (first.address, first.read, first.address_ack, first.data, first.stop) == (i2c_cfg["absent_addr"], read, False, [], True)


@then(parsers.parse('the reply has {sent:d} bytes sent and the result "{result}"'))
def probe_reply(reply, sent, result):
    assert (reply.sent, reply.result) == (sent, result)


@then(parsers.parse('the instance reports only the "{hook}" hook'))
def instance_hooks(fw, instance, hook):
    assert fw.i2c.hooks(instance["index"]) == [hook]


@then(
    parsers.parse(
        "with the AD3 driving SDA low from before the open, the instance opened standalone at {bus_freq:d} Hz answers "
        '"{result}" to a transfer in the direction to the arbitration address and reports the "{hook}" hook, then the '
        "AD3 releases SDA"
    )
)
def arbitration_lost_held(fw, ad3, need, i2c_cfg, instance, direction, sda_dio, bus_freq, result, hook):
    index, address = instance["index"], i2c_cfg["arbitration_addr"]
    ad3.dio.drive(sda_dio, 0)
    try:
        open_standalone(fw, need, instance, freq=bus_freq)
        if direction == "write":
            reply = fw.i2c.write(index, address, b"\x00")
            assert reply.result == result
        else:
            assert fw.i2c.read(index, address, 1).result == result
        assert hook in fw.i2c.hooks(index)
    finally:
        ad3.dio.release(sda_dio)


@then(
    parsers.parse(
        "with the AD3 pulling SDA low for one SCL period inside the second address bit after a START, a write of 0x00 "
        'to the arbitration address answers "{result}" and the instance reports the "{hook}" hook, then the AD3 pattern '
        "stops"
    )
)
def arbitration_lost_triggered(fw, ad3, i2c_cfg, instance, dios, opened, result, hook):
    scl, sda = dios
    index, address = instance["index"], i2c_cfg["arbitration_addr"]
    delay, width = arbitration_window(opened.timing, opened.kernel)
    try:
        inject_on_start(ad3, scl, sda, delay, width)
        assert fw.i2c.write(index, address, b"\x00").result == result
        assert hook in fw.i2c.hooks(index)
    finally:
        ad3.pattern.stop()


@then(parsers.parse('a write of {payload} from the instance to the arbitration address answers "{result}"'))
def write_arbitration_address(fw, i2c_cfg, instance, payload, result):
    assert fw.i2c.write(instance["index"], i2c_cfg["arbitration_addr"], hex_bytes(payload)).result == result


@given("the first standalone instance, with its pins resolved, that no enabled option loads", target_fixture="first_instance")
def first_instance(fw, need, i2c_cfg):
    instance = i2c_cfg["instances"][0]
    index, scl, sda = instance["index"], fw.pin(instance["scl"]), fw.pin(instance["sda"])
    need.unloaded(scl, sda)
    return index, scl, sda


@then("every malformed, out-of-range or not-open command line on it fails with its reason")
def errors_closed(fw, first_instance):
    index, scl, sda = first_instance
    cases = [
        ("i2c.open", "usage"),
        (f"i2c.open {index} sda={sda}", "usage"),
        (f"i2c.open {index} scl={scl}", "usage"),
        (f"i2c.open {index} scl={scl} sda={sda} freq=100000 timing=0x10", "usage"),
        (f"i2c.open {index} scl={scl} sda={sda} pull=down", "usage"),
        (f"i2c.open {index} scl={scl} sda={sda} speed=1", "usage"),
        (f"i2c.open 0 scl={scl} sda={sda}", "range"),
        (f"i2c.open 99 scl={scl} sda={sda}", "range"),
        (f"i2c.open {index} scl={scl} sda={sda} freq=19999", "range"),
        (f"i2c.open {index} scl={scl} sda={sda} freq=400001", "range"),
        (f"i2c.open {index} scl={sda} sda={scl}", "pin"),
        (f"i2c.write {index} 0x10 00", "notopen"),
        (f"i2c.read {index} 0x10 1", "notopen"),
        (f"i2c.close {index}", "notopen"),
    ]
    for line, expected in cases:
        assert reason(fw, line) == expected, line


@when("it is opened with the MCU pull-ups")
def open_first(fw, first_instance):
    index, scl, sda = first_instance
    fw.i2c.open(index, scl, sda, pull="up")


@then("every command line that claims its pins again or that the open instance refuses fails with its reason")
def errors_open(fw, first_instance):
    index, scl, sda = first_instance
    cases = [
        (f"i2c.open {index} scl={scl} sda={sda}", "busy"),
        (f"i2cs.open {index} scl={scl} sda={sda}", "busy"),
        (f"eeprom.attach {index} scl={scl} sda={sda}", "busy"),
        (f"gpio.cfg {scl} in", "busy"),
        (f"i2c.write {index} 0x80 00", "range"),
        (f"i2c.write {index} 0x10 0", "usage"),
        (f"i2c.write {index} 0x10 00 next=later", "usage"),
        (f"i2c.write {index} 0x10 00 len=1", "usage"),
        (f"i2c.write {index} 0x10 - len=1025", "range"),
        (f"i2c.read {index} 0x10 0", "range"),
        (f"i2c.read {index} 0x10 {I2C_HEX_MAX + 1}", "range"),
        (f"i2c.read {index} 0x10 1025 out=crc", "range"),
        (f"i2c.read {index} 0x10 1 out=bin", "usage"),
    ]
    for line, expected in cases:
        assert reason(fw, line) == expected, line


@when("it is closed and the i2cs target opens on its pins")
def close_first_open_target(fw, first_instance):
    index, scl, sda = first_instance
    fw.i2c.close(index)
    fw.i2cs.open(index, scl, sda)


@then(parsers.parse('opening it as an I2C master fails with "{refusal}"'))
def open_first_refused(fw, first_instance, refusal):
    index, scl, sda = first_instance
    expect_error(refusal, fw.i2c.open, index, scl, sda)


@then(parsers.parse("every SCL and SDA pin of the master and the target, configured as an input with a pull-down, reads {level:d}"))
def row_pins_pulled_up(fw, loop, level):
    for end in ("master", "target"):
        for line in ("scl", "sda"):
            pin = loop[end][line]
            fw.gpio.cfg(pin, "in", pull="down")
            try:
                assert fw.gpio.get(pin) == level, pin
            finally:
                fw.gpio.release(pin)


@then(
    parsers.parse(
        "each of the SCL and SDA pins of the instance, once checked to be loaded by no enabled option, configured as "
        "an input with a pull-down reads {level:d}"
    )
)
def instance_pins_pulled_down(fw, need, instance, level):
    for line in ("scl", "sda"):
        pin = instance[line]
        need.unloaded(pin)
        fw.gpio.cfg(pin, "in", pull="down")
        try:
            assert fw.gpio.get(pin) == level, pin
        finally:
            fw.gpio.release(pin)


@when("the loop is opened", target_fixture="opened_loop")
def opened_loop(fw, loop):
    return open_loop(fw, loop)


@when("the loop is opened at the frequency", target_fixture="opened_loop")
def opened_loop_at_freq(fw, loop, freq):
    return open_loop(fw, loop, freq=freq)


@when(parsers.parse("the loop is opened at {bus_freq:d} Hz"), target_fixture="opened_loop")
def opened_loop_at(fw, loop, bus_freq):
    return open_loop(fw, loop, freq=bus_freq)


@when("the loop is opened with the target in sink mode", target_fixture="opened_loop")
def opened_loop_sink(fw, loop):
    return open_loop(fw, loop, mode="sink")


@when("the loop is opened at the fault frequency with the target in sink mode", target_fixture="opened_loop")
def opened_loop_fault(fw, i2c_cfg, loop):
    return open_loop(fw, loop, freq=i2c_cfg["fault_freq"], mode="sink")


@when("the logic analyser DIOs of the SCL and SDA pins of the master", target_fixture="dios")
def master_dios(need, loop):
    return need.dio(loop["master"]["scl"]), need.dio(loop["master"]["sda"])


@given("the logic analyser DIO of the SCL pin of the master", target_fixture="scl_dio")
def master_scl_dio(need, loop):
    return need.dio(loop["master"]["scl"])


@given("the scope channels on the SCL and SDA pins of the master, which the wiring set must have", target_fixture="scopes")
def master_scopes(need, loop):
    scopes = {line: need.optional_scope(loop["master"][line]) for line in ("scl", "sda")}
    if None in scopes.values():
        pytest.skip("no scope on the I2C rows in this wiring set")
    return scopes


@when("the scope is armed on SCL rising through half of VDD, 8192 samples at 100 MHz", target_fixture="scope_capture")
def arm_rows_scope(ad3, scopes):
    rate = 100e6
    return rate, arm_scope(ad3, list(scopes.values()), rate, 8192, scopes["scl"], VDD / 2)


@when(parsers.parse("the master writes {count:d} bytes of PRBS data with seed {prbs_seed:d} to the target"))
def write_prbs(fw, opened_loop, count, prbs_seed):
    master, _, address = opened_loop
    fw.i2c.write(master, address, len=count, pattern="prbs", seed=prbs_seed)


@then("every rise of SCL and SDA from 30 % to 70 % of VDD, captured within 2 s, is within the rise time of the mode of the frequency")
def rise_within_limit(i2c_cfg, freq, scopes, scope_capture):
    limit = i2c_cfg["rise_ns"]["sm" if freq <= 100_000 else "fm"] * 1e-9
    rate, pending = scope_capture
    samples = pending.wait(timeout=2.0)
    for line, channel in scopes.items():
        rises = rise_times(samples[channel], rate, 0.3 * VDD, 0.7 * VDD)
        assert rises, f"no rising edge on {line}"
        assert max(rises) <= limit, f"{line}: {max(rises) * 1e9:.0f} ns rise (use 2.2 kOhm pull-ups)"


@when(parsers.parse("the target's registers from {register:x} are set to {payload}"))
def set_registers(fw, opened_loop, register, payload):
    _, target, _ = opened_loop
    fw.i2cs.regs(target, register, hex_bytes(payload))


@when(parsers.parse("the bytes {first:d} to {last:d} are set in the target's registers from {register:x}"))
def set_register_range(fw, opened_loop, register, first, last):
    _, target, _ = opened_loop
    fw.i2cs.regs(target, register, bytes(range(first, last + 1)))


@when(parsers.parse("the master writes the pointer {pointer} with next={next_mode}"))
def write_pointer(fw, opened_loop, pointer, next_mode):
    master, _, address = opened_loop
    fw.i2c.write(master, address, hex_bytes(pointer), next=next_mode)


@when(parsers.parse("the master reads {count:d} byte from the target"))
def read_target(fw, opened_loop, count):
    master, _, address = opened_loop
    fw.i2c.read(master, address, count)


@when(parsers.parse("the master reads {first_count:d} bytes with next={next_mode}, then {second_count:d} bytes"), target_fixture="reads")
def read_continued(fw, opened_loop, first_count, next_mode, second_count):
    master, _, address = opened_loop
    first = fw.i2c.read(master, address, first_count, next=next_mode)
    second = fw.i2c.read(master, address, second_count)
    return first, second


@given("the byte to NACK is the one at the position", target_fixture="nack_position")
def nack_at_position(position):
    return position


@given("the byte to NACK is the from-end count of bytes before the last byte of the NACK length", target_fixture="nack_position")
def nack_from_end(i2c_cfg, from_end):
    length = i2c_cfg["nack_length"]
    return length - from_end


@when("the target is set to NACK that byte")
def target_nacks(fw, opened_loop, nack_position):
    _, target, _ = opened_loop
    fw.i2cs.cfg(target, nack=nack_position)


@when("the master writes the NACK length of PRBS data seeded with the position of that byte", target_fixture="reply")
def write_to_nack(fw, i2c_cfg, opened_loop, nack_position):
    master, _, address = opened_loop
    return fw.i2c.write(master, address, len=i2c_cfg["nack_length"], pattern="prbs", seed=nack_position)


@when("the target's configuration is reset")
def target_cfg_reset(fw, opened_loop):
    _, target, _ = opened_loop
    fw.i2cs.cfg(target)


@when(parsers.parse("the target is set to answer reads with PRBS data seeded with the length"))
def target_pattern(fw, opened_loop, length):
    _, target, _ = opened_loop
    fw.i2cs.cfg(target, pattern="prbs", seed=length)


@when(parsers.parse("the target is set to turn byte {faultat:d} of a read into a STOP"))
def target_fault(fw, opened_loop, faultat):
    _, target, _ = opened_loop
    fw.i2cs.cfg(target, fault="stop", faultat=faultat)


@when("the EEPROM adapter is attached on the instance and the pins of the target")
def attach_eeprom_on_target(fw, loop):
    target = loop["target"]
    fw.eeprom.attach(target["index"], target["scl"], target["sda"])


@when("the instance of the master is opened on its pins")
def open_master(fw, loop):
    master = loop["master"]
    fw.i2c.open(master["index"], master["scl"], master["sda"])


@when("the master is closed")
def close_master(fw, opened_loop):
    master, _, _ = opened_loop
    fw.i2c.close(master)


@when("the master is opened again on its pins")
def reopen_master(fw, loop, opened_loop):
    master, _, _ = opened_loop
    fw.i2c.open(master, loop["master"]["scl"], loop["master"]["sda"])


@then(parsers.parse('a zero-length write to the target answers "{result}"'))
def probe_target(fw, opened_loop, result):
    master, _, address = opened_loop
    assert fw.i2c.write(master, address).result == result


@then("the master reports no hook")
def master_no_hooks(fw, opened_loop):
    master, _, _ = opened_loop
    assert fw.i2c.hooks(master) == []


@then(parsers.parse("the target counts {writes:d} write"))
def target_writes(fw, opened_loop, writes):
    _, target, _ = opened_loop
    assert fw.i2cs.status(target).writes == writes


@then(parsers.parse('the reply counts the bytes before that byte as sent and answers "{result}"'))
def nack_reply(reply, nack_position, result):
    assert (reply.sent, reply.result) == (nack_position - 1, result)


@then(parsers.parse('the master does not report the "{hook}" hook'))
def master_hook_absent(fw, opened_loop, hook):
    master, _, _ = opened_loop
    assert hook not in fw.i2c.hooks(master)


@then(parsers.parse("the target received the bytes before that byte and NACKed {nacked:d}"))
def target_nacked(fw, opened_loop, nack_position, nacked):
    _, target, _ = opened_loop
    status = fw.i2cs.status(target)
    assert (status.rx, status.nacked) == (nack_position - 1, nacked)


@then(parsers.parse('writing the NACK length to the target answers "{result}"'))
def write_nack_length(fw, i2c_cfg, opened_loop, result):
    master, _, address = opened_loop
    assert fw.i2c.write(master, address, len=i2c_cfg["nack_length"]).result == result


@then(parsers.parse('writing the inc pattern of {count:d} bytes to the target answers "{result}"'))
def write_inc_answers(fw, opened_loop, count, result):
    master, _, address = opened_loop
    assert fw.i2c.write(master, address, generate(count)).result == result


@then(parsers.parse('a read of {count:d} bytes from the target answers "{result}"'))
def read_target_answers(fw, opened_loop, count, result):
    master, _, address = opened_loop
    assert fw.i2c.read(master, address, count).result == result


@then(parsers.parse('a write of {payload} from the master to the absent address answers "{result}" with {sent:d} bytes sent'))
def write_absent_from_master(fw, i2c_cfg, opened_loop, payload, result, sent):
    master, _, _ = opened_loop
    reply = fw.i2c.write(master, i2c_cfg["absent_addr"], hex_bytes(payload))
    assert (reply.sent, reply.result) == (sent, result)


@then(parsers.parse('the master reports only the "{hook}" hook'))
def master_only_hook(fw, opened_loop, hook):
    master, _, _ = opened_loop
    assert fw.i2c.hooks(master) == [hook]


@then(parsers.parse('a write of {payload} to the target answers "{result}"'))
def write_target_answers(fw, opened_loop, payload, result):
    master, _, address = opened_loop
    assert fw.i2c.write(master, address, hex_bytes(payload)).result == result


@then(parsers.parse('a write of {payload} to the target with next={next_mode} answers "{result}"'))
def write_target_next_answers(fw, opened_loop, payload, next_mode, result):
    master, _, address = opened_loop
    assert fw.i2c.write(master, address, hex_bytes(payload), next=next_mode).result == result


@then(parsers.parse('a read of {count:d} bytes from the master at the absent address answers "{result}"'))
def read_absent_from_master(fw, i2c_cfg, opened_loop, count, result):
    master, _, _ = opened_loop
    assert fw.i2c.read(master, i2c_cfg["absent_addr"], count).result == result


@given("bytes 1 to the length of the inc pattern", target_fixture="data")
def inc_without_pointer(length):
    return generate(length + 1)[1:]


@then("a write of the inc pattern of the length and one more byte sends them all")
def write_inc_with_pointer(fw, opened_loop, length):
    master, _, address = opened_loop
    assert fw.i2c.write(master, address, len=length + 1).sent == length + 1


@then("a read of the length from the target gives those bytes")
def read_length_data(fw, opened_loop, length, data):
    master, _, address = opened_loop
    assert fw.i2c.read(master, address, length).data == data


@then(parsers.parse("those bytes are in the target's registers from {register:d}"))
def registers_hold_data(fw, opened_loop, length, data, register):
    _, target, _ = opened_loop
    assert fw.i2cs.dump(target, register, length) == data


@then(parsers.parse('a write of the length of PRBS data with seed {prbs_seed:d} sends the length and answers "{result}"'))
def write_prbs_length(fw, opened_loop, length, prbs_seed, result):
    master, _, address = opened_loop
    reply = fw.i2c.write(master, address, len=length, pattern="prbs", seed=prbs_seed)
    assert (reply.sent, reply.result) == (length, result)


@then(parsers.parse("the target received the length with the CRC of PRBS data of the length with seed {prbs_seed:d}"))
def target_received_crc(fw, opened_loop, length, prbs_seed):
    _, target, _ = opened_loop
    status = fw.i2cs.status(target)
    assert (status.rx, status.crc) == (length, crc_text(generate(length, "prbs", prbs_seed)))


@then(
    parsers.parse(
        'a read of the length with out={out} answers "{result}" with the length and the CRC of PRBS data of the length '
        "seeded with the length"
    )
)
def read_crc(fw, opened_loop, length, out, result):
    master, _, address = opened_loop
    read = fw.i2c.read(master, address, length, out=out)
    assert (read.result, read.length, read.crc) == (result, length, crc_text(generate(length, "prbs", length)))


@then(parsers.parse("a read of {count:d} bytes from the target gives {payload}"))
def read_gives(fw, opened_loop, count, payload):
    master, _, address = opened_loop
    assert fw.i2c.read(master, address, count).data == hex_bytes(payload)


@then(parsers.parse("the inc pattern of {length_of_pattern:d} bytes is what a read of {count:d} bytes from the target gives"))
def read_gives_inc(fw, opened_loop, count, length_of_pattern):
    master, _, address = opened_loop
    assert fw.i2c.read(master, address, count).data == generate(length_of_pattern)


@then(parsers.parse("the target counts {writes:d} write, {reads:d} read and {stops:d} STOP"))
def target_counts(fw, opened_loop, writes, reads, stops):
    _, target, _ = opened_loop
    status = fw.i2cs.status(target)
    assert (status.writes, status.reads, status.stops) == (writes, reads, stops)


@then(parsers.parse("the target counts {writes:d} write and {stops:d} STOP, and its last transfer is {payload}"))
def target_counts_last(fw, opened_loop, writes, stops, payload):
    _, target, _ = opened_loop
    status = fw.i2cs.status(target)
    assert (status.writes, status.stops, status.last) == (writes, stops, hex_bytes(payload))


@then(parsers.parse("the target's registers from {register:x} hold {payload}"))
def registers_hold(fw, opened_loop, register, payload):
    _, target, _ = opened_loop
    expected = hex_bytes(payload)
    assert fw.i2cs.dump(target, register, len(expected)) == expected


@then(parsers.parse('both reads answer "{result}"'))
def both_reads(reads, result):
    first, second = reads
    assert (first.result, second.result) == (result, result)


@then(parsers.parse("together they give the bytes {first:d} to {last:d}"))
def reads_together(reads, first, last):
    first_read, second_read = reads
    assert (first_read.data or b"") + (second_read.data or b"") == bytes(range(first, last + 1))


@then(parsers.parse("the target counts {reads:d} read and {stops:d} STOP"))
def target_counts_reads(fw, opened_loop, reads, stops):
    _, target, _ = opened_loop
    status = fw.i2cs.status(target)
    assert (status.reads, status.stops) == (reads, stops)


@then(parsers.parse('the master reports the "{hook}" hook'))
def master_hook(fw, opened_loop, hook):
    master, _, _ = opened_loop
    assert hook in fw.i2c.hooks(master)


@then(parsers.parse("the logic analyser decodes at least {count:d} transfers within 2 s"), target_fixture="transfers")
def decoded(capture, dios, count):
    scl, sda = dios
    samples = capture.wait(timeout=2.0)
    transfers = i2c_decode(samples.channel(scl), samples.channel(sda))
    assert len(transfers) >= count, f"{len(transfers)} transfer(s) captured: the read fell outside the window"
    return transfers


@then(parsers.parse("the first writes {payload} without a STOP"))
def first_transfer(transfers, payload):
    write = transfers[0]
    assert (write.read, write.payload, write.stop) == (False, hex_bytes(payload), False)


@then(parsers.parse("the second is a read after a repeated START that gives {payload}, ended by a STOP"))
def second_transfer(transfers, payload):
    read = transfers[1]
    assert (read.restart, read.read, read.payload, read.stop) == (True, True, hex_bytes(payload), True)


@then("the transfer in the direction, stretched by the target for the stretch time at the position, completes")
def stretch_completes(fw, loop, us, direction, position):
    _stretched_transfer(fw, loop, direction, position, us)


@when(
    "the transfer in the direction, stretched by the target for the stretch time at the position, completes with the "
    "logic analyser armed right before it on a falling SCL for the stretch and 2 ms",
    target_fixture="stretch_capture",
)
def stretch_captured(fw, ad3, loop, scl_dio, us, direction, position):
    seconds = us * 1e-6 + 2e-3
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / seconds, 10e6)

    def arm():
        return ad3.logic.arm(rate, int(rate * seconds), trigger=(scl_dio, "falling"), pretrigger=0.01)

    return rate, _stretched_transfer(fw, loop, direction, position, us, arm)


@then("the longest SCL low phase, captured within 2 s, is at least 95 % of the stretch time")
def longest_low(scl_dio, us, stretch_capture):
    rate, capture = stretch_capture
    trace = capture.wait(timeout=2.0).channel(scl_dio)
    edges = [index for index in range(1, len(trace)) if trace[index] != trace[index - 1]]
    lows = [(edges[i + 1] - edges[i]) / rate for i in range(len(edges) - 1) if not trace[edges[i]]]
    assert lows and max(lows) >= 0.95 * us * 1e-6, f"longest SCL low {max(lows, default=0) * 1e6:.0f} us"


@then(parsers.parse('a general call write of {payload} from the master answers "{result}"'))
def general_call(fw, loop, payload, result):
    assert fw.i2c.write(loop["master"]["index"], 0x00, hex_bytes(payload)).result == result


@then(parsers.parse('a zero-length write from the master to {chip:x} answers "{result}"'))
def probe_from_master(fw, loop, chip, result):
    assert fw.i2c.write(loop["master"]["index"], chip).result == result


@then(parsers.parse("unless the run is against the fake, the SCL and SDA pins of the master each read {level:d} as inputs"))
def master_pins_released(fw, request, loop, level):
    if not request.config.getoption("--fake"):
        for line in ("scl", "sda"):
            pin = loop["master"][line]
            fw.gpio.cfg(pin, "in")
            try:
                assert fw.gpio.get(pin) == level, pin
            finally:
                fw.gpio.release(pin)


@then(
    "each instance, as master against the other as target at the target's address, writes 0x10 0xA5, reads 0xA5 back "
    "after writing the pointer 0x10 with next=restart, then both close"
)
def both_instances(fw, loop):
    for master, target in ((loop["master"], loop["target"]), (loop["target"], loop["master"])):
        fw.i2c.open(master["index"], master["scl"], master["sda"])
        address = fw.i2cs.open(target["index"], target["scl"], target["sda"], addr=loop["target"]["addr"])
        assert fw.i2c.write(master["index"], address, b"\x10\xa5").result == "complete"
        fw.i2c.write(master["index"], address, b"\x10", next="restart")
        assert fw.i2c.read(master["index"], address, 1).data == b"\xa5"
        fw.i2c.close(master["index"])
        fw.i2cs.close(target["index"])
