#include "hal_st/middlewares/ble_middleware/FirmwareUpgradeServicesWb.hpp"
#include "shci.h"
#include DEVICE_HEADER

namespace hal
{
    namespace
    {
        FirmwareUpgradeServices::State ToState(uint8_t value)
        {
            if (value == FUS_STATE_VALUE_IDLE)
                return FirmwareUpgradeServices::State::idle;
            if (value >= FUS_STATE_VALUE_FW_UPGRD_ONGOING && value <= FUS_STATE_VALUE_FW_UPGRD_ONGOING_END)
                return FirmwareUpgradeServices::State::wirelessStackUpgradeOngoing;
            if (value >= FUS_STATE_VALUE_FUS_UPGRD_ONGOING && value <= FUS_STATE_VALUE_FUS_UPGRD_ONGOING_END)
                return FirmwareUpgradeServices::State::firmwareUpgradeServicesUpgradeOngoing;
            if (value >= FUS_STATE_VALUE_SERVICE_ONGOING && value <= FUS_STATE_VALUE_SERVICE_ONGOING_END)
                return FirmwareUpgradeServices::State::serviceOngoing;

            return FirmwareUpgradeServices::State::error;
        }
    }

    uint32_t FirmwareUpgradeServicesWb::SecureFlashStartAddress() const
    {
        FLASH_OBProgramInitTypeDef optionBytes{};
        HAL_FLASHEx_OBGetConfig(&optionBytes);
        return optionBytes.SecureFlashStartAddr;
    }

    void FirmwareUpgradeServicesWb::RequestFirmwareUpgradeServices()
    {
        // The wireless stack answers the first request with SHCI_FUS_CMD_NOT_SUPPORTED, and resets the device into FUS on the second
        SHCI_C2_FUS_GetState(nullptr);
        SHCI_C2_FUS_GetState(nullptr);
    }

    FirmwareUpgradeServices::Status FirmwareUpgradeServicesWb::GetStatus()
    {
        SHCI_FUS_GetState_ErrorCode_t errorCode = FUS_STATE_ERROR_NO_ERROR;
        auto state = ToState(SHCI_C2_FUS_GetState(&errorCode));

        return { state, state == State::error ? static_cast<uint8_t>(errorCode) : static_cast<uint8_t>(FUS_STATE_ERROR_NO_ERROR) };
    }

    bool FirmwareUpgradeServicesWb::Upgrade(uint32_t imageAddress)
    {
        return SHCI_C2_FUS_FwUpgrade(imageAddress, 0) == SHCI_Success;
    }

    bool FirmwareUpgradeServicesWb::DeleteWirelessStack()
    {
        return SHCI_C2_FUS_FwDelete() == SHCI_Success;
    }

    bool FirmwareUpgradeServicesWb::StartWirelessStack()
    {
        return SHCI_C2_FUS_StartWs() == SHCI_Success;
    }
}
