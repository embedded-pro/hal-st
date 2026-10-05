@family:stm32wb55
Feature: Hardware semaphores
  Hardware semaphores of the STM32WB55 through the `hsem` group: two-step locks per process (`HAL_HSEM_Take`/
  `HAL_HSEM_Release`), the lock state read from R (never RLR, whose read is itself a lock attempt), and
  `hal::SynchronousHardwareSemaphoreStm` over `hal::SynchronousHardwareSemaphoreMasterStm`.

  `hsem.lock <n> hold=<us>` takes the semaphore for process 1 and lets the scaffold timer (TIM17) free it from its
  interrupt after `hold`: the synchronous lock must wait that long (B.9: `WaitLock` used to return at once), and the
  query `IsLockedByCurrentCore` must not lock a free semaphore. No wiring. The semaphore, the core, the holds and
  the tolerances are in tests.hsem of the board file.

  Scenario: A two-step lock shows this core and the process, and release frees it
    Given the semaphore of the board file is free
    When process 3 takes the semaphore
    Then the semaphore is locked by this core for process 3
    And the semaphore is locked by the current core
    When process 3 releases the semaphore
    Then the semaphore is free

  Scenario: A semaphore held by one process is refused to another
    A release by the wrong process changes nothing.
    Given the semaphore of the board file is free
    When process 3 takes the semaphore
    Then taking the semaphore for process 4 fails with "busy"
    When process 4 releases the semaphore it does not hold
    Then the semaphore is held by process 3
    And process 3 releases the semaphore

  Scenario: hold= frees the semaphore from an EMIL timer
    Given the semaphore of the board file is free
    When process 2 takes the semaphore for 100 us
    Then the semaphore is locked
    When the host waits 0.3 s
    Then the semaphore is free

  Scenario: The synchronous lock waits until process 1 frees the semaphore
    `SynchronousHardwareSemaphoreStm` waits until process 1 frees the semaphore from the timer interrupt, and
    leaves it free afterwards (B.9).
    Given the semaphore of the board file is free
    When the semaphore is locked while process 1 holds it for the hold time
    Then the lock waited the hold time within the tolerance and the free wait
    And the semaphore is free

  Scenario: The synchronous lock of a free semaphore returns at once
    Given the semaphore of the board file is free
    When the semaphore is locked
    Then the lock waited no longer than the free wait
    And the semaphore is free

  Scenario: The synchronous lock refuses a semaphore held by another process
    Nothing could free a semaphore held by another process while the lock blocks the event loop: `ERR busy`.
    Given the semaphore of the board file is free
    When process 5 takes the semaphore
    Then locking the semaphore fails with "busy"
    And locking the semaphore with a hold of 1000 us fails with "busy"
    And process 5 releases the semaphore

  Scenario: The ownership query takes no lock
    `IsLockedByCurrentCore` reads R: a free semaphore is not ours and stays free (B.9).
    Given the semaphore of the board file is free
    Then the semaphore is not locked by the current core
    And the semaphore is free
    When process 7 takes the semaphore
    Then the semaphore is locked by the current core
    When process 7 releases the semaphore
    Then the semaphore is not locked by the current core

  Scenario: Malformed and out-of-range commands are refused
    Then the command line fails with the reason
