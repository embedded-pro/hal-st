"""Hardware semaphores of the STM32WB55 through the `hsem` group: two-step locks per process (`HAL_HSEM_Take`/
`HAL_HSEM_Release`), the lock state read from R (never RLR, whose read is itself a lock attempt), and
`hal::SynchronousHardwareSemaphoreStm` over `hal::SynchronousHardwareSemaphoreMasterStm`.

`hsem.lock <n> hold=<us>` takes the semaphore for process 1 and lets the scaffold timer (TIM17) free it from its
interrupt after `hold`: the synchronous lock must wait that long (B.9: `WaitLock` used to return at once), and the
query `IsLockedByCurrentCore` must not lock a free semaphore. No wiring.
"""

from __future__ import annotations

import time

import pytest
from ad3_waveforms_bench.terminal import FirmwareError

pytestmark = pytest.mark.family("stm32wb55")


@pytest.fixture
def hsem_cfg(board_cfg):
    return board_cfg.param("hsem")


@pytest.fixture
def semaphore(fw, hsem_cfg):
    number = hsem_cfg["semaphore"]
    assert not fw.hsem.status(number).locked, f"semaphore {number} is held before the test"
    yield number


def expect_reason(fw, line: str, reason: str) -> None:
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason, line


def test_take_and_release(fw, hsem_cfg, semaphore):
    """A two-step lock shows this core and the process in R; release frees it."""
    fw.hsem.take(semaphore, procid=3)
    status = fw.hsem.status(semaphore)
    assert (status.locked, status.core, status.procid) == (True, hsem_cfg["core"], 3)
    assert fw.hsem.mine(semaphore)
    fw.hsem.release(semaphore, procid=3)
    assert not fw.hsem.status(semaphore).locked


def test_other_process_is_refused(fw, semaphore):
    """A semaphore held by one process cannot be taken by another; a release by the wrong process changes nothing."""
    fw.hsem.take(semaphore, procid=3)
    expect_reason(fw, f"hsem.take {semaphore} procid=4", "busy")
    fw.terminal.command(f"hsem.release {semaphore} procid=4")
    assert fw.hsem.status(semaphore).procid == 3
    fw.hsem.release(semaphore, procid=3)


def test_hold_frees_the_semaphore(fw, semaphore):
    """`hold=` frees the semaphore from an EMIL timer."""
    fw.hsem.take(semaphore, procid=2, hold=100)
    assert fw.hsem.status(semaphore).locked
    time.sleep(0.3)
    assert not fw.hsem.status(semaphore).locked


@pytest.mark.board_params("hold_us", "hsem.holds_us")
def test_lock_waits_for_foreign_owner(fw, hsem_cfg, semaphore, hold_us):
    """`SynchronousHardwareSemaphoreStm` waits until process 1 frees the semaphore from the timer interrupt, and
    leaves it free afterwards (B.9)."""
    waited = fw.hsem.lock(semaphore, hold=hold_us)
    tolerance = hsem_cfg["tolerance"]
    assert hold_us * (1 - tolerance) - hsem_cfg["free_wait_max_us"] <= waited <= hold_us * (1 + tolerance) + hsem_cfg["free_wait_max_us"]
    assert not fw.hsem.status(semaphore).locked


def test_lock_of_free_semaphore_returns_at_once(fw, hsem_cfg, semaphore):
    assert fw.hsem.lock(semaphore) <= hsem_cfg["free_wait_max_us"]
    assert not fw.hsem.status(semaphore).locked


def test_lock_refuses_held_semaphore(fw, semaphore):
    """Nothing could free a semaphore held by another process while the lock blocks the event loop: `ERR busy`."""
    fw.hsem.take(semaphore, procid=5)
    expect_reason(fw, f"hsem.lock {semaphore}", "busy")
    expect_reason(fw, f"hsem.lock {semaphore} hold=1000", "busy")
    fw.hsem.release(semaphore, procid=5)


def test_query_takes_no_lock(fw, semaphore):
    """`IsLockedByCurrentCore` reads R: a free semaphore is not ours and stays free (B.9)."""
    assert not fw.hsem.mine(semaphore)
    assert not fw.hsem.status(semaphore).locked
    fw.hsem.take(semaphore, procid=7)
    assert fw.hsem.mine(semaphore)
    fw.hsem.release(semaphore, procid=7)
    assert not fw.hsem.mine(semaphore)


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
def test_errors(fw, line, reason):
    expect_reason(fw, line, reason)
