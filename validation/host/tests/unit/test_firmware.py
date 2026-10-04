import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.firmware import Firmware, settle


@pytest.fixture
def fake():
    return FakeFirmware()


@pytest.fixture
def fw(fake):
    return Firmware(FirmwareTerminal(serial=FakeSerial(fake, chunk=4), timeout=0.5), WB55_PINS)


@pytest.fixture
def fake_wba():
    return FakeFirmware(family="stm32wba55")


@pytest.fixture
def fw_wba(fake_wba):
    return Firmware(FirmwareTerminal(serial=FakeSerial(fake_wba, chunk=4), timeout=0.5), WBA55_PINS)


def last(fake):
    return fake.received[-1]


def test_system(fw, fake):
    fw.system.ping()
    info = fw.system.info()
    assert info.board == "NUCLEO-WB55RG"
    assert info.family == "stm32wb55"
    assert info.sysclk == 64_000_000
    assert info.reset == "pin"
    assert info.uid == fake.uid
    assert fw.system.pins() == WB55_PINS
    fw.system.delay(5)
    assert last(fake) == "delay 5"
    boot = fw.system.reset(timeout=1.0)
    assert boot.reset == "sw"
    assert boot.board == "NUCLEO-WB55RG"


def test_system_wba55(fw_wba):
    info = fw_wba.system.info()
    assert (info.board, info.family, info.sysclk) == ("NUCLEO-WBA55CG", "stm32wba55", 100_000_000)
    assert fw_wba.system.pins() == WBA55_PINS


def test_gpio_resolves_aliases_and_tracks_release(fw, fake):
    fw.gpio.cfg("led0", "out", drive="fast")
    fw.gpio.set("led0", 1)
    assert fake.received[-2:] == ["gpio.cfg PB0 out drive=fast", "gpio.set PB0 1"]
    assert fw.gpio.get("PB0") == 1
    fw.gpio.cfg("gpio0", "in", pull="up")
    assert last(fake) == "gpio.cfg PC6 in pull=up"
    fw.gpio.irq("gpio0", "both", type="immediate")
    assert last(fake) == "gpio.irq PC6 both type=immediate"
    fake.gpio_counts["PC6"] = 3
    assert fw.gpio.count("gpio0", clear=True) == 3
    assert last(fake) == "gpio.count PC6 clear=1"
    fw.gpio.pulse("led0", 3, 10)
    assert last(fake) == "gpio.pulse PB0 3 10"
    assert fw.open_instances == [("gpio", "PB0"), ("gpio", "PC6")]
    fw.gpio.release("gpio0")
    assert fw.open_instances == [("gpio", "PB0")]
    fw.close_all()
    assert last(fake) == "gpio.release PB0"
    assert fw.open_instances == []


@pytest.mark.parametrize(
    ("kwargs", "line"),
    [
        ({"channels": [1, 2]}, "pwm.open 1 channels=1,2"),
        ({"pins": ["tim1ch1", ("tim1ch2", "tim1ch2n"), (None, "tim1ch3n")]}, "pwm.open 1 pins=PA8,PA9:PB8,-:PB9"),
        ({"channels": [3], "pins": [("PA10", None)]}, "pwm.open 1 channels=3 pins=PA10:-"),
        ({"channels": [1], "freq": 20000, "mode": "center", "prescaler": 63}, "pwm.open 1 channels=1 freq=20000 mode=center prescaler=63"),
        ({"channels": [1], "pins": [("PA8", "PA7")], "dead": 500}, "pwm.open 1 channels=1 pins=PA8:PA7 dead=500"),
        ({"channels": [1], "dead": "off", "sync": True}, "pwm.open 1 channels=1 dead=off sync=1"),
        ({"channels": [1], "inv": True, "invn": False, "idle": True, "idlen": False}, "pwm.open 1 channels=1 inv=1 invn=0 idle=1 idlen=0"),
        ({"channels": [1], "brk": "tim1bkin", "brkpol": "low", "brkauto": True}, "pwm.open 1 channels=1 brk=PB12 brkpol=low brkauto=1"),
        (
            {"channels": [1], "mode": "centerup", "preload": False, "trgo": "update"},
            "pwm.open 1 channels=1 mode=centerup preload=0 trgo=update",
        ),
        ({"channels": [1], "brk": "tim1bkin", "brkfilter": 7}, "pwm.open 1 channels=1 brk=PB12 brkfilter=7"),
    ],
)
def test_pwm_open_formatting(fw, fake, kwargs, line):
    assert fw.pwm.open(1, **kwargs) == 64_000_000 // (kwargs.get("prescaler", 0) + 1)
    assert last(fake) == line
    fw.pwm.close(1)
    assert fw.open_instances == []


