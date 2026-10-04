"""Command groups of the fake firmware, one module per area (`fakes/<area>.py`); `discover()` finds them."""

from __future__ import annotations

import importlib
import inspect
import pkgutil

from .base import FakeGroup

__all__ = ["FakeGroup", "discover"]


def discover() -> tuple[type[FakeGroup], ...]:
    """Every `FakeGroup` subclass with a `prefix` defined in a module of this package, sorted by prefix; two groups
    with one prefix are an error."""
    found: dict[str, type[FakeGroup]] = {}
    for info in pkgutil.iter_modules(__path__):
        if info.name == "base":
            continue
        module = importlib.import_module(f"{__name__}.{info.name}")
        for _, cls in inspect.getmembers(module, inspect.isclass):
            if cls.__module__ != module.__name__ or not issubclass(cls, FakeGroup) or not cls.prefix:
                continue
            other = found.setdefault(cls.prefix, cls)
            if other is not cls:
                raise ValueError(f"fake groups {other.__qualname__} and {cls.__qualname__} both serve {cls.prefix!r}")
    return tuple(found[prefix] for prefix in sorted(found))
