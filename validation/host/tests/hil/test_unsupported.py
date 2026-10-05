"""Groups hal-st has no driver for on these boards (comparator, CAN, Ethernet) and the groups the running MCU lacks
answer `ERR unsupported`."""

import pytest
from ad3_waveforms_bench.terminal import FirmwareError


@pytest.mark.board_params("line", "unsupported.commands")
def test_unsupported_commands(fw, line):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == "unsupported"
