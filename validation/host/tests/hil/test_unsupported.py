"""Groups hal-st has no driver for on these boards (comparator, CAN, Ethernet) and the groups the running MCU lacks
answer `ERR unsupported`.

Scenarios: features/unsupported.feature.
"""

import pytest
from ad3_waveforms_bench.terminal import FirmwareError
from pytest_bdd import parsers, scenario, then


@pytest.mark.board_params("line", "unsupported.commands")
@scenario("unsupported.feature", "Commands of unsupported groups are refused")
def test_unsupported_commands(line):
    pass


@then(parsers.parse('the command line fails with "{reason}"'))
def command_refused(fw, line, reason):
    with pytest.raises(FirmwareError) as error:
        fw.terminal.command(line)
    assert error.value.reason == reason
