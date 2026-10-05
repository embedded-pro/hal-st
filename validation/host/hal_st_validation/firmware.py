"""Typed API over the validation firmware commands (validation/PROTOCOL.md); keyword arguments map 1:1 to
protocol options."""

from __future__ import annotations

import time
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from typing import Literal

from ad3_waveforms_bench.protocol import Event, Response, format_command, format_decimal
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal, PendingCommand, TerminalError

from .protocol import normalize_pin, parse_pin_map

Pin = str
Level = Literal[0, 1]
Edge = Literal["rising", "falling", "both", "off"]
Drive = Literal["low", "medium", "fast", "high"]
PwmOutput = Pin | None | tuple[Pin | None, Pin | None]


@dataclass(frozen=True)
class BootInfo:
    board: str
    family: str
    sysclk: int
    reset: str
    raw: str

    @classmethod
    def from_values(cls, source: Response | Event) -> BootInfo:
        return cls(
            board=source["board"],
            family=source["family"],
            sysclk=source.as_int("sysclk"),
            reset=source["reset"],
            raw=source.raw,
        )


@dataclass(frozen=True)
class Info(BootInfo):
    uid: str | None = None


@dataclass(frozen=True)
class QeiReading:
    pos: int
    dir: str
    speed: int
    res: int


def settle(pending: PendingCommand, timeout: float | None = None) -> Response | None:
    """Finish a command started with `FirmwareTerminal.begin` whatever its outcome, so a failing test cannot
    leave it pending; returns its final line, or None when it timed out or was already finished."""
    try:
        return pending.wait(timeout, check=False)
    except TerminalError:
        return None


def quiesce(terminal: FirmwareTerminal, quiet: float = 0.1, limit: float = 3.0) -> bool:
    """Read until the line has been silent for `quiet` seconds (at most `limit`), so a late reply to an earlier
    command is read now and dropped by the next `begin` instead of being taken as that command's reply."""
    deadline = time.monotonic() + limit
    while time.monotonic() < deadline:
        if terminal.pump(quiet) == 0:
            return True
    return False


class _Group:
    prefix = ""

    def __init__(self, firmware: Firmware) -> None:
        self._fw = firmware

    def _cmd(self, name: str, *args: object, cmd_timeout: float | None = None, **options: object) -> Response:
        return self._fw.command(f"{self.prefix}.{name}", *args, cmd_timeout=cmd_timeout, **options)

    def _pin(self, pin: Pin | None) -> str | None:
        return None if pin is None else self._fw.pin(pin)

    def _pins(self, pins: Sequence[Pin] | None) -> list[str] | None:
        return None if pins is None else [self._fw.pin(pin) for pin in pins]


class System(_Group):
    def ping(self) -> None:
        self._fw.command("ping")

    def info(self) -> Info:
        response = self._fw.command("info")
        uid = response.get("uid")
        base = BootInfo.from_values(response)
        return Info(**base.__dict__, uid=None if uid in (None, "none") else uid)

    def pins(self) -> dict[str, str]:
        return parse_pin_map(self._fw.command("board.pins"))

    def delay(self, ms: int) -> None:
        self._fw.command("delay", ms, cmd_timeout=self._fw.terminal.timeout + ms / 1000)

    def reset(self, timeout: float = 5.0) -> BootInfo:
        terminal = self._fw.terminal
        terminal.drain_events("boot")
        terminal.send_nowait("reset")
        self._fw.forget_open()
        return BootInfo.from_values(terminal.wait_boot(timeout))

    def wait_boot(self, timeout: float = 5.0) -> BootInfo:
        self._fw.forget_open()
        return BootInfo.from_values(self._fw.terminal.wait_boot(timeout))


class Gpio(_Group):
    prefix = "gpio"

    def cfg(self, pin: Pin, mode: Literal["in", "out", "od"], pull: str | None = None, drive: Drive | None = None) -> None:
        """`drive` is the output speed (`hal::Speed`) of the pin."""
        pin = self._fw.pin(pin)
        self._cmd("cfg", pin, mode, pull=pull, drive=drive)
        self._fw.track(("gpio", pin), "gpio.release", pin)

    def set(self, pin: Pin, value: int | bool) -> None:
        self._cmd("set", self._fw.pin(pin), int(bool(value)))

    def get(self, pin: Pin) -> int:
        return self._cmd("get", self._fw.pin(pin)).as_int("value")

    def pulse(self, pin: Pin, count: int, period_ms: int, cmd_timeout: float | None = None) -> None:
        duration = count * period_ms / 1000
        self._cmd("pulse", self._fw.pin(pin), count, period_ms, cmd_timeout=cmd_timeout or self._fw.terminal.timeout + duration * 1.5)

    def irq(self, pin: Pin, edge: Edge, type: Literal["immediate", "dispatched"] | None = None) -> None:
        self._cmd("irq", self._fw.pin(pin), edge, type=type)

    def count(self, pin: Pin, clear: bool | None = None) -> int:
        return self._cmd("count", self._fw.pin(pin), clear=clear).as_int("count")

    def release(self, pin: Pin) -> None:
        pin = self._fw.pin(pin)
        self._cmd("release", pin)
        self._fw.untrack(("gpio", pin))


