"""Fake `spis` group: the argument checks of validation/firmware/SpiSlaveGroup.cpp in their order, the pins, the
`ResourceAllocation` spi instance and slave DMA channels it holds (WB55 DMA2 1/2, WBA55 GPDMA1 8/7, shared with
`dma.wave` and the ADC), and the arm/result state of `hal::SpiSlaveStmDma`.

Nothing clocks the slave without the AD3, except the SPI loop: `FakeFirmware.spi_loop`, a mapping {master instance:
slave instance} (default: the two SPI instances of the MCU on each other, the `--with spiloop` wiring), lets
`spi.xfer` on an open master clock an open `spis` instance (`FakeSpiLoop` serves `spi.xfer` for that; the other
`spi.*` commands stay with `FakeFirmware`). Each byte the master clocks goes into the armed transfer (MOSI) and
takes the slave's next byte (MISO, zero for a receive-only transfer, whose DMA dummy the firmware does not
define); bytes beyond the armed length, or with no transfer armed, read `FakeFirmware.spi_miso`. The master's
`bits`, `lsb` and `mode` are not modelled. `spis.result` answers synchronously (a not yet done transfer sleeps
`wait`), so a second `spis.result` while one waits cannot happen here.
"""

from __future__ import annotations

from typing import Any

from ..groups.spis import SPIS_BUFFER, SPIS_HEX_MAX, SPIS_WAIT_DEFAULT_MS, SPIS_WAIT_MAX_MS
from ..patterns import PATTERNS, SEED_MAX, crc_text, generate
from .base import FakeGroup, _choice, _fail, _flag, _hex, _number, _shape

__all__ = ["FakeSpiLoop", "FakeSpiSlave"]

_INSTANCES = 4
_MASTER_CAPACITY = 64
_OPEN_KEYS = ("clk", "miso", "mosi", "nss")
_ARM_KEYS = ("rx", "len", "pattern", "seed")
_RESULT_KEYS = ("wait", "out")
_FUNCTIONS = (("spiClock", "clk"), ("spiMiso", "miso"), ("spiMosi", "mosi"), ("spiSlaveSelect", "nss"))
# `board::spiSlaveDma`: (transmit, receive) as `ResourceAllocation` keys.
_SLAVE_DMA = {"stm32wb55": (("dma2", 1), ("dma2", 2)), "stm32wba55": (("dma1", 8), ("dma1", 7))}


def _payload(args: list[str], options: dict[str, str], position: int, capacity: int) -> bytes:
    """`ParsePayload`: hex, or `-` with `len`/`pattern`/`seed`; `-` alone is empty."""
    text = args[position]
    generated = "len" in options
    patterned = "pattern" in options or "seed" in options
    if text != "-":
        if generated or patterned:
            _fail("usage")
        return _hex(text, capacity)
    if not generated:
        if patterned:
            _fail("usage")
        return b""
    pattern = _choice(options, "pattern", PATTERNS, "inc")
    seed = _number(options.get("seed", "0"), 0, SEED_MAX)
    length = _number(options["len"], 1, capacity)
    return generate(length, pattern, seed)


