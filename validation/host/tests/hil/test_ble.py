"""BLE central and peripheral over the air through the `ble` group of `hal_st.validation_ble_firmware`: advertising,
scanning, connecting, the Nordic UART service, MTU, link parameters, PHY, data length and pairing. Two boards,
`--port` and `--peer-port`, any mix of NUCLEO-WB55RG and NUCLEO-WBA55CG; no wiring (run with `--no-ad3`).

The `ble_pair` fixture resets both boards and opens the roles once per role assignment (`a-central`: the `--port` board
is the central), so the module runs twice, each board in each role. Timeouts come from `tests.ble` of both board files.

Scenarios: features/ble.feature.
"""

from __future__ import annotations

from collections.abc import Iterator

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when

from hal_st_validation.ble_pair import BleEnd, BlePair, BleSettings

pytestmark = pytest.mark.usefixtures("ble_pair")

DEVICE_NAME = "hal-st-periph"
ABSENT_ADDRESS = "0c:00:00:e1:80:02"
LOCAL_TERMINATED = 0x16
REMOTE_TERMINATED = 0x13
UNKNOWN_COMMAND = ("usage", "unrecognized")
MTU_MAX = 251
MTU_DEFAULT = 23
SECURITY_LEVEL = 2

CENTRAL_ONLY = (
    "ble.scan",
    "ble.stopscan",
    f"ble.connect {ABSENT_ADDRESS}",
    "ble.cancel",
    "ble.phy tx=1m rx=1m",
    "ble.datalength",
    "ble.mtu",
    "ble.discover",
)
PERIPHERAL_ONLY = ("ble.advertise", "ble.standby", "ble.allowpairing 1")

MALFORMED = (
    ("ble.nonsense", "usage"),
    ("ble.info extra", "usage"),
    ("ble.open", "usage"),
    ("ble.open role=sniffer", "usage"),
    ("ble.open role=central address=zz", "usage"),
    ("ble.advertise interval=5", "range"),
    ("ble.advertise interval=20000", "range"),
    ("ble.advertise name=this-name-is-far-too-long", "range"),
    ("ble.allowpairing", "usage"),
    ("ble.allowpairing 2", "range"),
    ("ble.scan addr=nonsense", "usage"),
    ("ble.scan timeout=0", "range"),
    ("ble.connect", "usage"),
    ("ble.connect zz", "usage"),
    (f"ble.connect {ABSENT_ADDRESS} type=weird", "usage"),
    (f"ble.connect {ABSENT_ADDRESS} timeout=1", "range"),
    ("ble.phy tx=1m", "usage"),
    ("ble.phy tx=9m rx=1m", "usage"),
    ("ble.conparams min=6 max=6", "usage"),
    ("ble.conparams min=1 max=1 latency=0 timeout=400", "range"),
    ("ble.conparams min=40 max=20 latency=0 timeout=400", "range"),
    ("ble.send", "usage"),
    ("ble.send 0g", "usage"),
    ("ble.send - len=0", "range"),
    ("ble.security", "usage"),
    ("ble.security level=0", "range"),
    ("ble.security level=3", "range"),
)


@pytest.fixture(scope="module", params=("a-central", "b-central"))
def ble_pair(request, fw, peer_fw, board_cfg, peer_board_cfg) -> BlePair:
    """Both boards reset and running their roles: the `--port` board as central for `a-central`, else the
    `--peer-port` board."""
    for firmware, option in ((fw, "--port"), (peer_fw, "--peer-port")):
        try:
            firmware.ble.info()
        except FirmwareError as error:
            if error.reason not in UNKNOWN_COMMAND:
                raise
            pytest.exit(f"the board on {option} does not run hal_st.validation_ble_firmware (flash it on both boards)", returncode=3)
    a_central = request.param == "a-central"
    for firmware, board in ((fw, board_cfg), (peer_fw, peer_board_cfg)):
        firmware.system.reset(timeout=board.param("system.boot_timeout", 5.0))
    central, peripheral = ((fw, board_cfg), (peer_fw, peer_board_cfg))[:: 1 if a_central else -1]
    return BlePair.open(
        BleEnd(central[0], central[1], "central"),
        BleEnd(peripheral[0], peripheral[1], "peripheral"),
        BleSettings.from_boards((board_cfg, peer_board_cfg)),
        request.param,
    )


@pytest.fixture
def ble(ble_pair: BlePair) -> Iterator[BlePair]:
    """The pair for one scenario; afterwards both boards go back to idle."""
    yield ble_pair
    failures = ble_pair.cleanup()
    if failures:
        pytest.fail(f"cleanup failed: {failures}")