class Pwm(_Group):
    prefix = "pwm"

    def open(
        self,
        timer: int,
        channels: Sequence[int] | None = None,
        pins: Sequence[PwmOutput] | None = None,
        freq: int | None = None,
        mode: Literal["edge", "center"] | None = None,
        prescaler: int | None = None,
        dead: int | Literal["off"] | None = None,
        inv: bool | None = None,
        invn: bool | None = None,
        idle: bool | None = None,
        idlen: bool | None = None,
        brk: Pin | None = None,
        brkpol: Literal["low", "high"] | None = None,
        brkauto: bool | None = None,
        sync: bool | None = None,
    ) -> int:
        """Returns `pwmclk`. A `pins` entry is the channel output, `(output, complementary)` or None (`-`); a None
        inside the tuple leaves that position unused (`-:PA7` drives only CH1N)."""
        response = self._cmd(
            "open",
            timer,
            channels=list(channels) if channels is not None else None,
            pins=None if pins is None else [self._output(entry) for entry in pins],
            freq=freq,
            mode=mode,
            prescaler=prescaler,
            dead=dead,
            inv=inv,
            invn=invn,
            idle=idle,
            idlen=idlen,
            brk=self._pin(brk),
            brkpol=brkpol,
            brkauto=brkauto,
            sync=sync,
        )
        self._fw.track(("pwm", timer), "pwm.close", timer)
        return response.as_int("pwmclk")

    def _output(self, entry: PwmOutput) -> str:
        if isinstance(entry, tuple):
            first, second = entry
            return f"{self._pin_or_dash(first)}:{self._pin_or_dash(second)}"
        return self._pin_or_dash(entry)

    def _pin_or_dash(self, pin: Pin | None) -> str:
        return "-" if pin is None else self._fw.pin(pin)

    def duty(self, timer: int, *duties: float) -> None:
        """One duty per opened channel (in channel order), or a single duty for all of them; up to 4 decimals."""
        if not 1 <= len(duties) <= 4:
            raise ValueError("one to four duties")
        self._cmd("duty", timer, *[format_decimal(float(duty), 4) for duty in duties])

    def freq(self, timer: int, hz: int) -> None:
        self._cmd("freq", timer, hz)

    def stop(self, timer: int) -> None:
        self._cmd("stop", timer)

    def close(self, timer: int) -> None:
        self._cmd("close", timer)
        self._fw.untrack(("pwm", timer))


class Uart(_Group):
    prefix = "uart"

    def open(
        self,
        index: int,
        lp: bool | None = None,
        tx: Pin | None = None,
        rx: Pin | None = None,
        rts: Pin | None = None,
        cts: Pin | None = None,
        baud: int | None = None,
        parity: Literal["none", "even", "odd"] | None = None,
        flow: Literal["none", "rts", "cts", "rtscts"] | None = None,
        swap: bool | None = None,
        dma: bool | None = None,
        duplex: bool | None = None,
        sync: bool | None = None,
    ) -> None:
        """`lp=True` selects LPUART<index>; the group keeps one instance, addressed by `index` afterwards."""
        self._cmd(
            "open",
            index,
            lp=lp,
            tx=self._pin(tx),
            rx=self._pin(rx),
            rts=self._pin(rts),
            cts=self._pin(cts),
            baud=baud,
            parity=parity,
            flow=flow,
            swap=swap,
            dma=dma,
            duplex=duplex,
            sync=sync,
        )
        self._fw.track(("uart", index), "uart.close", index)

    def send(self, index: int, data: bytes, cmd_timeout: float | None = None) -> None:
        if not data:
            raise ValueError("uart.send needs at least one byte")
        self._cmd("send", index, bytes(data), cmd_timeout=cmd_timeout)

    def recv(self, index: int, timeout: int | None = None, len: int | None = None) -> bytes:
        wait = self._fw.terminal.timeout + (timeout or 0) / 1000
        return self._cmd("recv", index, timeout=timeout, len=len, cmd_timeout=wait).as_bytes("data")

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack(("uart", index))


class Spi(_Group):
    prefix = "spi"

    def open(
        self,
        index: int,
        clk: Pin | None = None,
        mosi: Pin | None = None,
        miso: Pin | None = None,
        cs: Pin | None = None,
        baud: int | None = None,
        mode: int | None = None,
        dma: bool | None = None,
        sync: bool | None = None,
    ) -> None:
        self._cmd(
            "open",
            index,
            clk=self._pin(clk),
            mosi=self._pin(mosi),
            miso=self._pin(miso),
            cs=self._pin(cs),
            baud=baud,
            mode=mode,
            dma=dma,
            sync=sync,
        )
        self._fw.track(("spi", index), "spi.close", index)

    def xfer(self, index: int, tx: bytes, rx: int | None = None, continue_: bool | None = None) -> bytes:
        return self._cmd("xfer", index, bytes(tx), rx=rx, continue_=continue_).as_bytes("rx")

    def close(self, index: int) -> None:
        self._cmd("close", index)
        self._fw.untrack(("spi", index))


