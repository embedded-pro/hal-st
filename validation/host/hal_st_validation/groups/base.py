"""`Group`: the base of the typed command wrappers (`fw.<prefix>.<command>(...)`)."""

from __future__ import annotations

from collections.abc import Sequence
from typing import TYPE_CHECKING

from ad3_waveforms_bench.protocol import Response, format_command
from ad3_waveforms_bench.terminal import PendingCommand

if TYPE_CHECKING:
    from ..firmware import Firmware

Pin = str


class Group:
    """Commands `<prefix>.<name>`; keyword arguments map 1:1 to protocol options (None leaves one out). Pins are
    sent as `P<port><index>` after resolving the firmware's aliases."""

    prefix = ""

    def __init__(self, firmware: Firmware) -> None:
        self._fw = firmware

    def _cmd(self, name: str, *args: object, cmd_timeout: float | None = None, **options: object) -> Response:
        return self._fw.command(f"{self.prefix}.{name}", *args, cmd_timeout=cmd_timeout, **options)

    def begin(self, name: str, *args: object, cmd_timeout: float | None = None, **options: object) -> PendingCommand:
        """Send `<prefix>.<name>` without waiting for its final line (`settle()` finishes it whatever happens)."""
        return self._fw.terminal.begin(format_command(f"{self.prefix}.{name}", *args, **options), timeout=cmd_timeout)

    def _pin(self, pin: Pin | None) -> str | None:
        return None if pin is None else self._fw.pin(pin)

    def _pins(self, pins: Sequence[Pin] | None) -> list[str] | None:
        return None if pins is None else [self._fw.pin(pin) for pin in pins]
