import time

from ad3_waveforms_bench.terminal import FirmwareTerminal

from hal_st_validation.firmware import quiesce


class SlowLink:
    """A serial link that answers every command after `delay` seconds and can deliver a stray reply late."""

    def __init__(self, delay: float) -> None:
        self.delay = delay
        self.scheduled: list[tuple[float, bytes]] = []

    def later(self, data: bytes, delay: float) -> None:
        self.scheduled.append((time.monotonic() + delay, data))

    def _due(self) -> bytes:
        now = time.monotonic()
        ready = [data for at, data in self.scheduled if at <= now]
        self.scheduled = [(at, data) for at, data in self.scheduled if at > now]
        return b"".join(ready)

    @property
    def in_waiting(self) -> int:
        return 0

    def read(self, size: int = 1) -> bytes:
        data = self._due()
        if not data:
            time.sleep(0.005)
        return data

    def write(self, data: bytes) -> int:
        line = data.decode().strip("\r\x03")
        if line:
            reply = {"ping": b"OK\r\n> ", "info": b"OK board=NUCLEO-WB55RG\r\n> "}.get(line, b"ERR usage\r\n> ")
            self.later(reply, self.delay)
        return len(data)

    def close(self) -> None:
        pass


def test_late_stray_reply_is_dropped_after_quiesce():
    link = SlowLink(delay=0.02)
    terminal = FirmwareTerminal(serial=link, timeout=1.0)
    terminal.sync(timeout=1.0)
    link.later(b"ERR usage\r\n> ", 0.15)
    assert quiesce(terminal, quiet=0.3)
    assert terminal.command("info")["board"] == "NUCLEO-WB55RG"


def test_without_quiesce_a_late_reply_is_taken_by_the_next_command():
    """The failure seen through port-bridge: the stray reply lands on the next command."""
    link = SlowLink(delay=0.2)
    terminal = FirmwareTerminal(serial=link, timeout=1.0)
    terminal.sync(timeout=1.0)
    link.later(b"ERR usage\r\n> ", 0.05)
    response = terminal.command("info", check=False)
    assert not response.ok and response.reason == "usage"
