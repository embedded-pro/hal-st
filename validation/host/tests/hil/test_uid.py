"""Unique device ID (`hal::UniqueDeviceId`, `info uid=`): 96 bits read from UID_BASE, the same after a reset, not erased
flash (all 0x00 or all 0xFF), with the lot number bytes in printable ASCII (RM0434 / RM0493 "Unique device ID register
(96 bits)": bytes 0-3 wafer X/Y, byte 4 wafer number, bytes 5-11 lot number)."""

from __future__ import annotations

import pytest

from hal_st_validation.groups.io import UID_BYTES, parse_uid


@pytest.fixture
def uid_cfg(board_cfg):
    return board_cfg.param("uid", {})


def read_uid(fw):
    info = fw.system.info()
    assert info.uid is not None, info.raw
    return parse_uid(info.uid)


def test_uid_layout(fw, uid_cfg):
    uid = read_uid(fw)
    assert len(uid.raw) == UID_BYTES
    assert uid.raw not in (bytes(UID_BYTES), b"\xff" * UID_BYTES), "the UID reads like erased or blank memory"
    first, end = uid_cfg.get("lot", [5, 12])
    lot = uid.raw[first:end]
    assert all(0x20 <= byte <= 0x7E for byte in lot), f"lot number {lot.hex()} is not printable ASCII"


def test_uid_is_stable(fw):
    assert read_uid(fw) == read_uid(fw)


@pytest.mark.resets_board
def test_uid_survives_reset(fw, board_cfg):
    before = read_uid(fw)
    fw.system.reset(timeout=board_cfg.param("system.boot_timeout", 5.0))
    fw.system.ping()
    assert read_uid(fw) == before