def test_pwm_commands(fw, fake):
    fw.pwm.open(2, channels=[1, 2, 3, 4], freq=1000)
    fw.pwm.duty(2, 12.5, 100, 0, 33.33333)
    assert last(fake) == "pwm.duty 2 12.5 100 0 33.3333"
    fw.pwm.duty(2, 50)
    assert last(fake) == "pwm.duty 2 50"
    fw.pwm.freq(2, 2000)
    assert last(fake) == "pwm.freq 2 2000"
    fw.pwm.stop(2)
    assert last(fake) == "pwm.stop 2"
    with pytest.raises(ValueError):
        fw.pwm.duty(2)
    with pytest.raises(FirmwareError) as error:
        fw.pwm.open(1, channels=[1])
    assert error.value.reason == "busy"
    assert fw.open_instances == [("pwm", 2)]


def test_close_all_ignores_notopen(fw, fake):
    fw.uart.open(1, lp=True)
    fake.opened.clear()
    assert fw.close_all() == []


def test_close_all_reports_other_failures(fw, fake):
    fw.track(("pwm", 5), "pwm.close", 99)
    assert fw.close_all() == ["pwm.close 99: range"]


def test_uart_formatting(fw, fake):
    fw.uart.open(
        1, lp=True, tx="lpuart1tx", rx="lpuart1rx", rts="lpuart1rts", cts="lpuart1cts", baud=921600, parity="even", flow="rtscts", dma=True
    )
    assert last(fake) == "uart.open 1 lp=1 tx=PA2 rx=PA3 rts=PB12 cts=PA6 baud=921600 parity=even flow=rtscts dma=1"
    fw.uart.send(1, b"\x01\xff")
    assert last(fake) == "uart.send 1 01ff"
    fake.uart_rx[1] += b"\xaa"
    assert fw.uart.recv(1, timeout=10, len=1) == b"\xaa"
    assert last(fake) == "uart.recv 1 timeout=10 len=1"
    assert fw.uart.recv(1) == b""
    with pytest.raises(ValueError):
        fw.uart.send(1, b"")
    fw.uart.close(1)
    assert fw.open_instances == []


def test_uart_variant_formatting(fw_wba, fake_wba):
    fw_wba.uart.open(2, tx="usart2tx", rx="usart2rx", swap=True, duplex=True)
    assert last(fake_wba) == "uart.open 2 tx=PB0 rx=PA11 swap=1 duplex=1"
    fw_wba.uart.close(2)
    fw_wba.uart.open(2, tx="usart2tx", rx="usart2rx", rts="usart2rts", flow="rts", sync=True)
    assert last(fake_wba) == "uart.open 2 tx=PB0 rx=PA11 rts=PB1 flow=rts sync=1"


def test_spi_formatting(fw, fake):
    fw.spi.open(1, clk="spi1clk", mosi="spi1mosi", miso="spi1miso", cs="spi1cs", baud=4000000, mode=3, dma=True)
    assert last(fake) == "spi.open 1 clk=PA5 mosi=PA7 miso=PA6 cs=PA4 baud=4000000 mode=3 dma=1"
    fake.spi_miso = 0xFF
    assert fw.spi.xfer(1, b"", rx=2) == b"\xff\xff"
    assert last(fake) == "spi.xfer 1 - rx=2"
    assert fw.spi.xfer(1, b"\x12", continue_=True) == b"\xff"
    assert last(fake) == "spi.xfer 1 12 continue=1"
    assert fw.spi.xfer(1, b"\x12\x34", rx=0) == b""