def bonds(ble: BlePair, expected: int) -> None:
    for end in ble.ends:
        count, capacity = end.ble.bonds()
        assert count == expected, f"{end.role} holds {count} bonds instead of {expected}"
        assert capacity >= 1


def pair_boards(ble: BlePair) -> None:
    for end in ble.ends:
        end.ble.security(SECURITY_LEVEL)
        end.ble.wait("security", ble.settings.event_timeout, result=0)
    ble.changed.add("security")
    ble.connect()
    ble.central.ble.pair()


@scenario("ble.feature", "Both boards open their role and report the controller ready")
def test_roles():
    pass


@scenario("ble.feature", "The central finds an advertising peripheral")
def test_scan_finds_peripheral():
    pass


@scenario("ble.feature", "A peripheral that stopped advertising is not reported")
def test_scan_after_standby():
    pass


@scenario("ble.feature", "The peripheral advertises under the name it is given")
def test_advertised_name():
    pass


@scenario("ble.feature", "The central connects to the advertising peripheral")
def test_connect():
    pass


@scenario("ble.feature", "The central ends the link")
def test_central_disconnects():
    pass


@scenario("ble.feature", "The peripheral ends the link")
def test_peripheral_disconnects():
    pass


@scenario("ble.feature", "The boards connect and disconnect again and again")
def test_reconnect_cycles():
    pass


@scenario("ble.feature", "Connecting to an address nobody advertises times out")
def test_connect_timeout():
    pass


@scenario("ble.feature", "A connection attempt can be cancelled")
def test_connect_cancel():
    pass


@scenario("ble.feature", "Both boards agree on the MTU")
def test_mtu():
    pass


@scenario("ble.feature", "The Nordic UART service opens on both boards")
def test_uart_opens():
    pass


@pytest.mark.parametrize(
    ("size", "pattern"),
    [(1, "inc"), (20, "const"), (100, "prbs"), (244, "prbs")],
)
@scenario("ble.feature", "Data crosses the link in both directions")
def test_data(size, pattern):
    pass


@scenario("ble.feature", "A payload beyond what the link carries is refused")
def test_payload_too_large():
    pass


@scenario("ble.feature", "Sending without an open service is refused")
def test_send_without_service():
    pass


@scenario("ble.feature", "The central changes the connection parameters")
def test_central_updates_parameters():
    pass


@scenario("ble.feature", "The central keeps its own parameters when the peripheral asks for others")
def test_peripheral_request_is_answered_with_central_parameters():
    pass


@pytest.mark.parametrize("phy", ["1m", "2m", "coded"])
@scenario("ble.feature", "The central changes the PHY")
def test_phy(phy):
    pass


@scenario("ble.feature", "The central requests the largest data length")
def test_data_length():
    pass


@scenario("ble.feature", "Pairing bonds both boards")
def test_pairing():
    pass


@scenario("ble.feature", "A bond survives a reconnection")
def test_bond_survives_reconnection():
    pass


@scenario("ble.feature", "Removing the bonds empties both boards")
def test_remove_bonds():
    pass


@scenario("ble.feature", "A peripheral that refuses pairing fails it")
def test_pairing_refused():
    pass


@scenario("ble.feature", "Commands of the other role are refused")
def test_other_role_commands():
    pass


@scenario("ble.feature", "A second open is refused")
def test_second_open():
    pass


@scenario("ble.feature", "Malformed and out-of-range commands are refused")
def test_errors():
    pass


@then("both boards report their role and address")
def roles(ble):
    for end in ble.ends:
        info = end.ble.info()
        assert info.ready, f"{end.role} is not ready: {info}"
        assert info.role == end.role
        assert info.address == end.address
    assert ble.central.address != ble.peripheral.address


@given("the peripheral advertises")
def peripheral_advertises(ble):
    ble.peripheral.ble.advertise()
    ble.peripheral.ble.wait("state", ble.settings.event_timeout, value="advertising")


@given("the peripheral stops advertising")
def peripheral_stops(ble):
    ble.peripheral.ble.standby()
    ble.peripheral.ble.wait("state", ble.settings.event_timeout, value="standby")


@when(parsers.parse("the peripheral advertises under the name {name}"))
def peripheral_advertises_as(ble, name):
    ble.peripheral.ble.advertise(name=name)
    ble.peripheral.ble.wait("state", ble.settings.event_timeout, value="advertising")


@when("the central scans for the peripheral")
def central_scans(ble):
    ble.central.ble.scan(addr=ble.peripheral.address, timeout=int(ble.settings.scan_timeout * 1000))


