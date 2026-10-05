"""Discovery of the area modules (`fakes/*.py` `FakeGroup`s, `groups/*.py` `GROUPS`) with dummy modules placed in a
temporary directory on the packages' paths, dispatch through the fake firmware and the state the groups share."""

import sys

import pytest
from ad3_waveforms_bench.terminal import FirmwareError, FirmwareTerminal

from hal_st_validation import fakes, groups
from hal_st_validation.fake_firmware import FakeFirmware, FakeSerial
from hal_st_validation.fakes.base import FakeError, FakeGroup
from hal_st_validation.firmware import Firmware

FAKE_MODULE = """
from hal_st_validation.fakes.base import FakeGroup, _number, _shape


class Dummy(FakeGroup):
    prefix = "dummy"

    def boot(self):
        self.boots = getattr(self, "boots", 0) + 1
        self.polls = 0

    def poll(self):
        self.polls += 1

    def cmd_open(self, args, options):
        _shape(args, options, 1, 1, ("pin", "spi", "timer"))
        index = _number(args[0], 1, 3)
        pin = self.fw.pin(options.get("pin"))
        resources = [("spi", _number(options["spi"], 1, 3))] if "spi" in options else []
        timer = _number(options["timer"], 1, 17) if "timer" in options else None
        self.fw.open_instance(self.prefix, str(index), {"pin": pin}, [pin], timer=timer, resources=resources)
        return "OK"

    def cmd_close(self, args, options):
        _shape(args, options, 1, 1)
        self.fw.find_instance(self.prefix, args[0])
        return self.fw.close_instance(self.prefix, args[0])

    def cmd_event(self, args, options):
        self.fw.event("EVT dummy index=1")
        return ["EVT dummy line=2", "OK"]


class Helper:
    prefix = "helper"
"""

GROUP_MODULE = """
from hal_st_validation.groups.base import Group


class Dummy(Group):
    prefix = "dummy"

    def open(self, index, pin=None):
        self._cmd("open", index, pin=self._pin(pin))
        self._fw.track(("dummy", index), "dummy.close", index)


GROUPS = {"dummy": Dummy}
"""

MODULES = ("hal_st_validation.fakes.dummy_area", "hal_st_validation.groups.dummy_area", "hal_st_validation.groups.clash_area")


@pytest.fixture
def area(tmp_path, monkeypatch):
    """Writes `dummy_area.py` modules on the paths of `fakes` and `groups`; returns the group directory for more."""
    fake_dir, group_dir = tmp_path / "fakes", tmp_path / "groups"
    fake_dir.mkdir()
    group_dir.mkdir()
    (fake_dir / "dummy_area.py").write_text(FAKE_MODULE, encoding="utf-8")
    (group_dir / "dummy_area.py").write_text(GROUP_MODULE, encoding="utf-8")
    monkeypatch.setattr(fakes, "__path__", [*fakes.__path__, str(fake_dir)])
    monkeypatch.setattr(groups, "__path__", [*groups.__path__, str(group_dir)])
    yield group_dir
    for name in MODULES:
        sys.modules.pop(name, None)


def make(family="stm32wb55"):
    firmware = FakeFirmware(family=family, style="line")
    return FirmwareTerminal(serial=FakeSerial(firmware), timeout=0.5), firmware


def reason(terminal, line):
    response = terminal.command(line, check=False)
    return "ok" if response.ok else response.reason


def test_discovery(area):
    found = {cls.prefix: cls for cls in fakes.discover()}
    assert {"dummy", "qspi"} <= set(found)
    assert "helper" not in found, "only FakeGroup subclasses count"
    assert issubclass(found["dummy"], FakeGroup)
    assert "dummy" in groups.discover()


def test_without_area_modules_nothing_is_added():
    assert all(cls.__module__.startswith("hal_st_validation.fakes.") for cls in fakes.discover())
    terminal, firmware = make()
    assert "dummy" not in firmware.groups
    assert reason(terminal, "dummy.open 1") == "usage", "an unknown command"


def test_dispatch_and_errors(area):
    terminal, firmware = make()
    assert isinstance(firmware.group("dummy"), FakeGroup)
    assert reason(terminal, "dummy.open 1 pin=PA5") == "ok"
    assert firmware.opened[("dummy", "1")] == {"pin": "PA5"}
    assert reason(terminal, "dummy.open 4") == "range"
    assert reason(terminal, "dummy.open 1 nosuchkey=1") == "usage"
    assert reason(terminal, "dummy.nosuch 1") == "usage", "a verb without cmd_<verb>"
    assert reason(terminal, "dummy.open 2") == "busy", "one instance per group"
    assert reason(terminal, "dummy.close 2") == "notopen"
    assert reason(terminal, "dummy.close 1") == "ok"
    assert firmware.claims == {}


