import os
import subprocess
import sys
from pathlib import Path

import pytest

from hal_st_validation.config import ConfigError, KnownGap, known_gap_outcome, load_board, parse_board

HOST_DIR = Path(__file__).resolve().parents[2]
BOARDS = ["nucleo_wb55rg", "nucleo_wba55cg"]


@pytest.mark.parametrize(
    ("pattern", "test_id", "matches"),
    [
        ("test_pwm.py::*", "test_pwm.py::test_waveform[tim1-freq=100]", True),
        ("test_pwm.py::*", "test_pwm.py::test_open_errors", True),
        ("test_pwm.py::*", "test_qei.py::test_speed", False),
        ("test_pwm.py::test_waveform*", "test_pwm.py::test_waveform[tim1]", True),
        ("test_pwm.py::test_waveform", "test_pwm.py::test_waveform[tim1]", False),
        ("test_adc.py::test_sampling_time[*sampling=3.5*]", "test_adc.py::test_sampling_time[sampling=3.5-trigger=timer]", True),
        ("test_adc.py::test_sampling_time[*sampling=3.5*]", "test_adc.py::test_sampling_time[sampling=1.5-trigger=timer]", False),
        ("test_adc.py::test_sampling_time[*sampling=3.5*]", "test_adc.py::test_sampling_time[sampling=315-trigger=timer]", False),
        ("test_spi.py::test_receive_only_first[*variant=sync*]", "test_spi.py::test_receive_only_first[spi1-variant=sync]", True),
        ("test_spi.py::test_receive_only_first[*variant=sync*]", "test_spi.py::test_receive_only_first[spi1-variant=dma]", False),
        ("test_?.py::t", "test_a.py::t", True),
        ("test_?.py::t", "test_ab.py::t", False),
    ],
)
def test_patterns_match_whole_ids_with_literal_brackets(pattern, test_id, matches):
    """`*` and `?` are the only wildcards: the brackets of parameter ids are literal, unlike fnmatch."""
    assert KnownGap((pattern,), "reason").matches(test_id) is matches


def test_outcome_skips_hanging_gaps_and_xfails_the_others():
    hangs = KnownGap(("test_pwm.py::*",), "PwmStm.cpp:256 - pwm.open aborts", hangs=True)
    fails = KnownGap(("test_pwm.py::test_waveform[*mode=center*]",), "PwmStm.cpp:381 - centre period", hangs=False)
    gaps = [hangs, fails]
    assert known_gap_outcome(gaps, "test_qei.py::test_speed") is None
    assert known_gap_outcome(gaps, "test_pwm.py::test_open_errors") == (
        "skip",
        "known gap (firmware aborts/hangs): PwmStm.cpp:256 - pwm.open aborts",
    )
    assert known_gap_outcome(gaps, "test_pwm.py::test_waveform[tim1-mode=center]")[0] == "skip"
    kind, reason = known_gap_outcome(gaps, "test_pwm.py::test_waveform[tim1-mode=center]", run_hanging=True)
    assert kind == "xfail"
    assert reason == "known gap: PwmStm.cpp:256 - pwm.open aborts; PwmStm.cpp:381 - centre period"
    assert known_gap_outcome([fails], "test_pwm.py::test_waveform[tim1-mode=center]") == (
        "xfail",
        "known gap: PwmStm.cpp:381 - centre period",
    )
    assert known_gap_outcome([fails], "test_pwm.py::test_waveform[tim1-mode=edge]") is None


def _raw(gaps):
    return {"board": "x", "family": "stm32wb55", "known_gaps": gaps}


def test_parsing():
    board = parse_board(
        _raw([{"tests": ["test_a.py::*"], "reason": " a.cpp:1 - b ", "hangs": True}, {"tests": ["test_b.py::t"], "reason": "c"}])
    )
    assert board.known_gaps == (KnownGap(("test_a.py::*",), "a.cpp:1 - b", True), KnownGap(("test_b.py::t",), "c", False))
    assert parse_board({"board": "x", "family": "stm32wb55"}).known_gaps == ()


@pytest.mark.parametrize(
    "gaps",
    [
        {"tests": ["test_a.py::*"], "reason": "r"},
        [{"tests": [], "reason": "r"}],
        [{"tests": "test_a.py::*", "reason": "r"}],
        [{"tests": ["test_a.py"], "reason": "r"}],
        [{"tests": ["test_a.py::*"]}],
        [{"tests": ["test_a.py::*"], "reason": " "}],
        [{"tests": ["test_a.py::*"], "reason": "r", "hangs": "yes"}],
        [{"tests": ["test_a.py::*"], "reason": "r", "xfail": True}],
    ],
)
def test_invalid_entries_are_rejected(gaps):
    with pytest.raises(ConfigError):
        parse_board(_raw(gaps))