@then(
    "the central reports the peripheral with its name, public address and connectable advertising",
    target_fixture="report",
)
def central_reports(ble):
    report = ble.central.ble.wait("found", ble.settings.scan_timeout + ble.settings.event_timeout, addr=ble.peripheral.address)
    assert report["name"] == DEVICE_NAME
    assert report["type"] == "public"
    assert report["event"] == "advInd"
    return report


@then("the report carries a signal strength above the board files' minimum")
def report_rssi(ble, report):
    assert ble.settings.rssi_min <= report.as_int("rssi") < 0


@then(parsers.parse("the central reports the peripheral under the name {name}"))
def central_reports_name(ble, name):
    report = ble.central.ble.wait("found", ble.settings.scan_timeout + ble.settings.event_timeout, addr=ble.peripheral.address)
    assert report["name"] == name


@then("the central reports nothing within the quiet window")
def central_reports_nothing(ble):
    assert ble.central.ble.absent("found", ble.settings.quiet_window)


@given("the boards are connected", target_fixture="connected")
def boards_connected(ble):
    return ble.connect()


@when("the boards connect", target_fixture="connected")
def boards_connect(ble):
    return ble.connect()


@when("the central connects to the peripheral")
def central_connects(ble):
    ble.central.ble.connect(ble.peripheral.address, "public", timeout=int(ble.settings.connect_timeout * 1000))


@then("both boards report the connection with the address of the other", target_fixture="connected")
def both_connected(ble):
    limit = ble.settings.connect_timeout + ble.settings.event_timeout
    return (
        ble.central.ble.wait("connected", limit, peer=ble.peripheral.address),
        ble.peripheral.ble.wait("connected", limit, peer=ble.central.address),
    )


@then("both boards report the same link parameters")
def same_parameters(connected):
    central, peripheral = connected
    for key in ("interval", "latency", "timeout"):
        assert central.as_int(key) == peripheral.as_int(key), f"{key}: {central.raw} / {peripheral.raw}"
    assert 6 <= central.as_int("interval") <= 3200
    assert central.as_int("timeout") >= 10


@then("both boards report the link in their link state")
def link_state(ble, connected):
    for end in ble.ends:
        link = end.ble.link()
        assert link.connected
        assert link.state == "connected"
        assert link.peer == ble.other(end).address


@when("the central disconnects", target_fixture="disconnected")
def central_disconnects(ble):
    return ble.disconnect(ble.central)


@when("the peripheral disconnects", target_fixture="disconnected")
def peripheral_disconnects(ble):
    return ble.disconnect(ble.peripheral)


@then("the central reports the local host as the reason")
def central_local(disconnected):
    assert disconnected["central"].as_int("reason") == LOCAL_TERMINATED


@then("the peripheral reports the local host as the reason")
def peripheral_local(disconnected):
    assert disconnected["peripheral"].as_int("reason") == LOCAL_TERMINATED


@then("the central reports the remote user as the reason")
def central_remote(disconnected):
    assert disconnected["central"].as_int("reason") == REMOTE_TERMINATED


@then("the peripheral reports the remote user as the reason")
def peripheral_remote(disconnected):
    assert disconnected["peripheral"].as_int("reason") == REMOTE_TERMINATED


@then("both boards are idle")
def both_idle(ble):
    for end in ble.ends:
        ble.idle(end)


@when("the boards connect and disconnect for the reconnect cycles of the board files", target_fixture="cycles")
def reconnect_cycles(ble, depth):
    count = ble.settings.reconnect_cycles_full if depth == "full" else ble.settings.reconnect_cycles
    results = []
    for index in range(count):
        ble.connect()
        ble.disconnect(ble.central if index % 2 == 0 else ble.peripheral)
        for end in ble.ends:
            ble.idle(end)
        results.append(index)
    return results


@then("every cycle ended with both boards idle")
def cycles_idle(ble, cycles, depth):
    count = ble.settings.reconnect_cycles_full if depth == "full" else ble.settings.reconnect_cycles
    assert len(cycles) == count


@when(parsers.parse("the central connects to an address nobody advertises with a timeout of {timeout:d} ms"), target_fixture="timeout_ms")
def central_connects_to_absent_with_timeout(ble, timeout):
    ble.central.ble.connect(ABSENT_ADDRESS, "public", timeout=timeout)
    return timeout


@when("the central connects to an address nobody advertises")
def central_connects_to_absent(ble):
    ble.central.ble.connect(ABSENT_ADDRESS, "public", timeout=int(ble.settings.connect_timeout * 1000))


@when("the central cancels the connection attempt")
def central_cancels(ble):
    ble.central.ble.wait("state", ble.settings.event_timeout, value="connecting")
    ble.central.ble.cancel()


