"""`hal-st-console`: the `ad3-bench-console` with the validation firmware defaults (921600 baud).

hal-st-console --port /dev/ttyACM0
hal-st-console --port COM5 -c info -c board.pins
"""

from __future__ import annotations

import sys
from pathlib import Path

from ad3_waveforms_bench import console


def main(argv: list[str] | None = None) -> int:
    return console.main(
        argv,
        prog="hal-st-console",
        name="hal-st validation console",
        prompt="hal-st> ",
        baud=921600,
        history=Path.home() / ".hal_st_validation_history",
    )


if __name__ == "__main__":
    sys.exit(main())