class Adc(_Group):
    prefix = "adc"

    def open(
        self,
        adc: int,
        pins: Sequence[Pin] | None = None,
        sampling: str | float | None = None,
        timer: int | None = None,
        rate: int | None = None,
    ) -> None:
        """`sampling` in ADC clock cycles (`2.5`, `"640.5"`); `timer` triggers the runs at `rate` per second."""
        self._cmd("open", adc, pins=self._pins(pins), sampling=sampling, timer=timer, rate=rate)
        self._fw.track(("adc", adc), "adc.close", adc)

    def measure(self, adc: int, n: int | None = None, cmd_timeout: float | None = None) -> list[int]:
        return self._cmd("measure", adc, n=n, cmd_timeout=cmd_timeout).as_ints("samples")

    def close(self, adc: int) -> None:
        self._cmd("close", adc)
        self._fw.untrack(("adc", adc))


class Qei(_Group):
    prefix = "qei"

    def open(
        self,
        timer: int,
        lp: bool | None = None,
        a: Pin | None = None,
        b: Pin | None = None,
        idx: Pin | None = None,
        res: int | None = None,
        offset: int | None = None,
        inva: bool | None = None,
        invb: bool | None = None,
        cap: Literal["a", "b", "ab"] | None = None,
        filter: int | None = None,
        vel: int | Literal["off"] | None = None,
    ) -> None:
        """`lp=True` selects LPTIM<timer>; `vel` is the speed sampling period in µs."""
        self._cmd(
            "open",
            timer,
            lp=lp,
            a=self._pin(a),
            b=self._pin(b),
            idx=self._pin(idx),
            res=res,
            offset=offset,
            inva=inva,
            invb=invb,
            cap=cap,
            filter=filter,
            vel=vel,
        )
        self._fw.track(("qei", timer), "qei.close", timer)

    def read(self, timer: int) -> QeiReading:
        response = self._cmd("read", timer)
        return QeiReading(
            pos=response.as_int("pos"),
            dir=response["dir"],
            speed=response.as_int("speed"),
            res=response.as_int("res"),
        )

    def index(self, timer: int) -> int:
        """Level of the index input."""
        return self._cmd("index", timer).as_int("idx")

    def close(self, timer: int) -> None:
        self._cmd("close", timer)
        self._fw.untrack(("qei", timer))


class Watchdog(_Group):
    prefix = "wdt"

    def start(self, index: int, timeout: int, feed: Literal["auto", "manual"] | None = None, pin: Pin | None = None) -> None:
        """The WWDG cannot be stopped; `pin` toggles on every early warning and stays claimed until reset."""
        self._cmd("start", index, timeout=timeout, feed=feed, pin=self._pin(pin))

    def feed(self, index: int) -> None:
        self._cmd("feed", index)

    def wait_warning(self, index: int, timeout: float) -> Event:
        return self._fw.terminal.wait_event("wdt", lambda event: event.as_int("index") == index, timeout)

    def warnings(self, index: int) -> list[Event]:
        return [event for event in self._fw.terminal.drain_events("wdt") if event.as_int("index") == index]


class Firmware:
    """Entry point: `fw.gpio.set("led0", 1)`, `fw.pwm.open(1, channels=[1], freq=20000)`, ...

    Open instances are tracked so `close_all()` can restore a clean state between tests.
    Pins are sent as `P<port><index>` after resolving aliases with `aliases` (when given).
    """

    def __init__(self, terminal: FirmwareTerminal, aliases: Mapping[str, str] | None = None) -> None:
        self.terminal = terminal
        self.aliases = dict(aliases or {})
        self._open: dict[tuple[object, ...], tuple[str, tuple[object, ...]]] = {}
        self.system = System(self)
        self.gpio = Gpio(self)
        self.pwm = Pwm(self)
        self.uart = Uart(self)
        self.spi = Spi(self)
        self.adc = Adc(self)
        self.qei = Qei(self)
        self.wdt = Watchdog(self)

    def command(self, name: str, *args: object, cmd_timeout: float | None = None, **options: object) -> Response:
        return self.terminal.command(format_command(name, *args, **options), timeout=cmd_timeout)

    def pin(self, pin: Pin) -> str:
        return normalize_pin(pin, self.aliases or None)

    def track(self, key: tuple[object, ...], close_command: str, *args: object) -> None:
        self._open[key] = (close_command, args)

    def untrack(self, key: tuple[object, ...]) -> None:
        self._open.pop(key, None)

    def forget_open(self) -> None:
        self._open.clear()

    @property
    def open_instances(self) -> list[tuple[object, ...]]:
        return list(self._open)

    def close_all(self) -> list[str]:
        """Close everything opened through this object; returns the commands that failed."""
        failures: list[str] = []
        for key, (name, args) in reversed(list(self._open.items())):
            try:
                self.command(name, *args)
            except FirmwareError as error:
                if error.reason != "notopen":
                    failures.append(f"{error.command}: {error.reason}")
            self._open.pop(key, None)
        return failures