@then("the central reports that the connection failed because of the timeout")
def connect_timed_out(ble, timeout_ms):
    ble.central.ble.wait("connectfailed", timeout_ms / 1000 + ble.settings.event_timeout, reason="timeout")


@then("the central reports that the connection failed because it was cancelled")
def connect_cancelled(ble):
    ble.central.ble.wait("connectfailed", ble.settings.event_timeout, reason="cancelled")


@then("the central is idle")
def central_idle(ble):
    ble.idle(ble.central)


@when("the central exchanges the MTU")
def exchange_mtu(ble):
    ble.central.ble.mtu()


@then("both boards report the same MTU of at most 251 bytes", target_fixture="mtu")
def same_mtu(ble):
    central, peripheral = ble.wait_both("mtu")
    size = central.as_int("size")
    assert size == peripheral.as_int("size")
    assert MTU_DEFAULT <= size <= MTU_MAX
    return size


@then("both link states hold that MTU")
def link_mtu(ble, mtu):
    for end in ble.ends:
        assert ble.wait_link(end, lambda link: link.mtu == mtu).mtu == mtu


@when("the central discovers the Nordic UART service")
def discover(ble):
    ble.central.ble.discover()


@then("both boards report the service open")
def service_open(ble):
    for end in ble.ends:
        assert end.ble.wait("uart", ble.settings.event_timeout * 2, state="open").as_int("maxsend") > 0


@given("the boards are connected with the Nordic UART service open", target_fixture="maxsend")
def connected_with_uart(ble):
    ble.connect()
    return ble.enable_uart()


@when("the central sends the payload", target_fixture="sent")
def central_sends(ble, maxsend, size, pattern):
    if size > maxsend:
        pytest.skip(f"the service carries {maxsend} bytes")
    return ble.send(ble.central, size, pattern, seed=size)


@then("the peripheral receives the same payload")
def peripheral_receives(ble, sent):
    ble.expect(ble.peripheral, sent)


@when("the peripheral sends the payload", target_fixture="sent")
def peripheral_sends(ble, size, pattern):
    return ble.send(ble.peripheral, size, pattern, seed=size + 1)


@then("the central receives the same payload")
def central_receives(ble, sent):
    ble.expect(ble.central, sent)


@then("a payload one byte larger than the service allows is refused with range on both boards")
def payload_too_large(ble, maxsend):
    for end in ble.ends:
        with pytest.raises(FirmwareError) as error:
            end.ble.send(length=maxsend + 1)
        assert error.value.reason == "range"


@then("both boards refuse to send with notopen")
def send_refused(ble):
    for end in ble.ends:
        with pytest.raises(FirmwareError) as error:
            end.ble.send(b"\x01")
        assert error.value.reason == "notopen"


LATENCY = 0
SUPERVISION_TIMEOUT = 400


@when(parsers.parse("the central requests a connection interval of {interval:d} units"), target_fixture="requested")
def central_requests_interval(ble, interval):
    ble.central.ble.conparams(interval, interval, LATENCY, SUPERVISION_TIMEOUT)
    return interval


@given(parsers.parse("the central has requested a connection interval of {interval:d} units"), target_fixture="requested")
def central_has_requested_interval(ble, interval):
    ble.central.ble.conparams(interval, interval, LATENCY, SUPERVISION_TIMEOUT)
    ble.wait_both("conparams", interval=interval)
    return interval


@when(parsers.parse("the peripheral requests a connection interval of {interval:d} units"))
def peripheral_requests_interval(ble, interval):
    ble.peripheral.ble.conparams(interval, interval, LATENCY, SUPERVISION_TIMEOUT)


@then("both boards report the new connection parameters")
def new_parameters(ble, requested):
    for event in ble.wait_both("conparams", interval=requested):
        assert event.as_int("latency") == LATENCY
        assert event.as_int("timeout") == SUPERVISION_TIMEOUT


@then("both link states hold them")
def link_holds_parameters(ble, requested):
    for end in ble.ends:
        link = ble.wait_link(end, lambda link: link.interval == requested)
        assert (link.latency, link.timeout) == (LATENCY, SUPERVISION_TIMEOUT)


@then("both link states still hold the central's interval after the quiet window")
def central_interval_kept(ble, requested):
    ble.central.fw.terminal.pump(ble.settings.quiet_window)
    for end in ble.ends:
        assert end.ble.link().interval == requested


@when("the central requests the PHY for both directions")
def central_requests_phy(ble, phy):
    if phy not in ble.settings.phys:
        pytest.skip(f"the {phy} PHY is not on both boards (tests.ble.phys)")
    ble.central.ble.phy(phy, phy)