@pytest.mark.parametrize("name", BOARDS)
def test_board_files_cite_code(name):
    """Every gap names its source (`file:line`) and the symptom."""
    gaps = load_board(name).known_gaps
    assert gaps
    for gap in gaps:
        assert ".cpp:" in gap.reason and " - " in gap.reason, gap.reason


@pytest.fixture(scope="module")
def collected():
    """The HIL test ids (relative to tests/hil) of each board at the default depth."""
    ids = {}
    for name in BOARDS:
        result = subprocess.run(
            [sys.executable, "-m", "pytest", "tests/hil", "--co", "-q", "-p", "no:cacheprovider", "--board", name],
            cwd=HOST_DIR,
            capture_output=True,
            text=True,
            check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        ids[name] = [line.removeprefix("tests/hil/") for line in result.stdout.splitlines() if line.startswith("tests/hil/")]
    return ids


@pytest.mark.parametrize("name", BOARDS)
def test_every_pattern_matches_a_collected_test(name, collected):
    """A pattern that matches nothing is a typo or a renamed test: the gap would silently stop applying."""
    assert collected[name]
    for gap in load_board(name).known_gaps:
        for pattern in gap.tests:
            assert any(KnownGap((pattern,), gap.reason).matches(test_id) for test_id in collected[name]), pattern


MARK_REPORTER = """
def pytest_collection_finish(session):
    for item in session.items:
        for mark in item.iter_markers():
            if mark.name in ("skip", "xfail"):
                print("GAPMARK", item.nodeid.partition("tests/hil/")[2], mark.name, mark.kwargs.get("reason", ""))
"""


def collected_marks(tmp_path, board, *options):
    """The skip/xfail marks conftest.py puts on the HIL tests, as {test id: [(mark, reason), ...]}."""
    (tmp_path / "gap_mark_reporter.py").write_text(MARK_REPORTER, encoding="utf-8")
    environment = {**os.environ, "PYTHONPATH": os.pathsep.join(filter(None, [str(tmp_path), os.environ.get("PYTHONPATH")]))}
    command = [sys.executable, "-m", "pytest", "tests/hil", "--co", "-q", "-p", "no:cacheprovider", "-p", "gap_mark_reporter"]
    result = subprocess.run(
        [*command, "--board", board, *options], cwd=HOST_DIR, capture_output=True, text=True, env=environment, check=False
    )
    assert result.returncode == 0, result.stdout + result.stderr
    marks = {}
    for line in result.stdout.splitlines():
        if line.startswith("GAPMARK "):
            _, test_id, name, reason = line.split(" ", 3)
            marks.setdefault(test_id, []).append((name, reason))
    return marks


def test_conftest_applies_the_gaps_of_the_board(tmp_path):
    marks = collected_marks(tmp_path, "nucleo_wba55cg", "--port", "nosuchport")
    receive_only = {test_id: found for test_id, found in marks.items() if test_id.startswith("test_spi.py::test_receive_only_first[")}
    assert [name for name, _ in receive_only["test_spi.py::test_receive_only_first[spi1-variant=interrupt]"]] == ["xfail"]
    assert "SpiMasterStm.cpp:160-168" in receive_only["test_spi.py::test_receive_only_first[spi1-variant=interrupt]"][0][1]
    sync = receive_only["test_spi.py::test_receive_only_first[spi1-variant=sync]"]
    assert sync == [("skip", sync[0][1])] and sync[0][1].startswith("known gap (firmware aborts/hangs): ")
    assert "test_spi.py::test_receive_only_first[spi1-variant=dma]" not in marks
    assert "test_pwm.py::test_open_errors" not in marks, "argument errors never reach the driver"
    assert [name for name, _ in marks["test_pwm.py::test_duty_errors"]] == ["skip"]


def test_run_known_gaps_turns_skips_into_expected_failures(tmp_path):
    marks = collected_marks(tmp_path, "nucleo_wb55rg", "--port", "nosuchport", "--run-known-gaps")
    assert [name for name, _ in marks["test_pwm.py::test_duty_errors"]] == ["xfail"]
    assert not any(name == "skip" and "known gap" in reason for found in marks.values() for name, reason in found)


def test_fake_ignores_the_gaps(tmp_path):
    marks = collected_marks(tmp_path, "nucleo_wba55cg", "--fake")
    assert not any("known gap" in reason for found in marks.values() for _, reason in found)
