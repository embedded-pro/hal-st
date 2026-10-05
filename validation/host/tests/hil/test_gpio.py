"""GPIO (`hal::GpioPinStm`): levels, drive strengths, pulls, open drain, EXTI interrupts and timer-driven pulses.

Wiring set `bundle1`: the pins of `tests.gpio.loop_pins`/`output_pins` are pins of other peripherals, used here as
plain GPIO; `output_pins` is the user LED. An EXTI line serves one port at a time: `tests.gpio.exti_sharing` names
two wired pins with the same index on different ports.
"""

import statistics

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError


def check_output(fw, ad3, pin, dio, drive=None):
    fw.gpio.cfg(pin, "out", drive=drive)
    for level in (0, 1, 0, 1, 0):
        fw.gpio.set(pin, level)
        assert ad3.dio.read(dio) == level, f"{pin} set to {level}"


def check_input(fw, ad3, pin, dio, pull="none"):
    fw.gpio.cfg(pin, "in", pull=pull)
    for level in (1, 0, 1, 0):
        ad3.dio.drive(dio, level)
        assert fw.gpio.get(pin) == level, f"DIO{dio} driven {level}"
    ad3.dio.release(dio)


@pytest.mark.ad3
@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.board_params("drive", "gpio.drives")
def test_output_levels(fw, ad3, need, pin, drive):
    check_output(fw, ad3, pin, need.dio(pin), drive)


@pytest.mark.ad3
@pytest.mark.board_params("pin", "gpio.output_pins")
def test_output_pins(fw, ad3, need, pin):
    check_output(fw, ad3, pin, need.dio(pin))


@pytest.mark.ad3
@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.board_params("pull", "gpio.input_pulls")
def test_input_follows_ad3(fw, ad3, need, pin, pull):
    check_input(fw, ad3, pin, need.dio(pin), pull)


@pytest.mark.ad3
@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.board_params("pull", "gpio.pulls")
def test_pull_sets_idle_level(fw, ad3, need, pin, pull):
    dio = need.dio(pin)
    ad3.dio.release(dio)
    fw.gpio.cfg(pin, "in", pull=pull)
    expected = 1 if pull == "up" else 0
    assert fw.gpio.get(pin) == expected
    assert ad3.dio.read(dio) == expected


@pytest.mark.ad3
@pytest.mark.board_params("pin", "gpio.loop_pins")
def test_open_drain(fw, ad3, need, pin):
    dio = need.dio(pin)
    ad3.dio.release(dio)
    fw.gpio.cfg(pin, "od")
    fw.gpio.set(pin, 0)
    assert ad3.dio.read(dio) == 0, "open drain must pull low"
    fw.gpio.set(pin, 1)
    for level in (1, 0, 1):
        ad3.dio.drive(dio, level)
        assert fw.gpio.get(pin) == level, "released open-drain pin must follow the external level"
    ad3.dio.release(dio)


def test_configuration_errors(fw, board_cfg):
    pin = board_cfg.param("gpio.loop_pins")[0]
    for options, reason in (({"pull": "up"}, "usage"), ({"drive": "fastest"}, "usage")):
        with pytest.raises(FirmwareError) as error:
            fw.gpio.cfg(pin, "od", **options)
        assert error.value.reason == reason, options
    fw.gpio.cfg(pin, "od", pull="none")
    fw.gpio.cfg(pin, "out", drive="high")
    assert fw.gpio.get(pin) == 0, "out starts low"
    with pytest.raises(FirmwareError) as error:
        fw.command("gpio.set", fw.pin(pin), 2)
    assert error.value.reason == "usage"
    fw.gpio.cfg(pin, "in")
    with pytest.raises(FirmwareError) as error:
        fw.gpio.pulse(pin, 1, 1)
    assert error.value.reason == "usage", "gpio.pulse needs an output"
    fw.gpio.release(pin)
    for name, args in (("gpio.get", ()), ("gpio.set", (1,)), ("gpio.count", ()), ("gpio.irq", ("rising",)), ("gpio.release", ())):
        with pytest.raises(FirmwareError) as error:
            fw.command(name, fw.pin(pin), *args)
        assert error.value.reason == "notopen", name


def test_eight_pins_at_a_time(fw, board_cfg):
    """RAM limits the GPIO group to `tests.gpio.limit` pins; reconfiguring a pin needs no new entry."""
    pins = board_cfg.param("gpio.limit_pins")
    limit = board_cfg.param("gpio.limit")
    assert len(pins) > limit
    for pin in pins[:limit]:
        fw.gpio.cfg(pin, "in")
    with pytest.raises(FirmwareError) as error:
        fw.gpio.cfg(pins[limit], "in")
    assert error.value.reason == "busy"
    fw.gpio.cfg(pins[0], "in", pull="up")
    fw.gpio.release(pins[0])
    fw.gpio.cfg(pins[limit], "in")