@then("both boards report the PHY")
def phy_reported(ble, phy):
    ends = ble.ends if phy != "1m" else (ble.central,)
    for end in ends:
        event = end.ble.wait("phy", ble.settings.event_timeout * 2)
        assert (event["tx"], event["rx"]) == (phy, phy)


@then("both link states hold it")
def link_holds_phy(ble, phy):
    for end in ble.ends:
        link = ble.wait_link(end, lambda link: (link.tx_phy, link.rx_phy) == (phy, phy))
        assert link.connected


@when("the central requests the largest data length")
def central_requests_data_length(ble):
    ble.central.ble.datalength()


@then("both boards report the same data length", target_fixture="data_length")
def same_data_length(ble):
    central, peripheral = ble.wait_both("datalength", ble.settings.event_timeout * 2)
    assert central.as_int("txoctets") == peripheral.as_int("rxoctets")
    assert 27 <= central.as_int("txoctets") <= MTU_MAX
    assert 27 <= peripheral.as_int("txoctets") <= MTU_MAX
    return central


@then("a payload as large as the link carries still arrives intact")
def large_payload(ble, maxsend):
    ble.transfer(ble.central, maxsend, "prbs", seed=11)
    ble.transfer(ble.peripheral, maxsend, "prbs", seed=12)


@given("the boards are connected with security level 2 on both")
def connected_with_security(ble):
    for end in ble.ends:
        end.ble.security(SECURITY_LEVEL)
        end.ble.wait("security", ble.settings.event_timeout, result=0)
    ble.changed.add("security")
    ble.connect()


@given("the boards are paired")
def boards_paired(ble):
    pair_boards(ble)
    for end in ble.ends:
        end.ble.wait("paired", ble.settings.connect_timeout + ble.settings.event_timeout)


@given("the peripheral refuses pairing")
def peripheral_refuses_pairing(ble):
    ble.peripheral.ble.allow_pairing(False)


@when("the central pairs")
def central_pairs(ble):
    ble.central.ble.pair()


@then("both boards report the pairing complete with the same strength")
def pairing_complete(ble):
    limit = ble.settings.connect_timeout + ble.settings.event_timeout
    central = ble.central.ble.wait("paired", limit)
    peripheral = ble.peripheral.ble.wait("paired", limit)
    for key in ("secure", "auth", "keysize"):
        assert central.as_int(key) == peripheral.as_int(key), f"{key}: {central.raw} / {peripheral.raw}"
    assert 7 <= central.as_int("keysize") <= 16
    assert not central.as_bool("auth"), "Just Works cannot authenticate"


@then("each board holds one bond")
def one_bond(ble):
    bonds(ble, 1)


@then("each board still holds one bond")
def still_one_bond(ble):
    bonds(ble, 1)


@then("each board holds no bond")
def no_bond(ble):
    bonds(ble, 0)


@when("the boards disconnect and connect again")
def reconnect(ble):
    ble.disconnect(ble.central)
    for end in ble.ends:
        ble.idle(end)
    ble.connect()


@then("data still crosses the link")
def data_after_reconnect(ble):
    ble.enable_uart()
    ble.transfer(ble.central, 20, "inc", seed=3)
    ble.transfer(ble.peripheral, 20, "inc", seed=4)


@when("both boards remove their bonds")
def remove_bonds(ble):
    for end in ble.ends:
        end.ble.removebonds()
        end.ble.wait("bonds", ble.settings.event_timeout, count=0)


@then("the central reports the pairing failed")
def pairing_failed(ble):
    ble.central.ble.wait("pairfailed", ble.settings.connect_timeout + ble.settings.event_timeout)


def refused(end: BleEnd, lines, reason: str) -> None:
    for line in lines:
        with pytest.raises(FirmwareError) as error:
            end.fw.terminal.command(line)
        assert error.value.reason == reason, f"{end.role}: {line} answered {error.value.reason}"


@then("the peripheral refuses the central commands with unsupported")
def peripheral_refuses(ble):
    refused(ble.peripheral, CENTRAL_ONLY, "unsupported")


@then("the central refuses the peripheral commands with unsupported")
def central_refuses(ble):
    refused(ble.central, PERIPHERAL_ONLY, "unsupported")


@then("both boards refuse to open a role again with busy")
def open_again(ble):
    for end in ble.ends:
        refused(end, (f"ble.open role={end.role}",), "busy")


@then("every malformed or out-of-range ble command line fails with its reason")
def malformed(ble):
    for end in ble.ends:
        for line, reason in MALFORMED:
            refused(end, (line,), reason)
