"""`i2c.i2c_decode` and `i2c.scl_timing` on synthetic logic-analyzer captures: START, repeated START, STOP, NACK,
clock stretching, and the SCL figures the HIL tests compare with TIMINGR."""

import pytest
from ad3_waveforms_bench.instruments.fake import FakeDwfApi, fake_ad3

from hal_st_validation.i2c import I2cByte, arm_on_start, arm_scope, i2c_decode, inject_on_start, rise_times, scl_timing


class Bus:
    """SCL/SDA samples: each bit is low/high/low thirds of `half` samples, with the data set while SCL is low."""

    def __init__(self, half: int = 4) -> None:
        self.half = half
        self.scl = [1] * 8
        self.sda = [1] * 8

    def put(self, scl: int, sda: int, count: int | None = None) -> None:
        count = self.half if count is None else count
        self.scl.extend([scl] * count)
        self.sda.extend([sda] * count)

    def start(self) -> None:
        self.put(1, 1)
        self.put(1, 0)
        self.put(0, 0)

    def restart(self) -> None:
        self.put(0, 1)
        self.start()

    def stop(self) -> None:
        self.put(0, 0)
        self.put(1, 0)
        self.put(1, 1)

    def byte(self, value: int, ack: bool, stretch: int = 0) -> None:
        for bit in [(value >> (7 - index)) & 1 for index in range(8)] + [0 if ack else 1]:
            self.put(0, bit)
            self.put(1, bit)
            self.put(0, bit)
        if stretch:
            self.put(0, 0 if ack else 1, stretch)

    def idle(self) -> None:
        self.put(1, 1)


def test_write_read_with_repeated_start():
    bus = Bus()
    bus.start()
    bus.byte(0x42 << 1, True)
    bus.byte(0x10, True)
    bus.restart()
    bus.byte(0x42 << 1 | 1, True)
    bus.byte(0xAB, True)
    bus.byte(0xCD, False)
    bus.stop()
    bus.idle()
    write, read = i2c_decode(bus.scl, bus.sda)
    assert (write.restart, write.address, write.read, write.address_ack, write.stop) == (False, 0x42, False, True, False)
    assert write.data == [I2cByte(0x10, True)]
    assert (read.restart, read.address, read.read, read.stop) == (True, 0x42, True, True)
    assert read.payload == b"\xab\xcd"
    assert [byte.ack for byte in read.data] == [True, False]
    assert write.start < write.end <= read.start < read.end


def test_address_nack_then_stop():
    bus = Bus()
    bus.start()
    bus.byte(0x50 << 1, False)
    bus.stop()
    bus.idle()
    (transfer,) = i2c_decode(bus.scl, bus.sda)
    assert (transfer.address, transfer.address_ack, transfer.data, transfer.stop) == (0x50, False, [], True)


def test_data_nack():
    bus = Bus()
    bus.start()
    bus.byte(0x42 << 1, True)
    bus.byte(0x01, True)
    bus.byte(0x02, False)
    bus.stop()
    (transfer,) = i2c_decode(bus.scl, bus.sda)
    assert [(byte.value, byte.ack) for byte in transfer.data] == [(1, True), (2, False)]


def test_clock_stretch_keeps_the_bits():
    bus = Bus()
    bus.start()
    bus.byte(0x42 << 1, True, stretch=200)
    bus.byte(0x5A, True, stretch=50)
    bus.stop()
    (transfer,) = i2c_decode(bus.scl, bus.sda)
    assert transfer.payload == b"\x5a"


def test_two_transfers_and_no_restart_after_stop():
    bus = Bus()
    for value in (0x11, 0x22):
        bus.start()
        bus.byte(0x42 << 1, True)
        bus.byte(value, True)
        bus.stop()
        bus.idle()
    transfers = i2c_decode(bus.scl, bus.sda)
    assert [(t.restart, t.payload, t.stop) for t in transfers] == [(False, b"\x11", True), (False, b"\x22", True)]


def test_partial_address_is_dropped():
    bus = Bus()
    bus.start()
    bus.put(0, 1)
    bus.put(1, 1)
    bus.put(0, 1)
    bus.stop()
    assert i2c_decode(bus.scl, bus.sda) == []


def test_unterminated_transfer_is_kept():
    bus = Bus()
    bus.start()
    bus.byte(0x42 << 1, True)
    bus.byte(0x99, True)
    (transfer,) = i2c_decode(bus.scl, bus.sda)
    assert (transfer.payload, transfer.stop) == (b"\x99", False)


