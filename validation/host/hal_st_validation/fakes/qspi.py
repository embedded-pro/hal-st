"""Fake `qspi` group (STM32WB55): the argument checks of validation/firmware/QuadSpiGroup.cpp in their order, the
QUADSPI pins and the DMA2 channel 3 it claims, and a bus whose IO lines rest at the levels of `io` (IO0 in bit 0;
the AD3 is not modelled): reads return those levels, a poll matches them or times out. The STM32WBA55 has no
QUADSPI; its `qspi.*` names are unsupported commands of the fake firmware, which answer before this group.

`variant=poll` blocks for good on a status that never matches (`QuadSpiStm` waits with `HAL_MAX_DELAY`, a known
gap); the fake answers `ERR timeout` for it as for `variant=dma`, which is what test_qspi.py expects."""

from __future__ import annotations

from typing import Any

from ..groups.qspi import (
    QSPI_BUFFER,
    QSPI_DUMMY_MAX,
    QSPI_HEX_MAX,
    QSPI_INDEX,
    QSPI_PRESCALER_MAX,
    QSPI_REPEAT_MAX,
    QSPI_SIZE_MAX,
    QSPI_TIMEOUT_S,
    qspi_clock,
)
from ..patterns import PATTERNS, SEED_MAX, crc_text, generate
from .base import UINT32_MAX, FakeGroup, _choice, _fail, _hex, _number, _shape

_VARIANTS = ("poll", "dma", "spi")
_OUTPUTS = ("hex", "crc")
# `board::qspiPins` with the pin functions `QuadSpiStm` claims (instance 0 in the pinout table).
_PINS = (
    ("quadSpiClock", "PA3"),
    ("quadSpiSlaveSelect", "PA2"),
    ("quadSpiData0", "PB9"),
    ("quadSpiData1", "PB8"),
    ("quadSpiData2", "PA7"),
    ("quadSpiData3", "PA6"),
)
_DMA = ("dma2", 3)
_PHASE_KEYS = ("instr", "addr", "abytes", "alt", "altbytes", "dummy", "lines")
_COMMAND_KEYS = (*_PHASE_KEYS, "tx", "len", "pattern", "seed", "rx", "out", "repeat")
_POLL_KEYS = (*_PHASE_KEYS, "match", "mask", "size")
_TRANSFER_KEYS = ("len", "pattern", "seed", "rx", "repeat")
_PHASE_BYTES_MAX = 4
_IDLE_LEVELS = 0xF


def _fits(value: int, size: int) -> bool:
    return size >= _PHASE_BYTES_MAX or value < 1 << (8 * size)


def _phases_shape(options: dict[str, str]) -> bool:
    return ("addr" in options or "abytes" not in options) and ("alt" in options or "altbytes" not in options)