def test_adc_formatting(fw, fake):
    fw.adc.open(1, pins=["ain4", "pc2", "ain4"], sampling=640.5)
    assert last(fake) == "adc.open 1 pins=PC3,PC2,PC3 sampling=640.5"
    fake.adc_codes["PC3"] = 1234
    fake.adc_codes["PC2"] = 99
    assert fw.adc.measure(1, n=2) == [1234, 99, 1234] * 2
    assert last(fake) == "adc.measure 1 n=2"
    fw.adc.close(1)
    fw.adc.open(1, pins=["ain1"], sampling="2.5", timer=2, rate=10000)
    assert last(fake) == "adc.open 1 pins=PC0 sampling=2.5 timer=2 rate=10000"
    assert fw.open_instances == [("adc", 1)]


def test_qei_formatting(fw, fake):
    fw.qei.open(2, a="qei2a", b="qei2b", idx="qei2idx", res=100, offset=5, inva=True, invb=False, cap="b", filter=3, vel=500)
    assert last(fake) == "qei.open 2 a=PA15 b=PA1 idx=PC6 res=100 offset=5 inva=1 invb=0 cap=b filter=3 vel=500"
    reading = fw.qei.read(2)
    assert (reading.pos, reading.dir, reading.speed, reading.res) == (5, "fwd", 0, 100)
    fake.gpio_levels["PC6"] = 1
    assert fw.qei.index(2) == 1
    assert last(fake) == "qei.index 2"
    fw.qei.close(2)
    fw.qei.open(1, lp=True, a="lptim1in1", b="lptim1in2", filter=4, vel="off")
    assert last(fake) == "qei.open 1 lp=1 a=PC0 b=PC2 filter=4 vel=off"


def test_watchdog_formatting(fw, fake):
    fake.clock = lambda: 0.0
    fw.wdt.start(0, timeout=250, feed="manual", pin="gpio0")
    assert last(fake) == "wdt.start 0 timeout=250 feed=manual pin=PC6"
    fw.wdt.feed(0)
    assert last(fake) == "wdt.feed 0"
    assert fw.open_instances == [], "a started watchdog cannot be closed"
    fake.event("EVT wdt index=0 warning=1")
    assert fw.wdt.wait_warning(0, timeout=0.5).as_int("warning") == 1
    fake.event("EVT wdt index=0 warning=2")
    fw.system.ping()
    assert [event.as_int("warning") for event in fw.wdt.warnings(0)] == [2]


def test_unsupported_groups_through_raw_commands(fw):
    with pytest.raises(FirmwareError) as error:
        fw.command("can.open", 1, bitrate=500000)
    assert error.value.reason == "unsupported"


def test_spi_extension_formatting(fw, fake):
    fw.spi.open(1, clk="spi1clk", mosi="spi1mosi", miso="spi1miso", nss="spi1nss", dma=True, bits=12, lsb=True)
    assert last(fake) == "spi.open 1 clk=PA5 mosi=PA7 miso=PA6 dma=1 bits=12 lsb=1 nss=PA4"
    fw.spi.close(1)
    fw.spi.open(2, clk="spi2clk", mosi="spi2mosi", miso="spi2miso", lsb=False)
    assert last(fake) == "spi.open 2 clk=PB13 mosi=PB15 miso=PB14 lsb=0"


def test_adc_trgo_formatting(fw, fake):
    fw.pwm.open(2, channels=[1], trgo="update")
    fw.adc.open(1, pins=["ain4"], trgo=2)
    assert last(fake) == "adc.open 1 pins=PC3 trgo=2"
    assert fw.open_instances == [("pwm", 2), ("adc", 1)]


def test_uart_sendonly_formatting(fw, fake):
    fw.uart.open(1, lp=True, tx="lpuart1tx", sendonly=True)
    assert last(fake) == "uart.open 1 lp=1 tx=PA2 sendonly=1"
    assert fw.uart.recv(1) == b""


def test_qei_capture_on_edges_formatting(fw, fake):
    fw.qei.open(1, lp=True, a="lptim1in1", b="lptim1in2", cap="rise")
    assert last(fake) == "qei.open 1 lp=1 a=PC0 b=PC2 cap=rise"


def test_begin_sends_without_waiting(fw, fake):
    pending = fw.gpio.begin("cfg", "led0", "out", drive="fast")
    assert pending.wait().ok
    assert last(fake) == "gpio.cfg led0 out drive=fast"
    assert settle(fw.gpio.begin("get", "PB0")).as_int("value") == 0
