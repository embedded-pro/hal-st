"""Unique device ID (`hal::UniqueDeviceId`, `info uid=`): 96 bits read from UID_BASE, the same after a reset, not erased
flash (all 0x00 or all 0xFF), with the lot number bytes in printable ASCII (RM0434 / RM0493 "Unique device ID register
(96 bits)": bytes 0-3 wafer X/Y, byte 4 wafer number, bytes 5-11 lot number).

Scenarios: features/uid.feature.
"""

from __future__ import annotations

import pytest
from pytest_bdd import parsers, scenario, then, when

from hal_st_validation.groups.io import UID_BYTES, parse_uid


@pytest.fixture
def uid_cfg(board_cfg):
    return board_cfg.param("uid", {})


def read_uid(fw):
    info = fw.system.info()
    assert info.uid is not None, info.raw
    return parse_uid(info.uid)


@scenario("uid.feature", "The UID has the documented layout")
def test_uid_layout():
    pass


@scenario("uid.feature", "The UID reads the same twice")
def test_uid_is_stable():
    pass


@scenario("uid.feature", "The UID survives a reset")
def test_uid_survives_reset():
    pass


@when("the UID is read", target_fixture="uid")
def uid_read(fw):
    return read_uid(fw)


@when("the board resets")
def reset_board(fw, board_cfg):
    fw.system.reset(timeout=board_cfg.param("system.boot_timeout", 5.0))


@then(parsers.parse("it has {count:d} bytes"))
def uid_size(uid, count):
    assert count == UID_BYTES
    assert len(uid.raw) == UID_BYTES


@then("it does not read like erased or blank memory")
def not_blank(uid):
    assert uid.raw not in (bytes(UID_BYTES), b"\xff" * UID_BYTES), "the UID reads like erased or blank memory"


@then("its lot number bytes are printable ASCII")
def lot_printable(uid, uid_cfg):
    first, end = uid_cfg.get("lot", [5, 12])
    lot = uid.raw[first:end]
    assert all(0x20 <= byte <= 0x7E for byte in lot), f"lot number {lot.hex()} is not printable ASCII"


@then("the UID reads the same twice")
def stable(fw):
    assert read_uid(fw) == read_uid(fw)


@then("the board answers ping")
def answers_ping(fw):
    fw.system.ping()


@then("the UID reads as before")
def same_as_before(fw, uid):
    assert read_uid(fw) == uid