def test_scl_timing():
    """Each bit is one third low, one third high, one third low (half = 4 samples): 12-sample periods, 8-sample low
    phases and 4-sample high phases; a stretch lengthens one low phase but not the median period."""
    bus = Bus(half=4)
    bus.start()
    bus.byte(0x42 << 1, True, stretch=100)
    bus.byte(0x0F, True)
    bus.stop()
    bus.idle()
    timing = scl_timing(bus.scl, rate=1_000_000)
    assert timing.freq == pytest.approx(1e6 / 12)
    assert timing.tlow_min == pytest.approx(8e-6)
    assert timing.thigh_min == pytest.approx(4e-6)


def test_scl_timing_needs_clocks():
    with pytest.raises(ValueError):
        scl_timing([1, 1, 0, 0, 1, 1], rate=1e6)


def test_rise_times():
    edge = [0.0] * 5 + [0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.3] + [3.3] * 5 + [0.0] * 3 + [1.0, 2.0, 3.0]
    assert rise_times(edge, rate=1e8, low=1.0, high=2.0) == [pytest.approx(20e-9), pytest.approx(10e-9)]
    assert rise_times([0.0, 1.5, 0.0, 3.0], rate=1e8, low=1.0, high=2.0) == [pytest.approx(10e-9 / 3)]
    assert rise_times([0.0, 1.5, 1.8], rate=1e8, low=1.0, high=2.0) == []


def calls(api, name):
    return api.calls_to(name)


def test_arm_on_start_triggers_on_sda_falling_with_scl_high():
    api = FakeDwfApi()
    ad3 = fake_ad3(api)
    ad3.open()
    api.logic_samples = [0b1000, 0b0000]
    pending = arm_on_start(ad3, scl_dio=3, sda_dio=5, rate=1e6, samples=1000, pretrigger=0.1)
    assert calls(api, "FDwfDigitalInTriggerSet")[-1][1:] == (0, 1 << 3, 0, 1 << 5)
    assert calls(api, "FDwfDigitalInTriggerSourceSet")[-1][1] == api.constants.trigsrcDetectorDigitalIn.value
    assert calls(api, "FDwfDigitalInTriggerPositionSet")[-1][1] == 900
    capture = pending.wait(timeout=1.0)
    assert (capture.rate, capture.trigger_index, len(capture.samples)) == (1e6, 100, 1000)
    assert capture.channel(3)[:2] == [1, 0]
    with pytest.raises(ValueError):
        arm_on_start(ad3, 3, 5, rate=1e6, samples=10**6)


def test_inject_on_start_pulses_sda_open_drain_after_the_start():
    api = FakeDwfApi()
    ad3 = fake_ad3(api)
    ad3.open()
    inject_on_start(ad3, scl_dio=3, sda_dio=5, delay_s=20e-6, width_s=10e-6)
    assert calls(api, "FDwfDigitalInTriggerSet")[-1][1:] == (0, 1 << 3, 0, 1 << 5)
    assert calls(api, "FDwfDigitalOutTriggerSourceSet")[-1][1] == api.constants.trigsrcDetectorDigitalIn.value
    assert calls(api, "FDwfDigitalOutOutputSet")[-1][1:] == (5, api.constants.DwfDigitalOutOutputOpenDrain.value)
    assert calls(api, "FDwfDigitalOutIdleSet")[-1][1:] == (5, api.constants.DwfDigitalOutIdleZet.value)
    assert calls(api, "FDwfDigitalOutCounterInitSet")[-1][1:] == (5, 0, 1000)
    assert calls(api, "FDwfDigitalOutWaitSet")[-1][1] == pytest.approx(20e-6)
    assert calls(api, "FDwfDigitalOutRunSet")[-1][1] == pytest.approx(10e-6)
    assert calls(api, "FDwfDigitalOutConfigure")[-1][1] == 1
    with pytest.raises(ValueError):
        inject_on_start(ad3, 3, 5, delay_s=0.0, width_s=0.0)


def test_arm_scope_triggers_on_the_rising_edge():
    api = FakeDwfApi()
    ad3 = fake_ad3(api)
    ad3.open()
    api.scope_levels = {0: 3.3, 1: 0.0}
    pending = arm_scope(ad3, [1, 2], rate=1e8, samples=100, trigger=1, level=1.65)
    assert calls(api, "FDwfAnalogInTriggerChannelSet")[-1][1] == 0
    assert calls(api, "FDwfAnalogInTriggerLevelSet")[-1][1] == pytest.approx(1.65)
    assert calls(api, "FDwfAnalogInTriggerAutoTimeoutSet")[-1][1] == 0.0
    samples = pending.wait(timeout=1.0)
    assert samples[1] == [3.3] * 100 and samples[2] == [0.0] * 100
