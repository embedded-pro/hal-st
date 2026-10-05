"""GPIO (`hal::GpioPinStm`): levels, drive strengths, pulls, open drain, EXTI interrupts and timer-driven pulses.

Wiring set `bundle1`: the pins of `tests.gpio.loop_pins`/`output_pins` are pins of other peripherals, used here as
plain GPIO; `output_pins` is the user LED. An EXTI line serves one port at a time: `tests.gpio.exti_sharing` names
two wired pins with the same index on different ports.

Scenarios: features/gpio.feature.
"""

import statistics

import pytest
from ad3_waveforms_bench import analysis
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when


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


def expected_edges(edge, pulses):
    return {"rising": pulses, "falling": pulses, "both": 2 * pulses}[edge]


@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.board_params("drive", "gpio.drives")
@scenario("gpio.feature", "The pin drives the DIO with every drive strength")
def test_output_levels(pin, drive):
    pass


@pytest.mark.board_params("pin", "gpio.output_pins")
@scenario("gpio.feature", "The output pins drive the DIO")
def test_output_pins(pin):
    pass


@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.board_params("pull", "gpio.input_pulls")
@scenario("gpio.feature", "The pin reads the level the DIO drives with every pull")
def test_input_follows_ad3(pin, pull):
    pass


@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.board_params("pull", "gpio.pulls")
@scenario("gpio.feature", "The pull sets the idle level")
def test_pull_sets_idle_level(pin, pull):
    pass


@pytest.mark.board_params("pin", "gpio.loop_pins")
@scenario("gpio.feature", "An open-drain pin pulls low and, released, follows the external level")
def test_open_drain(pin):
    pass


@scenario("gpio.feature", "Wrong configurations and commands on unopened pins are refused")
def test_configuration_errors():
    pass


@scenario("gpio.feature", "The group holds a limited number of pins")
def test_eight_pins_at_a_time():
    pass


@scenario("gpio.feature", "Pins held by other groups are refused")
def test_pins_held_by_other_groups():
    pass


@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.matrix("gpio.irq")
@scenario("gpio.feature", "The interrupt counts the edges of the pulses and stops counting when turned off")
def test_interrupt_counts(pin, edge, handler, pulses, frequency):
    pass


@scenario("gpio.feature", "An EXTI line serves one port at a time")
def test_exti_line_serves_one_port():
    pass


@scenario("gpio.feature", "The owner of an EXTI line keeps counting")
def test_exti_owner_keeps_counting():
    pass


@pytest.mark.board_params("pin", "gpio.loop_pins")
@pytest.mark.board_params("period_ms", "gpio.pulse.periods_ms")
@scenario("gpio.feature", "Timer-driven pulses toggle the pin at the period")
def test_pulse_timing(pin, period_ms):
    pass


@given("the pin is wired to a DIO", target_fixture="dio")
def pin_wired(need, pin):
    return need.dio(pin)


@given("the DIO is released")
def dio_released(ad3, dio):
    ad3.dio.release(dio)


@given(parsers.parse('the pin is configured as an input with pull "{fixed_pull}"'))
def input_with_fixed_pull(fw, pin, fixed_pull):
    fw.gpio.cfg(pin, "in", pull=fixed_pull)


@given("the interrupt on the edge is enabled with the handler")
def interrupt_enabled(fw, pin, edge, handler):
    fw.gpio.irq(pin, edge, type=handler)


@given("the edge count of the pin is cleared")
def count_cleared(fw, pin):
    fw.gpio.count(pin, clear=True)


@given("the first of the loop pins", target_fixture="loop_pin")
def first_loop_pin(board_cfg):
    return board_cfg.param("gpio.loop_pins")[0]


@given("the limit pins of the board file, more than the limit", target_fixture="limit_pins")
def limit_pins(board_cfg):
    pins = board_cfg.param("gpio.limit_pins")
    limit = board_cfg.param("gpio.limit")
    assert len(pins) > limit
    return pins, limit


@given("the first SPI instance of the board file is opened", target_fixture="spi_instance")
def spi_opened(fw, board_cfg):
    instance = board_cfg.param("spi.instances")[0]
    fw.spi.open(instance["index"], clk=instance["clk"], mosi=instance["mosi"], miso=instance["miso"], cs=instance["cs"])
    return instance


