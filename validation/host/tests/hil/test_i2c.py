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
"""

from __future__ import annotations

from typing import Any

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

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

pytestmark = pytest.mark.uses_option("i2c")

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


# standalone, bundle1, pull=up


@pytest.mark.board_params("instance", "i2c.instances")
def test_open_replies_timing(fw, need, i2c_cfg, instance):
    """Without `freq` the driver's default `Config` (0x70B03D3D on STM32WB/WBA, B.1f); with `freq` the `I2cTiming`
    value for the reported kernel clock; `timing=` is written as given."""
    opened = open_standalone(fw, need, instance)
    assert opened.timing == DEFAULT_TIMING
    fw.i2c.close(instance["index"])
    for freq in sorted({*i2c_cfg["freqs"]["standalone"], *i2c_cfg["freqs"]["option"]}):
        opened = open_standalone(fw, need, instance, freq=freq)
        assert opened.timing == expected_timing(opened.kernel, freq)
        fw.i2c.close(instance["index"])
    assert open_standalone(fw, need, instance, timing=0x10B0172F).timing == 0x10B0172F


@pytest.mark.ad3
@pytest.mark.board_params("instance", "i2c.instances")
def test_default_timing_in_spec(fw, ad3, need, bench, i2c_cfg, instance):
    """B.1f: the default TIMINGR runs Standard mode on the bus: tLOW >= 4.7 us, tHIGH >= 4.0 us, f <= 100 kHz."""
    scl, sda = bus_dios(need, instance)
    open_standalone(fw, need, instance)
    capture = arm_bus(ad3, scl, sda, 100_000, 400e-6)
    assert fw.i2c.write(instance["index"], i2c_cfg["absent_addr"], b"\x00").result == "nack"
    check_scl(capture.wait(timeout=2.0), scl, 100_000, (0.0, 1.01))


@pytest.mark.ad3
@pytest.mark.board_params("instance", "i2c.instances")
@pytest.mark.board_params("freq", "i2c.freqs.standalone")
def test_timing(fw, ad3, need, bench, i2c_cfg, instance, freq):
    scl, sda = bus_dios(need, instance)
    open_standalone(fw, need, instance, freq=freq)
    capture = arm_bus(ad3, scl, sda, freq, 30 / freq)
    fw.i2c.write(instance["index"], i2c_cfg["absent_addr"], b"\x00")
    check_scl(capture.wait(timeout=2.0), scl, freq, i2c_cfg["tolerance"])


@pytest.mark.board_params("instance", "i2c.instances")
@pytest.mark.board_params("direction", values=["write", "read"])
def test_address_nack_recovers(fw, need, i2c_cfg, instance, direction):
    """B.1a/b: an address NACK answers `nack` (`sent=0`) after `EVT i2c hook=notfound`, and the same command behaves
    the same again: no stale NACKF, no callback left armed."""
    index, absent = instance["index"], i2c_cfg["absent_addr"]
    open_standalone(fw, need, instance)
    for _ in range(2):
        if direction == "write":
            reply = fw.i2c.write(index, absent, b"\x01\x02")
            assert (reply.sent, reply.result) == (0, "nack")
        else:
            assert fw.i2c.read(index, absent, 2).result == "nack"
        assert fw.i2c.hooks(index) == ["notfound"]


@pytest.mark.ad3
@pytest.mark.board_params("instance", "i2c.instances")
def test_address_nack_on_bus(fw, ad3, need, bench, i2c_cfg, instance):
    """The address and the R/W bit on the bus, NACKed, followed by a STOP."""
    scl = need.dio(instance["scl"])
    sda = need.dio(instance["sda"])
    open_standalone(fw, need, instance, freq=100_000)
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


@pytest.mark.board_params("instance", "i2c.instances")
def test_zero_length_probe(fw, need, i2c_cfg, instance):
    """`-` without `len` writes no data (NBYTES=0, R2): an address probe that answers `nack` here."""
    open_standalone(fw, need, instance)
    reply = fw.i2c.write(instance["index"], i2c_cfg["absent_addr"])
    assert (reply.sent, reply.result) == (0, "nack")
    assert fw.i2c.hooks(instance["index"]) == ["notfound"]


@pytest.mark.ad3
@pytest.mark.board_params("instance", "i2c.instances")
@pytest.mark.board_params("direction", values=["write", "read"])
def test_arbitration_lost_held(fw, ad3, need, bench, i2c_cfg, instance, direction):
    """B.1e: SDA held low by the AD3 before `i2c.open` (no START is latched, BUSY stays 0): the first address 1 bit
    loses arbitration (`buserror`, `EVT hook=arblost`); once SDA is released the same instance works (no device:
    `nack`)."""
    sda = need.dio(instance["sda"])
    index, address = instance["index"], i2c_cfg["arbitration_addr"]
    ad3.dio.drive(sda, 0)
    try:
        open_standalone(fw, need, instance, freq=100_000)
        if direction == "write":
            reply = fw.i2c.write(index, address, b"\x00")
            assert reply.result == "buserror"
        else:
            assert fw.i2c.read(index, address, 1).result == "buserror"
        assert "arblost" in fw.i2c.hooks(index)
    finally:
        ad3.dio.release(sda)
    assert fw.i2c.write(index, address, b"\x00").result == "nack"
    assert fw.i2c.hooks(index) == ["notfound"]


@pytest.mark.ad3
@pytest.mark.board_params("instance", "i2c.instances")
def test_arbitration_lost_triggered(fw, ad3, need, bench, i2c_cfg, instance):
    """B.1e: an open instance writes to 0x7f while the AD3 pulls SDA low for one SCL period inside the second
    address bit (started by the START detector): `buserror` and `EVT hook=arblost`, then a good transfer."""
    scl, sda = need.dio(instance["scl"]), need.dio(instance["sda"])
    index, address = instance["index"], i2c_cfg["arbitration_addr"]
    opened = open_standalone(fw, need, instance, freq=100_000)
    delay, width = arbitration_window(opened.timing, opened.kernel)
    try:
        inject_on_start(ad3, scl, sda, delay, width)
        assert fw.i2c.write(index, address, b"\x00").result == "buserror"
        assert "arblost" in fw.i2c.hooks(index)
    finally:
        ad3.pattern.stop()
    assert fw.i2c.write(index, address, b"\x00").result == "nack"


def test_errors(fw, need, i2c_cfg):
    instance = i2c_cfg["instances"][0]
    index, scl, sda = instance["index"], fw.pin(instance["scl"]), fw.pin(instance["sda"])
    need.unloaded(scl, sda)
    cases = [
        ("i2c.open", "usage"),
        (f"i2c.open {index} sda={sda}", "usage"),
        (f"i2c.open {index} scl={scl}", "usage"),
        (f"i2c.open {index} scl={scl} sda={sda} freq=100000 timing=0x10", "usage"),
        (f"i2c.open {index} scl={scl} sda={sda} pull=down", "usage"),
        (f"i2c.open {index} scl={scl} sda={sda} speed=1", "usage"),
        (f"i2c.open 2 scl={scl} sda={sda}", "range"),
        (f"i2c.open 4 scl={scl} sda={sda}", "range"),
        (f"i2c.open {index} scl={scl} sda={sda} freq=19999", "range"),
        (f"i2c.open {index} scl={scl} sda={sda} freq=400001", "range"),
        (f"i2c.open {index} scl={sda} sda={scl}", "pin"),
        (f"i2c.write {index} 0x10 00", "notopen"),
        (f"i2c.read {index} 0x10 1", "notopen"),
        (f"i2c.close {index}", "notopen"),
    ]
    for line, expected in cases:
        assert reason(fw, line) == expected, line
    fw.i2c.open(index, scl, sda, pull="up")
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
    fw.i2c.close(index)
    fw.i2cs.open(index, scl, sda)
    expect_error("busy", fw.i2c.open, index, scl, sda)


# --with i2c: master and target on the jumpered rows


@pytest.mark.requires_option("i2c")
def test_option_bus_pullups(fw, bench, ad3_released, loop):
    """The external pull-ups win against the MCU pull-down on every row pin."""
    for end in ("master", "target"):
        for line in ("scl", "sda"):
            pin = loop[end][line]
            fw.gpio.cfg(pin, "in", pull="down")
            try:
                assert fw.gpio.get(pin) == 1, pin
            finally:
                fw.gpio.release(pin)


@pytest.mark.board_params("instance", "i2c.instances")
@pytest.mark.conflicts_option("i2c")
def test_no_onboard_pullups(fw, need, bench, ad3_released, instance):
    """Without the option nothing on the board pulls the I2C pins up (documents WBA55 PB1/PB2): the MCU pull-down
    wins, which is why the standalone cases need `pull=up`."""
    for line in ("scl", "sda"):
        pin = instance[line]
        need.unloaded(pin)
        fw.gpio.cfg(pin, "in", pull="down")
        try:
            assert fw.gpio.get(pin) == 0, pin
        finally:
            fw.gpio.release(pin)


@pytest.mark.ad3
@pytest.mark.requires_option("i2c")
@pytest.mark.board_params("freq", "i2c.freqs.option")
def test_option_timing(fw, ad3, need, bench, i2c_cfg, loop, freq):
    """Standard and Fast mode with the external pull-ups: frequency within tolerance and the tLOW/tHIGH minima."""
    master, _, address = open_loop(fw, loop, freq=freq)
    scl, sda = need.dio(loop["master"]["scl"]), need.dio(loop["master"]["sda"])
    capture = arm_bus(ad3, scl, sda, freq, 60 / freq)
    assert fw.i2c.write(master, address, generate(4)).result == "complete"
    check_scl(capture.wait(timeout=2.0), scl, freq, i2c_cfg["tolerance"])


@pytest.mark.ad3
@pytest.mark.requires_option("i2c")
@pytest.mark.board_params("freq", "i2c.freqs.option")
def test_rise_time(fw, ad3, need, bench, i2c_cfg, loop, freq):
    """30-70 % rise time of SCL and SDA on the rows (needs a scope on the bus: WBA55 bundle2; the WB55 scopes stay on
    the ADC inputs, A.3), within `rise_ns` of the mode."""
    scopes = {line: need.optional_scope(loop["master"][line]) for line in ("scl", "sda")}
    if None in scopes.values():
        pytest.skip("no scope on the I2C rows in this wiring set")
    limit = i2c_cfg["rise_ns"]["sm" if freq <= 100_000 else "fm"] * 1e-9
    master, _, address = open_loop(fw, loop, freq=freq)
    rate = 100e6
    pending = arm_scope(ad3, list(scopes.values()), rate, 8192, scopes["scl"], VDD / 2)
    fw.i2c.write(master, address, len=64, pattern="prbs", seed=1)
    samples = pending.wait(timeout=2.0)
    for line, channel in scopes.items():
        rises = rise_times(samples[channel], rate, 0.3 * VDD, 0.7 * VDD)
        assert rises, f"no rising edge on {line}"
        assert max(rises) <= limit, f"{line}: {max(rises) * 1e9:.0f} ns rise (use 2.2 kOhm pull-ups)"


@pytest.mark.requires_option("i2c")
def test_target_answers(fw, loop):
    master, target, address = open_loop(fw, loop)
    assert fw.i2c.write(master, address).result == "complete"
    assert fw.i2c.hooks(master) == []
    assert fw.i2cs.status(target).writes == 1


@pytest.mark.requires_option("i2c")
@pytest.mark.board_params("position", "i2c.nack_positions")
def test_data_nack(fw, i2c_cfg, loop, position):
    _data_nack(fw, loop, i2c_cfg["nack_length"], position)


@pytest.mark.requires_option("i2c")
@pytest.mark.board_params("from_end", values=[1, 0])
def test_data_nack_at_the_end(fw, i2c_cfg, loop, from_end):
    """B.1c: a NACK of the next-to-last and of the last byte (the empty `onReceived` used to abort)."""
    length = i2c_cfg["nack_length"]
    _data_nack(fw, loop, length, length - from_end)


def _data_nack(fw, loop, length: int, position: int) -> None:
    """B.1a/c: the target NACKs byte `position`: `sent` counts the acknowledged bytes and no address NACK is
    reported."""
    master, target, address = open_loop(fw, loop, mode="sink")
    fw.i2cs.cfg(target, nack=position)
    reply = fw.i2c.write(master, address, len=length, pattern="prbs", seed=position)
    assert (reply.sent, reply.result) == (position - 1, "nack")
    assert "notfound" not in fw.i2c.hooks(master)
    status = fw.i2cs.status(target)
    assert (status.rx, status.nacked) == (position - 1, 1)
    fw.i2cs.cfg(target)
    assert fw.i2c.write(master, address, len=length).result == "complete"


@pytest.mark.requires_option("i2c")
def test_address_nack_after_other_direction(fw, i2c_cfg, loop):
    """B.1h: a read, then a write to an absent address (`sent=0`, `EVT notfound`); a write, then a read from an
    absent address (`nack`, `EVT notfound`)."""
    master, _, address = open_loop(fw, loop)
    absent = i2c_cfg["absent_addr"]
    assert fw.i2c.read(master, address, 4).result == "complete"
    reply = fw.i2c.write(master, absent, b"\x01")
    assert (reply.sent, reply.result) == (0, "nack")
    assert fw.i2c.hooks(master) == ["notfound"]
    assert fw.i2c.write(master, address, b"\x00\x01").result == "complete"
    assert fw.i2c.read(master, absent, 4).result == "nack"
    assert fw.i2c.hooks(master) == ["notfound"]


@pytest.mark.requires_option("i2c")
@pytest.mark.board_params("length", values=WRITE_READ_LENGTHS)
def test_write_read_regs(fw, loop, length):
    """Register file: the first written byte sets the pointer; a write and a restart read return the data. The write
    is the `inc` pattern from 0: pointer 0, then the bytes 1 to `length`."""
    master, target, address = open_loop(fw, loop)
    data = generate(length + 1)[1:]
    assert fw.i2c.write(master, address, len=length + 1).sent == length + 1
    assert fw.i2c.write(master, address, b"\x00", next="restart").result == "complete"
    assert fw.i2c.read(master, address, length).data == data
    assert fw.i2cs.dump(target, 0, length) == data


@pytest.mark.requires_option("i2c")
@pytest.mark.board_params("length", "i2c.payload_lengths")
def test_reload_lengths(fw, loop, length):
    """NBYTES/RELOAD: writes and reads of every length, checked by CRC at both ends."""
    master, target, address = open_loop(fw, loop, mode="sink")
    fw.i2cs.cfg(target, pattern="prbs", seed=length)
    reply = fw.i2c.write(master, address, len=length, pattern="prbs", seed=7)
    assert (reply.sent, reply.result) == (length, "complete")
    status = fw.i2cs.status(target)
    assert (status.rx, status.crc) == (length, crc_text(generate(length, "prbs", 7)))
    read = fw.i2c.read(master, address, length, out="crc")
    assert (read.result, read.length, read.crc) == ("complete", length, crc_text(generate(length, "prbs", length)))


@pytest.mark.requires_option("i2c")
def test_repeated_start(fw, loop):
    """`next=restart`: write the register, read it back after a repeated START, one STOP for both."""
    master, target, address = open_loop(fw, loop)
    fw.i2cs.regs(target, 0x20, b"\xde\xad\xbe\xef")
    fw.i2c.write(master, address, b"\x20", next="restart")
    assert fw.i2c.read(master, address, 4).data == b"\xde\xad\xbe\xef"
    status = fw.i2cs.status(target)
    assert (status.writes, status.reads, status.stops) == (1, 1, 1)


@pytest.mark.ad3
@pytest.mark.requires_option("i2c")
def test_repeated_start_on_bus(fw, ad3, need, bench, loop):
    master, target, address = open_loop(fw, loop, freq=100_000)
    scl, sda = need.dio(loop["master"]["scl"]), need.dio(loop["master"]["sda"])
    fw.i2cs.regs(target, 0x20, b"\x5a")
    capture = arm_bus(ad3, scl, sda, 100_000, 600e-6)
    fw.i2c.write(master, address, b"\x20", next="restart")
    fw.i2c.read(master, address, 1)
    samples = capture.wait(timeout=2.0)
    write, read = i2c_decode(samples.channel(scl), samples.channel(sda))[:2]
    assert (write.read, write.payload, write.stop) == (False, b"\x20", False)
    assert (read.restart, read.read, read.payload, read.stop) == (True, True, b"\x5a", True)


@pytest.mark.requires_option("i2c")
def test_continue_session(fw, loop):
    """`next=continue`: the next write continues the same transaction without a START or an address."""
    master, target, address = open_loop(fw, loop)
    assert fw.i2c.write(master, address, b"\x30\x01\x02", next="continue").result == "complete"
    assert fw.i2c.write(master, address, b"\x03\x04").result == "complete"
    status = fw.i2cs.status(target)
    assert (status.writes, status.stops, status.last) == (1, 1, b"\x30\x01\x02\x03\x04")
    assert fw.i2cs.dump(target, 0x30, 4) == b"\x01\x02\x03\x04"


@pytest.mark.requires_option("i2c")
def test_receive_continue_session(fw, loop):
    """B.1d: a read with `next=continue` completes (RELOAD set) and the next read continues the same transfer."""
    master, target, address = open_loop(fw, loop)
    fw.i2cs.regs(target, 0x40, bytes(range(8)))
    fw.i2c.write(master, address, b"\x40", next="restart")
    first = fw.i2c.read(master, address, 3, next="continue")
    second = fw.i2c.read(master, address, 5)
    assert (first.result, second.result) == ("complete", "complete")
    assert (first.data or b"") + (second.data or b"") == bytes(range(8))
    status = fw.i2cs.status(target)
    assert (status.reads, status.stops) == (1, 1)


def _stretched_transfer(fw, loop, direction: str, position: int, us: int) -> tuple[int, int, int]:
    master, target, address = open_loop(fw, loop, freq=100_000)
    fw.i2cs.regs(target, 0, generate(8))
    fw.i2cs.cfg(target, stretch=us, stretchat=position)
    if direction == "write":
        assert fw.i2c.write(master, address, bytes([0x80]) + generate(4)).result == "complete"
    else:
        fw.i2c.write(master, address, b"\x00", next="restart")
        assert fw.i2c.read(master, address, 4).data == generate(4)
    return master, target, address


@pytest.mark.requires_option("i2c")
@pytest.mark.board_params("us", "i2c.stretch_us")
@pytest.mark.board_params("direction,position", values=STRETCH_POSITIONS)
def test_clock_stretch_completes(fw, loop, us, direction, position):
    """The target holds SCL low for `us` at the address phase (position 0) or at a byte: the transfer completes."""
    _stretched_transfer(fw, loop, direction, position, us)


@pytest.mark.ad3
@pytest.mark.requires_option("i2c")
@pytest.mark.board_params("us", "i2c.stretch_us")
@pytest.mark.board_params("direction,position", values=STRETCH_POSITIONS)
def test_clock_stretch_on_bus(fw, ad3, need, bench, loop, us, direction, position):
    """The longest SCL low phase of the transfer is at least the stretch (5 % slack for the Stopwatch)."""
    scl = need.dio(loop["master"]["scl"])
    seconds = us * 1e-6 + 2e-3
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / seconds, 10e6)
    capture = ad3.logic.arm(rate, int(rate * seconds), trigger=(scl, "falling"), pretrigger=0.01)
    _stretched_transfer(fw, loop, direction, position, us)
    trace = capture.wait(timeout=2.0).channel(scl)
    edges = [index for index in range(1, len(trace)) if trace[index] != trace[index - 1]]
    lows = [(edges[i + 1] - edges[i]) / rate for i in range(len(edges) - 1) if not trace[edges[i]]]
    assert lows and max(lows) >= 0.95 * us * 1e-6, f"longest SCL low {max(lows, default=0) * 1e6:.0f} us"


@pytest.mark.requires_option("i2c")
def test_bus_error(fw, i2c_cfg, loop):
    """B.1e: the target turns byte 2 of a read into a misplaced STOP: `buserror` after `EVT hook=buserror`, then
    a good read on the same open instance."""
    master, target, address = open_loop(fw, loop, freq=i2c_cfg["fault_freq"], mode="sink")
    fw.i2cs.cfg(target, fault="stop", faultat=2)
    assert fw.i2c.read(master, address, 8).result == "buserror"
    assert "buserror" in fw.i2c.hooks(master)
    assert fw.i2c.read(master, address, 8).data == generate(8)


@pytest.mark.requires_option("i2c")
def test_general_call_does_not_hang(fw, loop):
    """B.1i: an idle I2cStm (the EEPROM adapter's, own address disabled) on the bus does not answer the general call,
    so the write is NACKed and the bus stays usable."""
    master, target = loop["master"], loop["target"]
    fw.eeprom.attach(target["index"], target["scl"], target["sda"])
    fw.i2c.open(master["index"], master["scl"], master["sda"])
    assert fw.i2c.write(master["index"], 0x00, b"\x00").result == "nack"
    assert fw.i2c.write(master["index"], 0x50).result == "complete"


@pytest.mark.requires_option("i2c")
def test_close_releases_held_bus(fw, request, loop):
    """`next=continue` leaves SCL held low by the master; `i2c.close` releases both lines (regression check, A.3)
    and a new open transfers normally."""
    master, _, address = open_loop(fw, loop)
    assert fw.i2c.write(master, address, b"\x00\x01", next="continue").result == "complete"
    fw.i2c.close(master)
    if not request.config.getoption("--fake"):
        for line in ("scl", "sda"):
            pin = loop["master"][line]
            fw.gpio.cfg(pin, "in")
            try:
                assert fw.gpio.get(pin) == 1, pin
            finally:
                fw.gpio.release(pin)
    fw.i2c.open(master, loop["master"]["scl"], loop["master"]["sda"])
    assert fw.i2c.write(master, address, b"\x00\x02").result == "complete"


@pytest.mark.requires_option("i2c")
def test_both_instances(fw, loop):
    """Each instance as master against the other as target."""
    for master, target in ((loop["master"], loop["target"]), (loop["target"], loop["master"])):
        fw.i2c.open(master["index"], master["scl"], master["sda"])
        address = fw.i2cs.open(target["index"], target["scl"], target["sda"], addr=loop["target"]["addr"])
        assert fw.i2c.write(master["index"], address, b"\x10\xa5").result == "complete"
        fw.i2c.write(master["index"], address, b"\x10", next="restart")
        assert fw.i2c.read(master["index"], address, 1).data == b"\xa5"
        fw.i2c.close(master["index"])
        fw.i2cs.close(target["index"])
