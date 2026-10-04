"""Quadrature encoder (`hal::SynchronousQuadratureEncoderStm`, LPTIM `SynchronousQuadratureEncoderLpTimStm`)
driven by the AD3 pattern generator.

Wiring set `bundle1`: A, B and index of each `tests.qei.instances` entry on DIOs. The LPTIM encoders
(`tests.qei.lp_instances`): NUCLEO-WB55RG LPTIM1 in `bundle2`; NUCLEO-WBA55CG LPTIM2 in `bundle1` and LPTIM1 in
`bundle2`. The pattern generator produces an exact number of 4-state cycles (A leads B for `fwd`); the LPTIM counts
both edges of both inputs (`cap=ab`) or the rising or falling edges only (`cap=rise|fall`, two counts per cycle).
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError

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


@pytest.mark.ad3
@pytest.mark.board_params("instance", "qei.instances")
@pytest.mark.matrix("qei.position")
def test_position_counts(fw, ad3, need, qei, instance, freq, cycles, direction, cap, inva, invb):
    """Counts per cycle follow the capture mode; inverting exactly one phase reverses the direction."""
    dios = instance_dios(need, instance)
    open_qei(fw, qei, instance, cap=cap, inva=inva, invb=invb)
    before = fw.qei.read(instance["index"]).pos
    run(ad3, dios, freq, cycles, direction)
    check_counts(fw, qei, instance["index"], before, expect.qei_counts(cycles, cap, direction, inva, invb))


@pytest.mark.ad3
@pytest.mark.board_params("instance", "qei.instances")
@pytest.mark.board_params("inva,invb", "qei.inversions")
def test_inverted_inputs_restore_the_signal(fw, ad3, need, qei, instance, inva, invb):
    """Physically inverted phases with the matching `inva`/`invb` count like the plain signal."""
    dios = instance_dios(need, instance)
    open_qei(fw, qei, instance, inva=inva, invb=invb)
    before = fw.qei.read(instance["index"]).pos
    cycles = 25
    run_inverted(ad3, dios, 1000, cycles, inva, invb)
    check_counts(fw, qei, instance["index"], before, qei["counts_per_cycle"]["ab"] * cycles)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "qei.instances")
@pytest.mark.matrix("qei.rollover")
def test_offset_and_rollover(fw, ad3, need, qei, instance, res, offset):
    dios = instance_dios(need, instance)
    open_qei(fw, qei, instance, res=res, offset=offset)
    reading = fw.qei.read(instance["index"])
    assert (reading.pos, reading.res) == (offset, res)
    cycles = 30
    run(ad3, dios, 1000, cycles)
    assert fw.qei.read(instance["index"]).pos == expect.wrap_position(offset + 4 * cycles, res)
    run(ad3, dios, 1000, cycles, "rev")
    assert fw.qei.read(instance["index"]).pos == offset


@pytest.mark.ad3
@pytest.mark.board_params("instance", "qei.instances")
@pytest.mark.matrix("qei.velocity")
def test_speed(fw, ad3, need, qei, instance, freq, vel):
    """`speed` is in counts per second, sampled every `vel` µs."""
    dios = instance_dios(need, instance)
    open_qei(fw, qei, instance, vel=vel, cap="ab")
    ad3.pattern.quadrature(dios["a"], dios["b"], freq, 0)
    try:
        time.sleep(3 * vel / 1e6 + 0.05)
        speed = fw.qei.read(instance["index"]).speed
    finally:
        ad3.pattern.stop()
    expected = qei["counts_per_cycle"]["ab"] * freq
    assert speed == pytest.approx(expected, rel=qei["speed_tolerance"], abs=1e6 / vel)


@pytest.mark.ad3
@pytest.mark.board_params("instance", "qei.instances")
def test_index_input(fw, ad3, need, qei, instance):
    """`qei.index` reads the level of the index input, which never changes the count."""
    dios = instance_dios(need, instance)
    open_qei(fw, qei, instance)
    position = fw.qei.read(instance["index"]).pos
    for level in (1, 0, 1, 0):
        ad3.dio.drive(dios["idx"], level)
        assert fw.qei.index(instance["index"]) == level
    ad3.dio.release(dios["idx"])
    assert fw.qei.read(instance["index"]).pos == position


@pytest.mark.ad3
@pytest.mark.board_params("instance", "qei.lp_instances")
@pytest.mark.matrix("qei.lp_position")
def test_lptim_position(fw, ad3, need, qei, instance, freq, cycles, direction, inva, filter, cap):
    """The LPTIM encoder (`lp=1`) counts both edges of both inputs (`ab`) or the rising or falling edges only;
    `inva=1` reverses the direction (mirrored mounting)."""
    dios = instance_dios(need, instance, ("a", "b"))
    resolution = instance["max_res"]
    fw.qei.open(instance["index"], lp=True, a=instance["a"], b=instance["b"], res=resolution, inva=inva, filter=filter, cap=cap)
    before = fw.qei.read(instance["index"]).pos
    run(ad3, dios, freq, cycles, direction)
    check_counts(fw, qei, instance["index"], before, lp_counts(qei, cycles, cap, direction, inva), resolution)


def test_default_instance(fw, qei):
    """The default encoder opens on its default pins (with the index input) when no pin is given."""
    index = qei["default_instance"]
    fw.qei.open(index)
    reading = fw.qei.read(index)
    assert (reading.res, reading.speed) == (qei["resolution"], 0)
    assert fw.qei.index(index) in (0, 1)
    fw.qei.close(index)
    with pytest.raises(FirmwareError) as error:
        fw.qei.read(index)
    assert error.value.reason == "notopen"


@pytest.mark.board_params("instance", "qei.instances")
def test_instance_needs_pins(fw, qei, instance):
    if instance["index"] == qei["default_instance"]:
        pytest.skip("the default encoder has default pins")
    with pytest.raises(FirmwareError) as error:
        fw.qei.open(instance["index"])
    assert error.value.reason == "usage"
    with pytest.raises(FirmwareError) as error:
        fw.qei.open(instance["index"], a=instance["a"])
    assert error.value.reason == "usage"


@pytest.mark.board_params("instance", "qei.instances")
def test_resolution_limits(fw, instance):
    """`res` is 2 to 65536, up to 4294967295 on the 32-bit TIM2; `offset` must stay below it."""
    maximum, index = instance["max_res"], instance["index"]
    pins = {"a": instance["a"], "b": instance["b"]}
    beyond = "usage" if maximum + 1 > 0xFFFFFFFF else "range"
    for options, reason in (({"res": 1}, "range"), ({"res": maximum + 1}, beyond), ({"res": 100, "offset": 100}, "range")):
        with pytest.raises(FirmwareError) as error:
            fw.qei.open(index, **pins, **options)
        assert error.value.reason == reason, options
    fw.qei.open(index, **pins, res=maximum, offset=maximum - 1)
    reading = fw.qei.read(index)
    assert (reading.pos, reading.res) == (maximum - 1, maximum)


@pytest.mark.board_params("instance", "qei.instances")
def test_open_errors(fw, instance):
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
    with pytest.raises(FirmwareError) as error:
        fw.qei.index(instance["index"])
    assert error.value.reason == "notopen"
    fw.qei.open(instance["index"], **pins, vel="off", filter=15)
    with pytest.raises(FirmwareError) as error:
        fw.qei.index(instance["index"])
    assert error.value.reason == "unsupported", "opened without idx"


@pytest.mark.board_params("instance", "qei.lp_instances")
def test_lptim_capture_modes(fw, qei, instance):
    """Every LPTIM capture mode opens and reads back the resolution."""
    for cap in qei["lp_position"]["cap"]:
        fw.qei.open(instance["index"], lp=True, a=instance["a"], b=instance["b"], cap=cap)
        assert fw.qei.read(instance["index"]).res == qei["resolution"], cap
        fw.qei.close(instance["index"])


@pytest.mark.board_params("instance", "qei.lp_instances")
def test_lptim_errors(fw, qei, instance):
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
    for index in qei["lp_missing_instances"]:
        with pytest.raises(FirmwareError) as error:
            fw.qei.open(index, **pins)
        assert error.value.reason == "range", f"LPTIM{index}"


def test_one_encoder_at_a_time(fw, qei):
    first, second = qei["instances"][:2]
    open_qei(fw, qei, first)
    with pytest.raises(FirmwareError) as error:
        open_qei(fw, qei, second)
    assert error.value.reason == "busy"


def test_timer_shared_with_pwm_and_adc(fw, board_cfg, qei):
    """A timer serves one group at a time: while it counts an encoder, PWM and the ADC trigger get `ERR busy`."""
    instance = next(instance for instance in qei["instances"] if instance["index"] in expect.ADC_TRIGGER_TIMERS)
    timer = instance["index"]
    open_qei(fw, qei, instance)
    with pytest.raises(FirmwareError) as error:
        fw.pwm.open(timer, channels=[3])
    assert error.value.reason == "busy", "PWM"
    with pytest.raises(FirmwareError) as error:
        fw.adc.open(board_cfg.param("adc.adc"), pins=[board_cfg.param("adc.inputs")[0]], timer=timer)
    assert error.value.reason == "busy", "timer-triggered ADC"