def test_pins_held_by_other_groups(fw, board_cfg):
    instance = board_cfg.param("spi.instances")[0]
    fw.spi.open(instance["index"], clk=instance["clk"], mosi=instance["mosi"], miso=instance["miso"], cs=instance["cs"])
    for key in ("clk", "cs"):
        with pytest.raises(FirmwareError) as error:
            fw.gpio.cfg(instance[key], "in")
        assert error.value.reason == "busy", key
    fw.spi.close(instance["index"])
    fw.gpio.cfg(instance["cs"], "in")


def expected_edges(edge, pulses):
    return {"rising": pulses, "falling": pulses, "both": 2 * pulses}[edge]


@pytest.mark.ad3
@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.matrix("gpio.irq")
def test_interrupt_counts(fw, ad3, need, pin, edge, handler, pulses, frequency):
    dio = need.dio(pin)
    ad3.dio.release(dio)
    fw.gpio.cfg(pin, "in", pull="down")
    fw.gpio.irq(pin, edge, type=handler)
    fw.gpio.count(pin, clear=True)
    ad3.pattern.pulses(dio, pulses, frequency)
    ad3.pattern.wait_done(timeout=pulses / frequency + 2)
    fw.system.delay(10)
    assert fw.gpio.count(pin) == expected_edges(edge, pulses)
    fw.gpio.irq(pin, "off")
    ad3.pattern.pulses(dio, 3, frequency)
    ad3.pattern.wait_done(timeout=3 / frequency + 2)
    assert fw.gpio.count(pin) == expected_edges(edge, pulses), "counting must stop after irq off"


def test_exti_line_serves_one_port(fw, board_cfg):
    """While a pin counts edges on its EXTI line, the pin of another port with the same index is refused."""
    sharing = board_cfg.param("gpio.exti_sharing")
    counting, other = sharing["counting"], sharing["sharing"]
    fw.gpio.cfg(counting, "in", pull="down")
    fw.gpio.cfg(other, "in", pull="down")
    fw.gpio.irq(counting, "rising")
    fw.gpio.irq(counting, "both", type="immediate")
    for edge in ("rising", "off"):
        with pytest.raises(FirmwareError) as error:
            fw.gpio.irq(other, edge)
        assert error.value.reason == "unsupported", edge
    fw.gpio.irq(counting, "off")
    fw.gpio.irq(other, "falling")
    with pytest.raises(FirmwareError) as error:
        fw.gpio.irq(counting, "rising")
    assert error.value.reason == "unsupported"
    fw.gpio.release(other)
    fw.gpio.irq(counting, "rising")


@pytest.mark.ad3
def test_exti_owner_keeps_counting(fw, ad3, need, board_cfg):
    """The refused pin leaves the owner's interrupt alone, and edges on it are not counted."""
    sharing = board_cfg.param("gpio.exti_sharing")
    counting, other = sharing["counting"], sharing["sharing"]
    counting_dio, other_dio = need.dio(counting), need.dio(other)
    ad3.dio.release(counting_dio, other_dio)
    fw.gpio.cfg(counting, "in", pull="down")
    fw.gpio.cfg(other, "in", pull="down")
    fw.gpio.irq(counting, "rising")
    with pytest.raises(FirmwareError):
        fw.gpio.irq(other, "rising")
    fw.gpio.count(counting, clear=True)
    for dio, pulses in ((other_dio, 7), (counting_dio, 5)):
        ad3.pattern.pulses(dio, pulses, 1000)
        ad3.pattern.wait_done(timeout=pulses / 1000 + 2)
    fw.system.delay(10)
    assert fw.gpio.count(counting) == 5
    assert fw.gpio.count(other) == 0


@pytest.mark.ad3
@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.board_params("period_ms", "gpio.pulse.periods_ms")
def test_pulse_timing(fw, ad3, need, board_cfg, pin, period_ms):
    dio = need.dio(pin)
    count = board_cfg.param("gpio.pulse.count")
    tolerance = board_cfg.param("gpio.pulse.tolerance")
    jitter = board_cfg.param("gpio.pulse.jitter_ms", 0.5) / 1000
    fw.gpio.cfg(pin, "out")
    fw.gpio.set(pin, 0)
    duration = (count + 1) * period_ms / 1000
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / (duration * 1.2))
    capture = ad3.logic.arm(rate, int(duration * 1.2 * rate), trigger=(dio, "rising"), pretrigger=0.02)
    fw.gpio.pulse(pin, count, period_ms)
    bits = capture.wait(timeout=duration + 2).channel(dio)
    found = analysis.edges(bits)
    assert len(found) == count, f"{len(found)} toggles instead of {count}"
    intervals = [(b.index - a.index) / rate for a, b in zip(found, found[1:])]
    if intervals:
        assert statistics.fmean(intervals) == pytest.approx(period_ms / 1000, rel=tolerance)
        worst = max(abs(interval - period_ms / 1000) for interval in intervals)
        assert worst <= jitter, f"toggle interval off by {worst * 1000:.3f} ms"