@given("the counting pin and the sharing pin of the EXTI sharing of the board file", target_fixture="exti_pins")
def exti_sharing(board_cfg):
    sharing = board_cfg.param("gpio.exti_sharing")
    counting, other = sharing["counting"], sharing["sharing"]
    return {"counting": counting, "sharing": other}


@given("the counting pin and the sharing pin are wired to DIOs", target_fixture="exti_dios")
def exti_pins_wired(need, exti_pins):
    counting_dio, other_dio = need.dio(exti_pins["counting"]), need.dio(exti_pins["sharing"])
    return {"counting": counting_dio, "sharing": other_dio}


@given("both DIOs are released")
def both_dios_released(ad3, exti_dios):
    ad3.dio.release(exti_dios["counting"], exti_dios["sharing"])


@given(
    parsers.parse("the pulse count, tolerance and jitter of the board file, the jitter {default_ms:g} ms unless set"),
    target_fixture="pulse_spec",
)
def pulse_parameters(board_cfg, default_ms):
    return {
        "count": board_cfg.param("gpio.pulse.count"),
        "tolerance": board_cfg.param("gpio.pulse.tolerance"),
        "jitter": board_cfg.param("gpio.pulse.jitter_ms", default_ms) / 1000,
    }


@given("the pin is configured as an output")
def output_configured(fw, pin):
    fw.gpio.cfg(pin, "out")


@given(parsers.parse("the pin is set to {level:d}"))
@when(parsers.parse("the pin is set to {level:d}"))
def pin_set(fw, pin, level):
    fw.gpio.set(pin, level)


@when("the pin is configured as an input with the pull")
def input_with_pull(fw, pin, pull):
    fw.gpio.cfg(pin, "in", pull=pull)


@when("the pin is configured as open drain")
def open_drain_configured(fw, pin):
    fw.gpio.cfg(pin, "od")


@when(parsers.parse('the loop pin is configured as open drain with pull "{od_pull}"'))
def loop_pin_open_drain(fw, loop_pin, od_pull):
    fw.gpio.cfg(loop_pin, "od", pull=od_pull)


@when(parsers.parse('the loop pin is configured as an output with drive "{out_drive}"'))
def loop_pin_output(fw, loop_pin, out_drive):
    fw.gpio.cfg(loop_pin, "out", drive=out_drive)


@when("the loop pin is configured as an input")
def loop_pin_input(fw, loop_pin):
    fw.gpio.cfg(loop_pin, "in")


@when("the loop pin is released")
def loop_pin_released(fw, loop_pin):
    fw.gpio.release(loop_pin)


@when("as many limit pins as the limit are configured as inputs")
def limit_inputs(fw, limit_pins):
    pins, limit = limit_pins
    for pin in pins[:limit]:
        fw.gpio.cfg(pin, "in")


@when(parsers.parse('the first limit pin is configured as an input with pull "{limit_pull}"'))
def first_limit_pin_pulled(fw, limit_pins, limit_pull):
    pins, _ = limit_pins
    fw.gpio.cfg(pins[0], "in", pull=limit_pull)


@when("the first limit pin is released")
def first_limit_pin_released(fw, limit_pins):
    pins, _ = limit_pins
    fw.gpio.release(pins[0])


@when("the SPI instance is closed")
def spi_closed(fw, spi_instance):
    fw.spi.close(spi_instance["index"])


@when(parsers.parse('the counting pin and the sharing pin are configured as inputs with pull "{exti_pull}"'))
def exti_inputs(fw, exti_pins, exti_pull):
    fw.gpio.cfg(exti_pins["counting"], "in", pull=exti_pull)
    fw.gpio.cfg(exti_pins["sharing"], "in", pull=exti_pull)


@when(parsers.parse("the interrupt of the {which} pin is enabled on the {irq_edge} edge"))
def exti_enabled(fw, exti_pins, which, irq_edge):
    fw.gpio.irq(exti_pins[which], irq_edge)


@when(parsers.parse("the interrupt of the counting pin is enabled on both edges with the {irq_handler} handler"))
def exti_enabled_on_both(fw, exti_pins, irq_handler):
    fw.gpio.irq(exti_pins["counting"], "both", type=irq_handler)


@when("the interrupt of the counting pin is turned off")
def exti_off(fw, exti_pins):
    fw.gpio.irq(exti_pins["counting"], "off")


@when("the sharing pin is released")
def sharing_released(fw, exti_pins):
    fw.gpio.release(exti_pins["sharing"])


