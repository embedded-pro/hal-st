"""Command group wrappers, one module per area (`groups/<area>.py`), each exporting
`GROUPS: dict[str, type[Group]]` (attribute name on `Firmware` -> class); `discover()` collects them."""

from __future__ import annotations

import importlib
import pkgutil

from .base import Group

__all__ = ["Group", "discover"]


def discover() -> dict[str, type[Group]]:
    """The `GROUPS` of every module of this package; one name exported twice is an error."""
    found: dict[str, type[Group]] = {}
    owners: dict[str, str] = {}
    for info in sorted(pkgutil.iter_modules(__path__), key=lambda info: info.name):
        if info.name == "base":
            continue
        module = importlib.import_module(f"{__name__}.{info.name}")
        for name, cls in getattr(module, "GROUPS", {}).items():
            if not (isinstance(cls, type) and issubclass(cls, Group)):
                raise TypeError(f"{module.__name__}.GROUPS[{name!r}] is not a Group")
            if name in found:
                raise ValueError(f"group {name!r} is exported by {owners[name]} and {module.__name__}")
            found[name] = cls
            owners[name] = module.__name__
    return found
