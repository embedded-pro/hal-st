"""The fake `ble` group (validation/ble_firmware/BleGroup.cpp, PROTOCOL.md "BLE") with two fake boards on one `FakeAir`:
roles, scanning, connecting, link events on both boards, the Nordic UART payload checks, pairing, and what a reset
does to the other board; plus `BleSettings` and the `pair` skip of tests/conftest.py."""

import subprocess
import sys
from pathlib import Path

import pytest
from ad3_waveforms_bench.terminal import FirmwareTerminal, TerminalTimeout

from hal_st_validation.ble_pair import BleEnd, BlePair, BleSettings
from hal_st_validation.config import load_board
from hal_st_validation.fake_firmware import WB55_PINS, WBA55_PINS, FakeFirmware, FakeSerial
from hal_st_validation.fakes.ble import FakeAir
from hal_st_validation.firmware import Firmware
from hal_st_validation.patterns import crc_text, generate

HOST = Path(__file__).parents[2]
PERIPHERAL = "0a:00:00:e1:80:02"
CENTRAL = "0b:00:00:e1:80:02"


class Clock:
    def __init__(self) -> None:
        self.now = 100.0

    def __call__(self) -> float:
        return self.now

    def advance(self, seconds: float) -> None:
        self.now += seconds


def make_board(family, air, clock):
    fake = FakeFirmware(family=family, style="line", air=air, clock=clock, sleep=clock.advance)
    terminal = FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5)
    return Firmware(terminal, WB55_PINS if family == "stm32wb55" else WBA55_PINS)


@pytest.fixture
def clock():
    return Clock()


@pytest.fixture
def boards(clock):
    air = FakeAir()
    return make_board("stm32wb55", air, clock), make_board("stm32wba55", air, clock)


@pytest.fixture
def pair(boards):
    """Board 0 is the central and board 1 the peripheral, roles open."""
    central, peripheral = boards
    central.ble.open("central")
    peripheral.ble.open("peripheral")
    return central, peripheral


@pytest.fixture
def linked(pair):
    central, peripheral = pair
    peripheral.ble.advertise()
    central.ble.connect(PERIPHERAL)
    central.ble.wait("connected", 0.5, peer=PERIPHERAL)
    peripheral.ble.wait("connected", 0.5, peer=CENTRAL)
    return pair


def reason(firmware, line):
    response = firmware.terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


def test_only_the_wb_and_wba_families_have_the_group(clock):
    g4 = make_board("stm32g474", FakeAir(), clock)
    assert reason(g4, "ble.info") == "usage"


def test_open_chooses_the_role_once_and_reports_ready(boards):
    central, peripheral = boards
    assert central.ble.info().role == "none"
    assert central.ble.open("central") == CENTRAL
    central.ble.wait("ready", 0.5)
    assert peripheral.ble.open("peripheral", address="0a:00:00:e1:80:09") == "0a:00:00:e1:80:09"
    info = central.ble.info()
    assert (info.ready, info.role, info.address) == (True, "central", CENTRAL)
    assert reason(central, "ble.open role=central") == "busy"
    assert reason(central, "ble.open") == "usage"
    assert reason(central, "ble.open role=sniffer") == "usage"


def test_commands_need_a_role_and_the_right_one(boards, pair):
    fresh = make_board("stm32wb55", FakeAir(), Clock())
    assert reason(fresh, "ble.scan") == "notopen"
    central, peripheral = pair
    assert reason(peripheral, "ble.scan") == "unsupported"
    assert reason(central, "ble.advertise") == "unsupported"
    assert reason(central, "ble.send 01") == "notopen"


def test_scan_reports_each_advertiser_once_and_honours_the_filters(pair):
    central, peripheral = pair
    peripheral.ble.advertise(name="hal-st-renamed")
    central.ble.scan()
    found = central.ble.wait("found", 0.5)
    assert (found["addr"], found["type"], found["event"], found["name"]) == (PERIPHERAL, "public", "advInd", "hal-st-renamed")
    assert -100 < found.as_int("rssi") < 0
    peripheral.ble.advertise(name="hal-st-renamed")
    assert central.ble.absent("found", 0.05)
    central.ble.stopscan()
    central.ble.scan(addr="0c:00:00:e1:80:02")
    assert central.ble.absent("found", 0.05)
    central.ble.stopscan()
    central.ble.scan(name="other")
    assert central.ble.absent("found", 0.05)


def test_scan_sees_a_peripheral_that_starts_advertising_later_and_times_out(pair, clock):
    central, peripheral = pair
    central.ble.scan(timeout=2000)
    peripheral.ble.advertise()
    central.ble.wait("found", 0.5, addr=PERIPHERAL)
    clock.advance(2.5)
    central.ble.wait("state", 0.5, value="standby")
    assert central.ble.link().state == "standby"


