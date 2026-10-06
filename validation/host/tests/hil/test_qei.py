"""Quadrature encoder (`hal::SynchronousQuadratureEncoderStm`, LPTIM `SynchronousQuadratureEncoderLpTimStm`)
driven by the AD3 pattern generator.

Wiring set `bundle1`: A, B and index of each `tests.qei.instances` entry on DIOs. The LPTIM encoders
(`tests.qei.lp_instances`): NUCLEO-WB55RG LPTIM1 in `bundle2`; NUCLEO-WBA55CG LPTIM2 in `bundle1` and LPTIM1 in
`bundle2`. The pattern generator produces an exact number of 4-state cycles (A leads B for `fwd`); the LPTIM counts
both edges of both inputs (`cap=ab`) or the rising or falling edges only (`cap=rise|fall`, two counts per cycle).

Scenarios: features/qei.feature.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation import expect


@pytest.fixture
def qei(board_cfg):
    return board_cfg.param("qei")


def instance_dios(need, instance, keys=("a", "b", "idx")):
    return {key: need.dio(instance[key]) for key in keys}


def open_qei(fw, qei, instance, **options):
    options.setdefault("res", qei["resolution"])
    fw.qei.open(instance["index"], a=instance["a"], b=instance["b"], idx=instance.get("idx"), **options)


def run(ad3, dios, frequency, cycles, direction="fwd"):
    ad3.pattern.quadrature(dios["a"], dios["b"], frequency, cycles, direction)
    ad3.pattern.wait_done(timeout=cycles / frequency + 2)


def run_inverted(ad3, dios, frequency, cycles, invert_a, invert_b, direction="fwd"):
    """Quadrature with physically inverted phases, rotated so the run ends in the idle (low, low) state."""
    pattern = analysis.quadrature_pattern(1, direction)
    states = [(a ^ invert_a, b ^ invert_b) for a, b in zip(pattern["a"], pattern["b"])]
    while states[-1] != (0, 0):
        states = states[1:] + states[:1]
    ad3.pattern.custom({dios["a"]: [a for a, _ in states], dios["b"]: [b for _, b in states]}, frequency * 4, run_samples=4 * cycles)
    ad3.pattern.wait_done(timeout=cycles / frequency + 2)


def lp_counts(qei, cycles, cap, direction, inva):
    """Signed count change of the LPTIM encoder: `counts_per_cycle[cap]` per cycle, reversed by `inva`."""
    sign = 1 if expect.qei_counts(1, "ab", direction, inva) > 0 else -1
    return qei["counts_per_cycle"][cap] * cycles * sign


def check_counts(fw, qei, index, before, counts, resolution=None):
    resolution = resolution or qei["resolution"]
    after = fw.qei.read(index)
    assert expect.wrap_delta(after.pos - before, resolution) == expect.wrap_delta(counts, resolution)
    assert after.dir == ("fwd" if counts > 0 else "rev")


@pytest.mark.board_params("instance", "qei.instances")
@pytest.mark.matrix("qei.position")
@scenario("qei.feature", "The count follows the capture mode, and inverting exactly one phase reverses it")
def test_position_counts(instance, freq, cycles, direction, cap, inva, invb):
    pass


@pytest.mark.board_params("instance", "qei.instances")
@pytest.mark.board_params("inva,invb", "qei.inversions")
@scenario("qei.feature", "Inverted inputs restore physically inverted phases")
def test_inverted_inputs_restore_the_signal(instance, inva, invb):
    pass


@pytest.mark.board_params("instance", "qei.instances")
@pytest.mark.matrix("qei.rollover")
@scenario("qei.feature", "The position starts at the offset, rolls over at the resolution and comes back")
def test_offset_and_rollover(instance, res, offset):
    pass


@pytest.mark.board_params("instance", "qei.instances")
@pytest.mark.matrix("qei.velocity")
@scenario("qei.feature", "The speed is in counts per second, sampled every velocity period")
def test_speed(instance, freq, vel):
    pass


@pytest.mark.board_params("instance", "qei.instances")
@scenario("qei.feature", "qei.index reads the level of the index input, which never changes the count")
def test_index_input(instance):
    pass


@pytest.mark.board_params("instance", "qei.lp_instances")
@pytest.mark.matrix("qei.lp_position")
@scenario("qei.feature", "The LPTIM encoder counts the edges of the capture mode, reversed by inva")
def test_lptim_position(instance, freq, cycles, direction, inva, filter, cap):
    pass


@scenario("qei.feature", "The default encoder opens on its default pins")
def test_default_instance():
    pass


@pytest.mark.board_params("instance", "qei.instances")
@scenario("qei.feature", "An encoder other than the default needs its pins")
def test_instance_needs_pins(instance):
    pass


@pytest.mark.board_params("instance", "qei.instances")
@scenario("qei.feature", "The resolution is 2 to the maximum and the offset stays below it")
def test_resolution_limits(instance):
    pass


@pytest.mark.board_params("instance", "qei.instances")
@scenario("qei.feature", "Malformed and out-of-range opens are refused, and the index input needs an open encoder with idx")
def test_open_errors(instance):
    pass


@pytest.mark.board_params("instance", "qei.lp_instances")
@scenario("qei.feature", "Every LPTIM capture mode opens and reads back the resolution")
def test_lptim_capture_modes(instance):
    pass


@pytest.mark.board_params("instance", "qei.lp_instances")
@scenario("qei.feature", "Malformed and unsupported LPTIM opens are refused")
def test_lptim_errors(instance):
    pass


@scenario("qei.feature", "One encoder is open at a time")
def test_one_encoder_at_a_time():
    pass


@scenario("qei.feature", "A timer that counts an encoder is busy for PWM and the ADC trigger")
def test_timer_shared_with_pwm_and_adc():
    pass


@given("the A, B and index inputs of the encoder are wired to DIOs", target_fixture="dios")
def encoder_wired(need, instance):
    return instance_dios(need, instance)


@given("the A and B inputs of the encoder are wired to DIOs", target_fixture="dios")
def lp_encoder_wired(need, instance):
    return instance_dios(need, instance, ("a", "b"))


@given("the encoder is not the default encoder, which has default pins")
def not_default(qei, instance):
    if instance["index"] == qei["default_instance"]:
        pytest.skip("the default encoder has default pins")


@given("the first encoder of the board file on a timer that can trigger the ADC", target_fixture="encoder")
def trigger_encoder(qei):
    return next(instance for instance in qei["instances"] if instance["index"] in expect.ADC_TRIGGER_TIMERS)


@when("the encoder is opened with the capture mode and the inversions")
def open_with_capture(fw, qei, instance, cap, inva, invb):
    open_qei(fw, qei, instance, cap=cap, inva=inva, invb=invb)


@when("the encoder is opened with the inversions")
def open_with_inversions(fw, qei, instance, inva, invb):
    open_qei(fw, qei, instance, inva=inva, invb=invb)


@when("the encoder is opened with the resolution and the offset")
def open_with_offset(fw, qei, instance, res, offset):
    open_qei(fw, qei, instance, res=res, offset=offset)


@when(parsers.parse('the encoder is opened with the velocity period and capture mode "{mode}"'))
def open_with_velocity(fw, qei, instance, vel, mode):
    open_qei(fw, qei, instance, vel=vel, cap=mode)


@when("the encoder is opened")
def open_plain(fw, qei, instance):
    open_qei(fw, qei, instance)


@when("the LPTIM encoder is opened at its maximum resolution with the inversion, the filter and the capture mode")
def open_lptim(fw, instance, inva, filter, cap):
    fw.qei.open(instance["index"], lp=True, a=instance["a"], b=instance["b"], res=instance["max_res"], inva=inva, filter=filter, cap=cap)


@when("the position of the encoder is read", target_fixture="before")
def position_read(fw, instance):
    return fw.qei.read(instance["index"]).pos


@when("the AD3 runs the cycles at the frequency in the direction")
def run_cycles(ad3, dios, freq, cycles, direction):
    run(ad3, dios, freq, cycles, direction)


@when(parsers.parse("the AD3 runs {cycle_count:d} cycles at {hz:d} Hz with the phases physically inverted as the inversions"))
def run_inverted_cycles(ad3, dios, inva, invb, cycle_count, hz):
    run_inverted(ad3, dios, hz, cycle_count, inva, invb)


@when(parsers.parse('the AD3 runs {cycle_count:d} cycles at {hz:d} Hz in direction "{way}"'))
def run_way(ad3, dios, cycle_count, hz, way):
    run(ad3, dios, hz, cycle_count, way)


@when(
    parsers.parse(
        "the speed is read {settle_ms:d} ms after three velocity periods of an endless quadrature at the frequency, which then stops"
    ),
    target_fixture="speed",
)
def speed_read(fw, ad3, dios, instance, freq, vel, settle_ms):
    ad3.pattern.quadrature(dios["a"], dios["b"], freq, 0)
    try:
        time.sleep(3 * vel / 1e6 + settle_ms / 1000)
        speed = fw.qei.read(instance["index"]).speed
    finally:
        ad3.pattern.stop()
    return speed


@when("the default encoder is opened without pins")
def open_default(fw, qei):
    fw.qei.open(qei["default_instance"])


@when("the default encoder is closed")
def close_default(fw, qei):
    fw.qei.close(qei["default_instance"])


@when("the encoder is opened at its maximum resolution with the offset one below it")
def open_at_maximum(fw, instance):
    maximum = instance["max_res"]
    fw.qei.open(instance["index"], a=instance["a"], b=instance["b"], res=maximum, offset=maximum - 1)


@when(parsers.parse('the encoder is opened without index input, the velocity "{velocity}" and filter {filter_value:d}'))
def open_without_index(fw, instance, velocity, filter_value):
    fw.qei.open(instance["index"], a=instance["a"], b=instance["b"], vel=velocity, filter=filter_value)


@when("the first encoder of the board file is opened")
def open_first(fw, qei):
    first, _ = qei["instances"][:2]
    open_qei(fw, qei, first)


@when("that encoder is opened")
def open_encoder(fw, qei, encoder):
    open_qei(fw, qei, encoder)


@then("the position moved by the counts of the cycles for the capture mode, direction and inversions, and the direction reads accordingly")
def moved_by_counts(fw, qei, instance, before, cycles, cap, direction, inva, invb):
    check_counts(fw, qei, instance["index"], before, expect.qei_counts(cycles, cap, direction, inva, invb))


@then(parsers.parse("the position moved forward by both edges of both inputs of the {cycle_count:d} cycles"))
def moved_forward(fw, qei, instance, before, cycle_count):
    check_counts(fw, qei, instance["index"], before, qei["counts_per_cycle"]["ab"] * cycle_count)


@then(
    "the position moved by the LPTIM counts of the cycles for the capture mode, the direction and the inversion, wrapped at the maximum "
    "resolution, and the direction reads accordingly"
)
def moved_by_lp_counts(fw, qei, instance, before, cycles, cap, direction, inva):
    check_counts(fw, qei, instance["index"], before, lp_counts(qei, cycles, cap, direction, inva), instance["max_res"])


@then("the encoder reads the offset as position and the resolution")
def reads_offset(fw, instance, res, offset):
    reading = fw.qei.read(instance["index"])
    assert (reading.pos, reading.res) == (offset, res)


@then(
    parsers.parse("the position is the offset plus {per_cycle:d} counts for each of the {cycle_count:d} cycles, wrapped at the resolution")
)
def position_wrapped(fw, instance, res, offset, per_cycle, cycle_count):
    assert fw.qei.read(instance["index"]).pos == expect.wrap_position(offset + per_cycle * cycle_count, res)


@then("the position is the offset")
def position_at_offset(fw, instance, offset):
    assert fw.qei.read(instance["index"]).pos == offset


@then(
    "the speed is the counts per cycle of both edges of both inputs times the frequency, within the speed tolerance or one count per "
    "velocity period"
)
def speed_matches(qei, freq, vel, speed):
    expected = qei["counts_per_cycle"]["ab"] * freq
    assert speed == pytest.approx(expected, rel=qei["speed_tolerance"], abs=1e6 / vel)


@then("qei.index reads every level the DIO on the index input drives, and the DIO is released")
def index_follows(fw, ad3, dios, instance):
    for level in (1, 0, 1, 0):
        ad3.dio.drive(dios["idx"], level)
        assert fw.qei.index(instance["index"]) == level
    ad3.dio.release(dios["idx"])


@then("the position is unchanged")
def position_unchanged(fw, instance, before):
    assert fw.qei.read(instance["index"]).pos == before


@then(parsers.parse("it reads the resolution and speed {stopped:d}"))
def default_reading(fw, qei, stopped):
    reading = fw.qei.read(qei["default_instance"])
    assert (reading.res, reading.speed) == (qei["resolution"], stopped)


@then(parsers.parse("its index input reads {low:d} or {high:d}"))
def default_index(fw, qei, low, high):
    assert fw.qei.index(qei["default_instance"]) in (low, high)


@then(parsers.parse('reading it fails with "{reason}"'))
def default_read_refused(fw, qei, reason):
    with pytest.raises(FirmwareError) as error:
        fw.qei.read(qei["default_instance"])
    assert error.value.reason == reason


@then(parsers.parse('opening the encoder without pins fails with "{reason}"'))
def open_without_pins_refused(fw, instance, reason):
    with pytest.raises(FirmwareError) as error:
        fw.qei.open(instance["index"])
    assert error.value.reason == reason


@then(parsers.parse('opening the encoder with only its A pin fails with "{reason}"'))
def open_with_a_refused(fw, instance, reason):
    with pytest.raises(FirmwareError) as error:
        fw.qei.open(instance["index"], a=instance["a"])
    assert error.value.reason == reason


@then("opening the encoder with resolution 1, one beyond its maximum or an offset not below the resolution fails with its reason")
def resolution_refused(fw, instance):
    maximum, index = instance["max_res"], instance["index"]
    pins = {"a": instance["a"], "b": instance["b"]}
    beyond = "usage" if maximum + 1 > 0xFFFFFFFF else "range"
    for options, reason in (({"res": 1}, "range"), ({"res": maximum + 1}, beyond), ({"res": 100, "offset": 100}, "range")):
        with pytest.raises(FirmwareError) as error:
            fw.qei.open(index, **pins, **options)
        assert error.value.reason == reason, options


@then("the encoder reads that offset as position and its maximum resolution")
def reads_maximum(fw, instance):
    maximum = instance["max_res"]
    reading = fw.qei.read(instance["index"])
    assert (reading.pos, reading.res) == (maximum - 1, maximum)


@then("every malformed or out-of-range open of the encoder fails with its reason")
def open_errors(fw, instance):
    pins = {"a": instance["a"], "b": instance["b"]}
    cases = [
        ({**pins, "cap": "x"}, "usage"),
        ({**pins, "cap": "rise"}, "usage"),
        ({**pins, "cap": "fall"}, "usage"),
        ({**pins, "filter": 16}, "range"),
        ({**pins, "vel": 0}, "range"),
        ({**pins, "vel": 1000001}, "range"),
        ({**pins, "inva": 2}, "range"),
        ({"a": instance["b"], "b": instance["a"]}, "pin"),
    ]
    for options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.qei.open(instance["index"], **options)
        assert error.value.reason == reason, options


@then(parsers.parse('reading the index input of the encoder fails with "{reason}"'))
def index_refused(fw, instance, reason):
    with pytest.raises(FirmwareError) as error:
        fw.qei.index(instance["index"])
    assert error.value.reason == reason


@then(parsers.parse('reading the index input of the encoder opened without idx fails with "{reason}"'))
def index_without_idx_refused(fw, instance, reason):
    with pytest.raises(FirmwareError) as error:
        fw.qei.index(instance["index"])
    assert error.value.reason == reason, "opened without idx"


@then("the LPTIM encoder opens with every capture mode of the LPTIM position matrix, reads the resolution and closes")
def lptim_capture_modes(fw, qei, instance):
    for cap in qei["lp_position"]["cap"]:
        fw.qei.open(instance["index"], lp=True, a=instance["a"], b=instance["b"], cap=cap)
        assert fw.qei.read(instance["index"]).res == qei["resolution"], cap
        fw.qei.close(instance["index"])


@then("every malformed, out-of-range or unsupported open of the LPTIM encoder fails with its reason")
def lptim_errors(fw, instance):
    pins = {"lp": True, "a": instance["a"], "b": instance["b"]}
    cases = [
        ({**pins, "filter": 3}, "range"),
        ({**pins, "res": instance["max_res"] + 1}, "range"),
        ({**pins, "cap": "x"}, "usage"),
        ({**pins, "cap": "a"}, "unsupported"),
        ({**pins, "cap": "b"}, "unsupported"),
        ({**pins, "offset": 0}, "unsupported"),
        ({**pins, "invb": 0}, "unsupported"),
        ({"lp": True}, "usage"),
        ({"lp": True, "a": instance["b"], "b": instance["a"]}, "pin"),
    ]
    for options, reason in cases:
        with pytest.raises(FirmwareError) as error:
            fw.qei.open(instance["index"], **options)
        assert error.value.reason == reason, options


@then(parsers.parse('opening every missing LPTIM instance fails with "{reason}"'))
def lptim_missing_refused(fw, qei, instance, reason):
    pins = {"lp": True, "a": instance["a"], "b": instance["b"]}
    for index in qei["lp_missing_instances"]:
        with pytest.raises(FirmwareError) as error:
            fw.qei.open(index, **pins)
        assert error.value.reason == reason, f"LPTIM{index}"


@then(parsers.parse('opening the second encoder fails with "{reason}"'))
def second_refused(fw, qei, reason):
    _, second = qei["instances"][:2]
    with pytest.raises(FirmwareError) as error:
        open_qei(fw, qei, second)
    assert error.value.reason == reason


@then(parsers.parse('opening PWM on its timer with channel {channel:d} fails with "{reason}"'))
def pwm_refused(fw, encoder, channel, reason):
    with pytest.raises(FirmwareError) as error:
        fw.pwm.open(encoder["index"], channels=[channel])
    assert error.value.reason == reason, "PWM"


@then(parsers.parse('opening the ADC on its first input, triggered by that timer, fails with "{reason}"'))
def adc_refused(fw, board_cfg, encoder, reason):
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(board_cfg.param("adc.adc"), pins=[board_cfg.param("adc.inputs")[0]], timer=encoder["index"])
    assert error.value.reason == reason, "timer-triggered ADC"