class FakeSpiSlave(FakeGroup):
    prefix = "spis"

    def _key(self, args: list[str]) -> str:
        return str(_number(args[0], 0, _INSTANCES - 1))

    def _find(self, args: list[str], options: dict[str, str], low: int, high: int, keys: tuple[str, ...] = ()) -> dict[str, Any]:
        _shape(args, options, low, high, keys)
        return self.fw.find_instance(self.prefix, self._key(args))

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, _OPEN_KEYS)
        index = _number(args[0], 0, _INSTANCES - 1)
        if index not in self.fw.spec.spis:
            _fail("range")
        pins = {key: self.fw.pin(options.get(key)) for key in _OPEN_KEYS}
        if any(pin is None for pin in pins.values()):
            _fail("usage")
        for function, key in _FUNCTIONS:
            self.fw.check_function(function, index, pins[key])
        state = {"index": index, "armed": False, "done": False, "tx": b"", "rx": 0, "received": bytearray(), "clocked": 0}
        resources = [("spi", index), *_SLAVE_DMA[self.fw.family]]
        self.fw.open_instance(self.prefix, str(index), state, pins.values(), resources=resources)
        return "OK"

    def cmd_arm(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, 2, 2, _ARM_KEYS)
        send = _payload(args, options, 1, SPIS_BUFFER)
        receive = _number(options.get("rx", str(len(send))), 0, SPIS_BUFFER)
        if (not send and receive == 0) or (send and receive not in (0, len(send))):
            _fail("usage")
        if state["armed"]:
            _fail("busy")
        state.update(armed=True, done=False, tx=send, rx=receive, received=bytearray(), clocked=0)
        return "OK"

    def cmd_result(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, 1, 1, _RESULT_KEYS)
        output = _choice(options, "out", ("hex", "crc"), "hex")
        wait = _number(options.get("wait", str(SPIS_WAIT_DEFAULT_MS)), 0, SPIS_WAIT_MAX_MS)
        if output == "hex" and state["rx"] > SPIS_HEX_MAX:
            _fail("range")
        if state["done"]:
            received = bytes(state["received"])
            if output == "crc":
                return f"OK done=1 len={len(received)} crc={crc_text(received)}"
            return f"OK done=1 rx={received.hex()}"
        if state["armed"]:
            self.fw.sleep(wait / 1000)
        return "OK done=0"

    def cmd_cancel(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, 1, 1)
        cancelled = state["armed"]
        state.update(armed=False, done=False)
        return f"OK cancelled={int(cancelled)}"

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key = self._key(args)
        self.fw.find_instance(self.prefix, key)
        return self.fw.close_instance(self.prefix, key)

    def clock(self, index: int, mosi: bytes) -> bytes | None:
        """A master clocks `mosi` into the `spis` instance `index`: its MISO bytes, or None when it is not open."""
        state = self.fw.opened.get((self.prefix, str(index)))
        if state is None:
            return None
        miso = bytearray()
        for byte in mosi:
            if not state["armed"]:
                miso.append(self.fw.spi_miso)
                continue
            position = state["clocked"]
            length = max(len(state["tx"]), state["rx"])
            miso.append(state["tx"][position] if state["tx"] else 0x00)
            if position < state["rx"]:
                state["received"].append(byte)
            state["clocked"] = position + 1
            if state["clocked"] == length:
                state.update(armed=False, done=True)
        return bytes(miso)


class FakeSpiLoop(FakeGroup):
    """`spi.xfer` of the SPI master with the loop to `spis`; the checks are those of EMIL's `HilSpiCommands`."""

    prefix = "spi"

    def loop(self) -> dict[int, int]:
        """`FakeFirmware.spi_loop`, or the two SPI instances of the MCU wired to each other."""
        loop = getattr(self.fw, "spi_loop", None)
        if loop is not None:
            return dict(loop)
        first, second = sorted(self.fw.spec.spis)
        return {first: second, second: first}

    def cmd_xfer(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 2, 2, ("rx", "continue"))
        index = _number(args[0], 0, _INSTANCES - 1)
        self.fw.find_instance("spi", str(index))
        data = _hex(args[1], _MASTER_CAPACITY)
        size = _number(options.get("rx", str(len(data))), 0, _MASTER_CAPACITY)
        _flag(options, "continue")
        length = max(len(data), size)
        if length == 0:
            _fail("usage")
        target = self.loop().get(index)
        slave = self.fw.group(FakeSpiSlave.prefix)
        miso = None
        if target is not None and isinstance(slave, FakeSpiSlave):
            miso = slave.clock(target, data + bytes(length - len(data)))
        if miso is None:
            miso = bytes([self.fw.spi_miso]) * length
        return f"OK rx={miso[:size].hex() or '-'}"