@when("the edge count of the counting pin is cleared")
def counting_cleared(fw, exti_pins):
    fw.gpio.count(exti_pins["counting"], clear=True)


@when(parsers.parse("the AD3 sends {first:d} pulses on the sharing DIO and then {second:d} pulses on the counting DIO at {hz:d} Hz"))
def exti_pulses_sent(ad3, exti_dios, first, second, hz):
    for dio, pulses in ((exti_dios["sharing"], first), (exti_dios["counting"], second)):
        ad3.pattern.pulses(dio, pulses, hz)
        ad3.pattern.wait_done(timeout=pulses / hz + 2)


@when("the AD3 sends the pulses on the DIO at the frequency")
def pulses_sent(ad3, dio, pulses, frequency):
    ad3.pattern.pulses(dio, pulses, frequency)
    ad3.pattern.wait_done(timeout=pulses / frequency + 2)


@when(parsers.parse("the firmware waits {ms:d} ms"))
def firmware_waits(fw, ms):
    fw.system.delay(ms)


@when("the interrupt is turned off")
def interrupt_off(fw, pin):
    fw.gpio.irq(pin, "off")


@when(parsers.parse("the AD3 sends {number:d} pulses on the DIO at the frequency"))
def more_pulses_sent(ad3, dio, frequency, number):
    ad3.pattern.pulses(dio, number, frequency)
    ad3.pattern.wait_done(timeout=number / frequency + 2)


@when(
    "the pin pulses the pulse count of times at the period while the logic analyzer records the DIO from just before its first rising edge",
    target_fixture="recording",
)
def pulses_recorded(fw, ad3, dio, pin, period_ms, pulse_spec):
    count = pulse_spec["count"]
    duration = (count + 1) * period_ms / 1000
    rate = min(ad3.logic.clock_hz, ad3.logic.buffer_size / (duration * 1.2))
    capture = ad3.logic.arm(rate, int(duration * 1.2 * rate), trigger=(dio, "rising"), pretrigger=0.02)
    fw.gpio.pulse(pin, count, period_ms)
    return {"rate": rate, "bits": capture.wait(timeout=duration + 2).channel(dio)}


@then("the pin, configured as an output with the drive strength, drives the DIO to every level it is set to")
def drives_with_strength(fw, ad3, pin, dio, drive):
    check_output(fw, ad3, pin, dio, drive)


@then("the pin, configured as an output, drives the DIO to every level it is set to")
def drives(fw, ad3, pin, dio):
    check_output(fw, ad3, pin, dio)


@then("the pin, configured as an input with the pull, reads every level the DIO drives, and the DIO is released")
def reads_with_pull(fw, ad3, pin, dio, pull):
    check_input(fw, ad3, pin, dio, pull)


@then("the pin and the DIO read high if the pull is up and low otherwise")
def idle_level(fw, ad3, pin, dio, pull):
    expected = 1 if pull == "up" else 0
    assert fw.gpio.get(pin) == expected
    assert ad3.dio.read(dio) == expected


@then("the DIO is pulled low")
def pulled_low(ad3, dio):
    assert ad3.dio.read(dio) == 0, "open drain must pull low"


@then("the released open-drain pin reads every level the DIO drives, and the DIO is released")
def open_drain_follows(fw, ad3, pin, dio):
    for level in (1, 0, 1):
        ad3.dio.drive(dio, level)
        assert fw.gpio.get(pin) == level, "released open-drain pin must follow the external level"
    ad3.dio.release(dio)


@then('configuring the loop pin as open drain with pull "up" or with drive "fastest" fails with "usage"')
def open_drain_options_refused(fw, loop_pin):
    for options, reason in (({"pull": "up"}, "usage"), ({"drive": "fastest"}, "usage")):
        with pytest.raises(FirmwareError) as error:
            fw.gpio.cfg(loop_pin, "od", **options)
        assert error.value.reason == reason, options


@then("the loop pin reads 0: an output starts low")
def output_starts_low(fw, loop_pin):
    assert fw.gpio.get(loop_pin) == 0, "out starts low"


@then(parsers.parse('setting the loop pin to {level:d} fails with "{reason}"'))
def set_level_refused(fw, loop_pin, level, reason):
    with pytest.raises(FirmwareError) as error:
        fw.command("gpio.set", fw.pin(loop_pin), level)
    assert error.value.reason == reason