def test_claims_are_shared_with_the_built_in_groups(area):
    terminal, firmware = make()
    terminal.command("dummy.open 1 pin=spi1clk spi=1 timer=2")
    assert reason(terminal, "spi.open 1 clk=PA5 mosi=PA7 miso=PA6") == "busy", "PA5 is held"
    assert reason(terminal, "pwm.open 2 channels=1") == "busy", "TIM2 is held"
    terminal.command("dummy.close 1")
    terminal.command("dummy.open 1 spi=1")
    assert reason(terminal, "spi.open 1 clk=PA5 mosi=PA7 miso=PA6") == "busy", "SPI1 is held"
    assert reason(terminal, "spi.open 2 clk=PB13 mosi=PB15 miso=PB14") == "ok"
    terminal.command("dummy.close 1")
    assert reason(terminal, "dummy.open 1 spi=2") == "busy", "the spi group holds SPI2"
    assert firmware.resources == {("spi", 2): ("spi", "2")}


def test_resources(area):
    _, firmware = make()
    firmware.claim_resource("dma1", 7, ("dummy", "1"))
    firmware.claim_resource("dma1", 7, ("dummy", "1"))
    with pytest.raises(FakeError) as error:
        firmware.claim_resource("dma1", 7, ("adc", "1"))
    assert error.value.reason == "busy"
    with pytest.raises(ValueError):
        firmware.claim_resource("dma3", 1, ("dummy", "1"))
    with pytest.raises(ValueError):
        firmware.claim_resource("dma1", 16, ("dummy", "1"))
    firmware.release(("dummy", "1"))
    assert firmware.resources == {}


def test_boot_poll_and_events(area):
    terminal, firmware = make()
    group = firmware.group("dummy")
    assert group.boots == 1
    terminal.command("dummy.open 1 pin=PA5 spi=1")
    terminal.pump(0.01)
    assert group.polls > 0, "reads and writes poll the groups"
    terminal.send_nowait("reset")
    terminal.wait_boot(0.5)
    assert group.boots == 2
    assert firmware.opened == {} and firmware.resources == {} and firmware.claims == {}
    assert terminal.command("dummy.event").ok
    assert [event["line"] for event in terminal.drain_events("dummy") if "line" in event.values] == ["2"]


def test_unsupported_names_come_first(area):
    terminal, _ = make("stm32wba55")
    assert reason(terminal, "qspi.close 1") == "unsupported"
    terminal, _ = make("stm32wb55")
    assert reason(terminal, "qspi.close 1") == "notopen", "the qspi group of fakes/qspi.py serves the STM32WB55"


def test_firmware_attaches_the_groups(area):
    fake = FakeFirmware()
    fw = Firmware(FirmwareTerminal(serial=FakeSerial(fake), timeout=0.5), fake.pins)
    fw.dummy.open(1, pin="spi1clk")
    assert fake.received[-1] == "dummy.open 1 pin=PA5"
    assert fw.open_instances == [("dummy", 1)]
    assert fw.close_all() == []
    assert fake.received[-1] == "dummy.close 1"
    with pytest.raises(FirmwareError):
        fw.dummy._cmd("close", 1)


def test_group_name_clashes(area):
    (area / "clash_area.py").write_text(GROUP_MODULE.replace('{"dummy": Dummy}', '{"gpio": Dummy}'), encoding="utf-8")
    with pytest.raises(ValueError, match="clashes with Firmware.gpio"):
        Firmware(FirmwareTerminal(serial=FakeSerial(FakeFirmware()), timeout=0.5))
    (area / "clash_area.py").write_text(GROUP_MODULE, encoding="utf-8")
    sys.modules.pop("hal_st_validation.groups.clash_area", None)
    with pytest.raises(ValueError, match="exported by"):
        groups.discover()
    (area / "clash_area.py").write_text('GROUPS = {"other": object}\n', encoding="utf-8")
    sys.modules.pop("hal_st_validation.groups.clash_area", None)
    with pytest.raises(TypeError):
        groups.discover()


def test_fake_group_prefix_clash(tmp_path, monkeypatch):
    (tmp_path / "one_area.py").write_text(FAKE_MODULE, encoding="utf-8")
    (tmp_path / "two_area.py").write_text(FAKE_MODULE, encoding="utf-8")
    monkeypatch.setattr(fakes, "__path__", [*fakes.__path__, str(tmp_path)])
    try:
        with pytest.raises(ValueError, match="both serve 'dummy'"):
            fakes.discover()
    finally:
        for name in ("hal_st_validation.fakes.one_area", "hal_st_validation.fakes.two_area"):
            sys.modules.pop(name, None)
