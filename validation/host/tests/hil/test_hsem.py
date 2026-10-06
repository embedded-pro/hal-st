"""Hardware semaphores of the STM32WB55 through the `hsem` group: two-step locks per process (`HAL_HSEM_Take`/
`HAL_HSEM_Release`), the lock state read from R (never RLR, whose read is itself a lock attempt), and
`hal::SynchronousHardwareSemaphoreStm` over `hal::SynchronousHardwareSemaphoreMasterStm`.

`hsem.lock <n> hold=<us>` takes the semaphore for process 1 and lets the scaffold timer (TIM17) free it from its
interrupt after `hold`: the synchronous lock must wait that long (B.9: `WaitLock` used to return at once), and the
query `IsLockedByCurrentCore` must not lock a free semaphore. No wiring.

Scenarios: features/hsem.feature.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import given, parsers, scenario, then, when


@pytest.fixture
def hsem_cfg(board_cfg):
    return board_cfg.param("hsem")


def expect_reason(fw, line: str, reason: str) -> None:
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


@scenario("hsem.feature", "A two-step lock shows this core and the process, and release frees it")
def test_take_and_release():
    pass


@scenario("hsem.feature", "A semaphore held by one process is refused to another")
def test_other_process_is_refused():
    pass


@scenario("hsem.feature", "hold= frees the semaphore from an EMIL timer")
def test_hold_frees_the_semaphore():
    pass


@pytest.mark.board_params("hold_us", "hsem.holds_us")
@scenario("hsem.feature", "The synchronous lock waits until process 1 frees the semaphore")
def test_lock_waits_for_foreign_owner(hold_us):
    pass


@scenario("hsem.feature", "The synchronous lock of a free semaphore returns at once")
def test_lock_of_free_semaphore_returns_at_once():
    pass


@scenario("hsem.feature", "The synchronous lock refuses a semaphore held by another process")
def test_lock_refuses_held_semaphore():
    pass


@scenario("hsem.feature", "The ownership query takes no lock")
def test_query_takes_no_lock():
    pass


@pytest.mark.parametrize(
    "line, reason",
    [
        ("hsem.take 1", "usage"),
        ("hsem.take procid=1", "usage"),
        ("hsem.take 32 procid=1", "range"),
        ("hsem.take 1 procid=0", "range"),
        ("hsem.take 1 procid=256", "range"),
        ("hsem.take 1 procid=1 hold=0", "range"),
        ("hsem.take 1 procid=1 hold=60001", "range"),
        ("hsem.release 1", "usage"),
        ("hsem.status", "usage"),
        ("hsem.status 32", "range"),
        ("hsem.lock 32", "range"),
        ("hsem.lock 1 hold=1", "range"),
        ("hsem.lock 1 hold=65537", "range"),
        ("hsem.lock 1 procid=1", "usage"),
        ("hsem.mine x", "usage"),
    ],
)
@scenario("hsem.feature", "Malformed and out-of-range commands are refused")
def test_errors(line, reason):
    pass


@given("the semaphore of the board file is free", target_fixture="semaphore")
def semaphore_free(fw, hsem_cfg):
    number = hsem_cfg["semaphore"]
    assert not fw.hsem.status(number).locked, f"semaphore {number} is held before the test"
    return number


@when(parsers.parse("process {procid:d} takes the semaphore"))
def take(fw, semaphore, procid):
    fw.hsem.take(semaphore, procid=procid)


@when(parsers.parse("process {procid:d} takes the semaphore for {hold:d} us"))
def take_with_hold(fw, semaphore, procid, hold):
    fw.hsem.take(semaphore, procid=procid, hold=hold)


@when(parsers.parse("process {procid:d} releases the semaphore"))
@then(parsers.parse("process {procid:d} releases the semaphore"))
def release(fw, semaphore, procid):
    fw.hsem.release(semaphore, procid=procid)


@when(parsers.parse("process {procid:d} releases the semaphore it does not hold"))
def release_foreign(fw, semaphore, procid):
    fw.terminal.command(f"hsem.release {semaphore} procid={procid}")


@when(parsers.parse("the host waits {seconds:g} s"))
def host_waits(seconds):
    time.sleep(seconds)


@when("the semaphore is locked while process 1 holds it for the hold time", target_fixture="waited")
def lock_with_hold(fw, semaphore, hold_us):
    return fw.hsem.lock(semaphore, hold=hold_us)


@when("the semaphore is locked", target_fixture="waited")
def lock(fw, semaphore):
    return fw.hsem.lock(semaphore)


@then(parsers.parse("the semaphore is locked by this core for process {procid:d}"))
def locked_by(fw, hsem_cfg, semaphore, procid):
    status = fw.hsem.status(semaphore)
    assert (status.locked, status.core, status.procid) == (True, hsem_cfg["core"], procid)


@then("the semaphore is locked by the current core")
def mine(fw, semaphore):
    assert fw.hsem.mine(semaphore)


@then("the semaphore is not locked by the current core")
def not_mine(fw, semaphore):
    assert not fw.hsem.mine(semaphore)


@then("the semaphore is free")
def free(fw, semaphore):
    assert not fw.hsem.status(semaphore).locked


@then("the semaphore is locked")
def locked(fw, semaphore):
    assert fw.hsem.status(semaphore).locked


@then(parsers.parse("the semaphore is held by process {procid:d}"))
def held_by(fw, semaphore, procid):
    assert fw.hsem.status(semaphore).procid == procid


@then(parsers.parse('taking the semaphore for process {procid:d} fails with "{reason}"'))
def take_refused(fw, semaphore, procid, reason):
    expect_reason(fw, f"hsem.take {semaphore} procid={procid}", reason)


@then(parsers.parse('locking the semaphore fails with "{reason}"'))
def lock_refused(fw, semaphore, reason):
    expect_reason(fw, f"hsem.lock {semaphore}", reason)


@then(parsers.parse('locking the semaphore with a hold of {hold:d} us fails with "{reason}"'))
def lock_with_hold_refused(fw, semaphore, hold, reason):
    expect_reason(fw, f"hsem.lock {semaphore} hold={hold}", reason)


@then("the lock waited the hold time within the tolerance and the free wait")
def waited_the_hold(hsem_cfg, hold_us, waited):
    tolerance = hsem_cfg["tolerance"]
    assert hold_us * (1 - tolerance) - hsem_cfg["free_wait_max_us"] <= waited <= hold_us * (1 + tolerance) + hsem_cfg["free_wait_max_us"]


@then("the lock waited no longer than the free wait")
def returned_at_once(hsem_cfg, waited):
    assert waited <= hsem_cfg["free_wait_max_us"]


@then("the command line fails with the reason")
def command_refused(fw, line, reason):
    expect_reason(fw, line, reason)