class FakeQuadSpi(FakeGroup):
    prefix = "qspi"
    io = _IDLE_LEVELS

    def boot(self) -> None:
        self.io = _IDLE_LEVELS

    def _byte(self, lines: int) -> int:
        """A received byte: the nibble of IO3-IO0 twice with 4 lines, IO1 eight times with 1 line."""
        if lines == 4:
            return self.io << 4 | self.io
        return 0xFF if self.io & 0b10 else 0x00

    def _find(self, args: list[str], options: dict[str, str], low: int, high: int, keys: tuple[str, ...]) -> dict[str, Any]:
        _shape(args, options, low, high, keys)
        return self.fw.find_instance(self.prefix, str(_number(args[0], 0, QSPI_INDEX)))

    def _phases(self, options: dict[str, str]) -> int:
        """`ParsePhases`: the numbers in order, then `lines` other than 1 and 4 or values wider than their bytes
        (`range`); returns `lines`."""
        _number(options.get("instr", "0"), 0, 0xFF)
        address = _number(options.get("addr", "0"), 0, UINT32_MAX)
        address_bytes = _number(options.get("abytes", "3"), 1, _PHASE_BYTES_MAX)
        alternate = _number(options.get("alt", "0"), 0, UINT32_MAX)
        alternate_bytes = _number(options.get("altbytes", "1"), 1, _PHASE_BYTES_MAX)
        _number(options.get("dummy", "0"), 0, QSPI_DUMMY_MAX)
        lines = _number(options.get("lines", "1"), 1, 4)
        if lines not in (1, 4) or not _fits(address, address_bytes) or not _fits(alternate, alternate_bytes):
            _fail("range")
        return lines

    @staticmethod
    def _generated(options: dict[str, str]) -> bytes:
        pattern = _choice(options, "pattern", PATTERNS, "inc")
        seed = _number(options.get("seed", "0"), 0, SEED_MAX)
        length = _number(options["len"], 1, QSPI_BUFFER)
        return generate(length, pattern, seed)

    @staticmethod
    def _check_busy(state: dict[str, Any]) -> None:
        if state["stuck"]:
            _fail("busy")

    def cmd_open(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1, ("variant", "prescaler", "size"))
        index = _number(args[0], 0, QSPI_INDEX)
        variant = _choice(options, "variant", _VARIANTS, "poll")
        prescaler = _number(options.get("prescaler", "2"), 0, QSPI_PRESCALER_MAX)
        _number(options.get("size", "24"), 1, QSPI_SIZE_MAX)
        if index not in self.fw.spec.qspi:
            _fail("range")
        for function, pin in _PINS:
            self.fw.check_function(function, 0, pin)
        state = {"variant": variant, "prescaler": prescaler, "stuck": False}
        resources = [] if variant == "poll" else [_DMA]
        self.fw.open_instance(self.prefix, str(index), state, [pin for _, pin in _PINS], resources=resources)
        return f"OK clk={qspi_clock(self.fw.kernel_clock, prescaler)}"

    def cmd_cmd(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, 1, 1, _COMMAND_KEYS)
        transmit, generated, receive = "tx" in options, "len" in options, "rx" in options
        patterned = "pattern" in options or "seed" in options
        if (
            transmit + generated + receive > 1
            or (patterned and not generated)
            or ("out" in options and not receive)
            or (receive and "repeat" in options)
            or not _phases_shape(options)
        ):
            _fail("usage")
        lines = self._phases(options)
        length = 0
        output = "hex"
        if transmit:
            _hex(options["tx"], QSPI_BUFFER)
        elif generated:
            self._generated(options)
        elif receive:
            length = _number(options["rx"], 1, QSPI_BUFFER)
            output = _choice(options, "out", _OUTPUTS, "hex")
            if output == "hex" and length > QSPI_HEX_MAX:
                _fail("range")
        _number(options.get("repeat", "1"), 1, QSPI_REPEAT_MAX)
        self._check_busy(state)
        if not receive:
            return "OK flevel=0"
        data = bytes([self._byte(lines)]) * length
        if output == "crc":
            return f"OK len={length} crc={crc_text(data)}"
        return f"OK data={data.hex()}"

    def cmd_poll(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, 1, 1, _POLL_KEYS)
        if "match" not in options or "mask" not in options or not _phases_shape(options):
            _fail("usage")
        lines = self._phases(options)
        match = _number(options["match"], 0, UINT32_MAX)
        mask = _number(options["mask"], 0, UINT32_MAX)
        size = _number(options.get("size", "1"), 1, _PHASE_BYTES_MAX)
        if not _fits(match, size) or not _fits(mask, size):
            _fail("range")
        self._check_busy(state)
        status = int.from_bytes(bytes([self._byte(lines)]) * size, "big")
        if status & mask == match & mask:
            return "OK"
        self.fw.sleep(QSPI_TIMEOUT_S)
        state["stuck"] = True
        return "ERR timeout"

    def cmd_xfer(self, args: list[str], options: dict[str, str]) -> str:
        state = self._find(args, options, 2, 2, _TRANSFER_KEYS)
        text = args[1]
        receive = "rx" in options
        if (text != "-" or "len" in options) == receive or (receive and "repeat" in options):
            _fail("usage")
        generated = "len" in options
        patterned = "pattern" in options or "seed" in options
        if text != "-":
            if generated or patterned:
                _fail("usage")
            _hex(text, QSPI_BUFFER)
        elif generated:
            self._generated(options)
        elif patterned:
            _fail("usage")
        length = _number(options["rx"], 1, QSPI_HEX_MAX) if receive else 0
        _number(options.get("repeat", "1"), 1, QSPI_REPEAT_MAX)
        if state["variant"] != "spi":
            _fail("unsupported")
        self._check_busy(state)
        if not receive:
            return "OK flevel=0"
        return f"OK rx={(bytes([self._byte(1)]) * length).hex()}"

    def cmd_close(self, args: list[str], options: dict[str, str]) -> str:
        _shape(args, options, 1, 1)
        key = str(_number(args[0], 0, QSPI_INDEX))
        self.fw.find_instance(self.prefix, key)
        return self.fw.close_instance(self.prefix, key)