def test_connect_reports_the_link_on_both_boards(linked):
    central, peripheral = linked
    for firmware, peer in ((central, PERIPHERAL), (peripheral, CENTRAL)):
        link = firmware.ble.link()
        assert (link.state, link.connected, link.peer) == ("connected", True, peer)
        assert (link.interval, link.latency, link.timeout, link.mtu) == (40, 0, 500, 23)
    assert reason(peripheral, "ble.advertise") == "failed"
    assert reason(central, f"ble.connect {PERIPHERAL}") == "busy"


def test_connect_to_an_address_nobody_advertises_times_out_or_is_cancelled(pair, clock):
    central, _ = pair
    central.ble.connect("0c:00:00:e1:80:02", timeout=1000)
    central.ble.wait("state", 0.5, value="connecting")
    clock.advance(1.5)
    central.ble.wait("connectfailed", 0.5, reason="timeout")
    assert central.ble.link().state == "standby"
    central.ble.connect("0c:00:00:e1:80:02")
    central.ble.cancel()
    central.ble.wait("connectfailed", 0.5, reason="cancelled")
    central.ble.cancel()


def test_a_central_that_connects_first_gets_the_peripheral_when_it_starts_advertising(pair):
    central, peripheral = pair
    central.ble.connect(PERIPHERAL)
    peripheral.ble.advertise()
    central.ble.wait("connected", 0.5, peer=PERIPHERAL)
    peripheral.ble.wait("connected", 0.5, peer=CENTRAL)


@pytest.mark.parametrize("initiator", ["central", "peripheral"])
def test_disconnect_gives_the_initiator_reason_0x16_and_the_other_0x13(linked, initiator):
    central, peripheral = linked
    first, second = (central, peripheral) if initiator == "central" else (peripheral, central)
    first.ble.disconnect()
    assert first.ble.wait("disconnected", 0.5).as_int("reason") == 0x16
    assert second.ble.wait("disconnected", 0.5).as_int("reason") == 0x13
    for firmware in linked:
        assert firmware.ble.link().state == "standby"
        firmware.ble.disconnect()


def test_standby_ends_a_link_and_a_reset_drops_it_for_the_peer(linked):
    central, peripheral = linked
    peripheral.ble.standby()
    assert central.ble.wait("disconnected", 0.5).as_int("reason") == 0x13
    assert peripheral.ble.wait("disconnected", 0.5).as_int("reason") == 0x16
    peripheral.ble.advertise()
    central.ble.connect(PERIPHERAL)
    central.ble.wait("connected", 0.5)
    central.system.reset()
    assert peripheral.ble.wait("disconnected", 0.5).as_int("reason") == 0x08
    assert peripheral.ble.link().state == "standby"
    assert central.ble.info().role == "none"
    assert reason(peripheral, "ble.mtu") == "unsupported"


def test_mtu_phy_and_data_length_reach_both_boards(linked):
    central, peripheral = linked
    central.ble.mtu()
    assert [firmware.ble.wait("mtu", 0.5).as_int("size") for firmware in linked] == [251, 251]
    central.ble.phy("2m", "1m")
    assert (central.ble.wait("phy", 0.5)["tx"], central.ble.wait_link(lambda link: True, 0.5).rx_phy) == ("2m", "1m")
    event = peripheral.ble.wait("phy", 0.5)
    assert (event["tx"], event["rx"]) == ("1m", "2m")
    central.ble.phy("2m", "1m")
    assert central.ble.wait("phy", 0.5)["tx"] == "2m"
    assert peripheral.ble.absent("phy", 0.2)
    central.ble.datalength()
    central_event = central.ble.wait("datalength", 0.5)
    peripheral_event = peripheral.ble.wait("datalength", 0.5)
    assert central_event.as_int("txoctets") == peripheral_event.as_int("txoctets") == 251
    assert "rxoctets" not in central_event
    assert peripheral_event.as_int("rxoctets") == 251
    assert central.ble.link().rx_octets is None
    assert peripheral.ble.link().rx_octets == 251
    assert peripheral.ble.link().mtu == 251


def test_the_central_decides_the_connection_parameters(linked):
    central, peripheral = linked
    central.ble.conparams(24, 24, 0, 400)
    for firmware in linked:
        assert firmware.ble.wait("conparams", 0.5, interval=24).as_int("timeout") == 400
    peripheral.ble.conparams(80, 80, 0, 400)
    assert central.ble.absent("conparams", 0.05)
    assert peripheral.ble.link().interval == 24
    assert reason(central, "ble.conparams min=40 max=20 latency=0 timeout=400") == "range"
    assert reason(central, "ble.conparams min=3200 max=3200 latency=0 timeout=10") == "range"
    assert reason(central, "ble.conparams min=6 max=6") == "usage"


