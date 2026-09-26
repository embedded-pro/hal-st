# STM32WB55 Wireless Coprocessor Upgrade

STM32CubeProgrammer upgrades the firmware of CPU2, the wireless stack or the firmware upgrade services (FUS) themselves, through its firmware upgrade services panel: write the image to flash, start FUS, upgrade, delete the wireless stack, start the wireless stack. `hal::WirelessCoprocessorUpgradeWb` does the same from the application on CPU1, so that a product can upgrade its wireless coprocessor with an image it received itself, for instance over BLE.

The procedure follows AN5185, ST firmware upgrade services for STM32WB Series [1].

## Components

| Component                                | Role                                                                                                                                    |
|------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------|
| `hal::FirmwareUpgradeServicesWb`         | The FUS commands over SHCI: start FUS, read its state, upgrade, delete the wireless stack, start the wireless stack; and reads the SFSA |
| `hal::WirelessCoprocessorUpgradeWb`      | Places and writes the image, and runs the procedure across the resets it causes                                                         |
| `hal::SystemTransportLayerWb`            | `OnFirmwareUpgradeServicesRunning()` reports that CPU2 came up running FUS instead of the wireless stack                                |
| `hal::FlashCoordinatedWithWirelessStack` | `FirmwareUpgradeServicesReady()` releases flash operations while FUS runs                                                               |

`FirmwareUpgradeServicesWb` implements the `hal::FirmwareUpgradeServices` interface, which `WirelessCoprocessorUpgradeWb` depends on.

## Resets

CPU2 resets the whole device twice in the procedure: when the wireless stack hands over to FUS, and when FUS starts the wireless stack again. FUS itself may reset the device while it installs. `WirelessCoprocessorUpgradeWb` therefore keeps a journal in one sector of internal flash and continues from it: call `Resume()` once after each reset, when CPU2 reports which firmware runs. Internal flash programs each doubleword once between erases, so every step of the procedure appends a doubleword to the journal and nothing is overwritten.

## Image placement

The image goes directly below the secure flash start address (SFSA, the start of CPU2's flash), where FUS expects it, at the address STM32CubeProgrammer computes from the SFSA:

- FUS image: `SFSA - size`, rounded down to a page
- Wireless stack image: `SFSA - size - 0x4000`, rounded down to a page; wireless stacks in the format ST marks "FUS_v2 only" use the extra 16 KiB to migrate their NVM data

With a wireless stack installed, the SFSA is the start of that stack, so the image must fit between the journal sector and the current stack. `Prepare()` reports whether it fits. When it does not, `DeleteWirelessStack()` removes the stack first: the SFSA then moves up to the start of FUS, but BLE is unavailable until the new stack runs, so the image has to arrive over another transport.

The pages from the journal sector up to the SFSA must not hold the application or its data.

## Use

```cpp
static hal::FlashCoordinatedWithWirelessStack flash{ internalFlash, watchdog };
static hal::FirmwareUpgradeServicesWb firmwareUpgradeServices;
static hal::WirelessCoprocessorUpgradeWb upgrade{ flash, journalSector, firmwareUpgradeServices, [](hal::WirelessCoprocessorUpgradeWb::Outcome outcome)
    {
        // idle, readyForImage, installed, installFailed, installUnconfirmed or deleteFailed
    } };

// Wireless stack ready, from the onInitialized callback of SystemTransportLayerWb
upgrade.Resume(hal::WirelessCoprocessorUpgradeWb::RunningFirmware::wirelessStack);

// FUS ready
systemTransportLayer.OnFirmwareUpgradeServicesRunning([]()
    {
        flash.FirmwareUpgradeServicesReady();
        upgrade.Resume(hal::WirelessCoprocessorUpgradeWb::RunningFirmware::firmwareUpgradeServices);
    });
```

`internalFlash` spans the internal flash from `FLASH_BASE`, with the page size as sector size, as `hal::FlashHomogeneousInternalStm` does.

To upgrade:

1. `Prepare(image, size, onDone)` erases the journal and the pages for the image; `onDone(false)` when it does not fit.
2. `Write(data, onDone)` for each chunk, in order, until `size` bytes are written. Chunks may have any size; the data must stay valid until `onDone`.
3. `Install()` starts FUS and has it install the image. The device resets; after the procedure the outcome is reported from `Resume()`.

When `Prepare()` reports that the image does not fit, `DeleteWirelessStack()` starts FUS and deletes the stack; `Resume()` then reports `readyForImage`, and steps 1 to 3 follow while FUS runs.

The outcome carries FUS's error code (`SHCI_FUS_GetState_ErrorCode_t`) for `installFailed` and `deleteFailed`, or `0xff` when FUS did not report one. `installUnconfirmed` means the wireless stack was running again before FUS reported how the install ended; compare `SystemTransportLayerWb::GetVersion()` with the image.

FUS images must be installed in order: FUS v1.2.0 before FUS v2.x, and a wireless stack in the "FUS_v2 only" format only over FUS v2.x. Check the running FUS with `SystemTransportLayerWb::GetVersion()` first. FUS verifies the image signature itself; ST's images are encrypted and signed, and are installed as delivered.

## References

[1] AN5185, ST firmware upgrade services for STM32WB Series, https://www.st.com/resource/en/application_note/an5185-st-firmware-upgrade-services-for-stm32wb-series-stmicroelectronics.pdf