@then(parsers.parse('pulsing the loop pin once for 1 ms fails with "{reason}": gpio.pulse needs an output'))
def pulse_refused(fw, loop_pin, reason):
    with pytest.raises(FirmwareError) as error:
        fw.gpio.pulse(loop_pin, 1, 1)
    assert error.value.reason == reason, "gpio.pulse needs an output"


@then(parsers.parse('gpio.get, gpio.set, gpio.count, gpio.irq and gpio.release of the loop pin fail with "{reason}"'))
def unopened_refused(fw, loop_pin, reason):
    for name, args in (("gpio.get", ()), ("gpio.set", (1,)), ("gpio.count", ()), ("gpio.irq", ("rising",)), ("gpio.release", ())):
        with pytest.raises(FirmwareError) as error:
            fw.command(name, fw.pin(loop_pin), *args)
        assert error.value.reason == reason, name


@then(parsers.parse('configuring the next limit pin as an input fails with "{reason}"'))
def next_limit_pin_refused(fw, limit_pins, reason):
    pins, limit = limit_pins
    with pytest.raises(FirmwareError) as error:
        fw.gpio.cfg(pins[limit], "in")
    assert error.value.reason == reason


@then("the next limit pin can be configured as an input")
def next_limit_pin_configured(fw, limit_pins):
    pins, limit = limit_pins
    fw.gpio.cfg(pins[limit], "in")


@then(parsers.parse('configuring its clk pin and then its cs pin as GPIO inputs fails with "{reason}"'))
def spi_pins_refused(fw, spi_instance, reason):
    for key in ("clk", "cs"):
        with pytest.raises(FirmwareError) as error:
            fw.gpio.cfg(spi_instance[key], "in")
        assert error.value.reason == reason, key


@then("its cs pin can be configured as a GPIO input")
def spi_pin_free(fw, spi_instance):
    fw.gpio.cfg(spi_instance["cs"], "in")


@then(parsers.parse('enabling the interrupt of the sharing pin on the rising edge and turning it off fail with "{reason}"'))
def sharing_refused(fw, exti_pins, reason):
    for edge in ("rising", "off"):
        with pytest.raises(FirmwareError) as error:
            fw.gpio.irq(exti_pins["sharing"], edge)
        assert error.value.reason == reason, edge


@then(parsers.parse('enabling the interrupt of the counting pin on the rising edge fails with "{reason}"'))
def counting_refused(fw, exti_pins, reason):
    with pytest.raises(FirmwareError) as error:
        fw.gpio.irq(exti_pins["counting"], "rising")
    assert error.value.reason == reason


@then("the interrupt of the counting pin can be enabled on the rising edge")
def counting_enabled_again(fw, exti_pins):
    fw.gpio.irq(exti_pins["counting"], "rising")


@then("enabling the interrupt of the sharing pin on the rising edge fails")
def sharing_fails(fw, exti_pins):
    with pytest.raises(FirmwareError):
        fw.gpio.irq(exti_pins["sharing"], "rising")


@then(parsers.parse("the edge count of the {which} pin is {count:d}"))
def edge_count_is(fw, exti_pins, which, count):
    assert fw.gpio.count(exti_pins[which]) == count


@then("the edge count is the number of pulses, twice that for both edges")
def edges_counted(fw, pin, edge, pulses):
    assert fw.gpio.count(pin) == expected_edges(edge, pulses)


@then("the edge count is still the number of the first pulses, twice that for both edges")
def counting_stopped(fw, pin, edge, pulses):
    assert fw.gpio.count(pin) == expected_edges(edge, pulses), "counting must stop after irq off"


@then("the DIO toggles the pulse count of times", target_fixture="toggles")
def toggle_count(recording, pulse_spec):
    count = pulse_spec["count"]
    found = analysis.edges(recording["bits"])
    assert len(found) == count, f"{len(found)} toggles instead of {count}"
    return found


@then("the intervals between the toggles average the period within the tolerance and none is off by more than the jitter, if there are any")
def toggle_intervals(recording, pulse_spec, toggles, period_ms):
    rate, tolerance, jitter = recording["rate"], pulse_spec["tolerance"], pulse_spec["jitter"]
    intervals = [(b.index - a.index) / rate for a, b in zip(toggles, toggles[1:])]
    if intervals:
        assert statistics.fmean(intervals) == pytest.approx(period_ms / 1000, rel=tolerance)
        worst = max(abs(interval - period_ms / 1000) for interval in intervals)
        assert worst <= jitter, f"toggle interval off by {worst * 1000:.3f} ms"