def test_send_needs_the_service_and_fits_the_mtu(linked):
    central, peripheral = linked
    assert reason(central, "ble.send 01") == "notopen"
    central.ble.discover()
    maxsend = central.ble.wait("uart", 0.5, state="open").as_int("maxsend")
    peripheral.ble.wait("uart", 0.5, state="open")
    assert maxsend == 20
    assert reason(central, "ble.discover") == "busy"
    sent = central.ble.send(length=20, pattern="prbs", seed=5)
    assert sent.crc == int(crc_text(generate(20, "prbs", 5)), 16)
    received = peripheral.ble.wait("rx", 0.5)
    assert (received.as_int("len"), int(received["crc"], 16)) == (20, sent.crc)
    assert reason(central, "ble.send - len=21") == "range"
    assert reason(peripheral, "ble.send - len=0") == "range"
    central.ble.disconnect()
    assert peripheral.ble.wait("uart", 0.5, state="closed")
    assert reason(peripheral, "ble.send 01") == "notopen"


def test_pairing_bonds_both_boards_and_a_refusal_fails_it(linked):
    central, peripheral = linked
    peripheral.ble.allow_pairing(False)
    central.ble.pair()
    central.ble.wait("pairfailed", 0.5)
    assert central.ble.bonds() == (0, 10)
    peripheral.ble.allow_pairing(True)
    central.ble.pair()
    for firmware in linked:
        paired = firmware.ble.wait("paired", 0.5)
        assert (paired.as_int("secure"), paired.as_int("auth"), paired.as_int("keysize")) == (1, 0, 16)
        assert firmware.ble.bonds() == (1, 10)
    central.ble.removebonds()
    central.ble.wait("bonds", 0.5, count=0)
    assert central.ble.bonds()[0] == 0
    assert peripheral.ble.bonds()[0] == 1
    assert reason(central, "ble.allowpairing 1") == "unsupported"
    assert reason(peripheral, "ble.allowpairing 2") == "range"


def test_a_pair_flows_through_blepair(boards):
    central, peripheral = boards
    settings = BleSettings(event_timeout=0.5, connect_timeout=0.5, quiet_window=0.05)
    pair = BlePair.open(
        BleEnd(central, load_board("nucleo_wb55rg"), "central"),
        BleEnd(peripheral, load_board("nucleo_wba55cg"), "peripheral"),
        settings,
    )
    pair.connect()
    assert pair.enable_uart() == 248
    pair.transfer(pair.central, 100, "prbs", seed=2)
    pair.transfer(pair.peripheral, 248, "inc")
    assert pair.cleanup() == []
    for end in pair.ends:
        assert end.ble.link().state == "standby"


def test_cleanup_reports_a_board_that_stopped_answering(boards):
    central, peripheral = boards
    pair = BlePair.open(
        BleEnd(central, load_board("nucleo_wb55rg"), "central"),
        BleEnd(peripheral, load_board("nucleo_wba55cg"), "peripheral"),
        BleSettings(event_timeout=0.2),
    )
    central.terminal._serial.device.lookup = lambda name: lambda device, args, options: "ERR failed"
    failures = pair.cleanup()
    assert failures
    assert all("central" in failure for failure in failures)
    with pytest.raises(TerminalTimeout):
        central.ble.wait("connected", 0.05)


def test_settings_merge_the_board_files():
    wb, wba = load_board("nucleo_wb55rg"), load_board("nucleo_wba55cg")
    assert BleSettings.from_boards((wb, wba)).phys == ("1m", "2m")
    wb.set_param("ble.connect_timeout", 8.0)
    wba.set_param("ble.rssi_min", -80)
    wba.set_param("ble.phys", ["1m"])
    merged = BleSettings.from_boards((wb, wba))
    assert merged.connect_timeout == 8.0
    assert merged.rssi_min == -90
    assert merged.phys == ("1m",)
    wb.tests.pop("ble")
    wba.tests.pop("ble")
    assert BleSettings.from_boards((wb, wba)) == BleSettings()


def test_pair_tests_skip_without_a_second_board():
    result = subprocess.run(
        [sys.executable, "-m", "pytest", "tests/hil/test_ble.py", "--port", "/dev/not-a-port", "-rs", "-q", "-p", "no:cacheprovider"],
        cwd=HOST,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "pair test: pass --peer-port" in result.stdout
    assert "passed" not in result.stdout
