#pragma once

#include "hal/interfaces/Flash.hpp"
#include "hal_st/middlewares/ble_middleware/FirmwareUpgradeServicesWb.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace hal
{
    // Upgrades FUS or the wireless stack from CPU1, as STM32CubeProgrammer's firmware upgrade services do.
    // CPU2 resets the device when it switches to FUS and while it installs, so progress is kept in a journal
    // sector of internal flash, and Resume() continues from it after every reset.
    class WirelessCoprocessorUpgradeWb
    {
    public:
        enum class Image : uint8_t
        {
            wirelessStack,
            firmwareUpgradeServices
        };

        enum class RunningFirmware : uint8_t
        {
            wirelessStack,
            firmwareUpgradeServices
        };

        enum class Result : uint8_t
        {
            idle,
            readyForImage,
            installed,
            installFailed,
            // The wireless stack runs again, but it was started before FUS reported how the install ended;
            // compare the running version with the image
            installUnconfirmed,
            deleteFailed
        };

        struct Outcome
        {
            Result result;
            uint8_t errorCode;
        };

        struct Config
        {
            constexpr Config()
            {}

            infra::Duration statusPollInterval = std::chrono::milliseconds(100);

            // FUS may still report idle right after accepting a command; it counts as done once it was seen busy,
            // or after it stays idle for this many polls
            uint8_t idlePollsBeforeDone = 10;
        };

        // internalFlash spans the internal flash from FLASH_BASE with the page size as sector size; the journal
        // sector and the images placed below the secure flash start address must be outside the application
        WirelessCoprocessorUpgradeWb(hal::Flash& internalFlash, uint32_t journalSector, FirmwareUpgradeServices& firmwareUpgradeServices, const infra::Function<void(Outcome)>& onOutcome, const Config& config = Config());

        // Call once after each reset, when CPU2 reports which firmware it runs
        void Resume(RunningFirmware runningFirmware);

        void Prepare(Image image, uint32_t size, const infra::Function<void(bool fits)>& onDone);
        void Write(infra::ConstByteRange data, const infra::Function<void()>& onDone);
        void Install();

        // Frees the flash taken by the wireless stack for an image that does not fit below it; reports readyForImage
        // while FUS runs, without the wireless stack
        void DeleteWirelessStack();

    private:
        enum class Step : uint8_t
        {
            deleteRequested = 1,
            deleteIssued,
            installRequested,
            upgradeIssued,
            wirelessStackStartIssued
        };

        struct Record
        {
            uint16_t magic;
            Step step;
            Image image;
            uint32_t value;
        };

        static_assert(sizeof(Record) == sizeof(uint64_t));

        void ReadJournal(const infra::Function<void()>& onDone);
        void ReadNextRecords();
        void EraseJournal(const infra::Function<void()>& onDone);
        void AppendRecord(Step step, uint32_t value, const infra::Function<void()>& onDone);

        void ContinueWithWirelessStack();
        void ContinueWithFirmwareUpgradeServices();
        void RequestInstall();
        void IssueDelete();
        void DeleteDone(FirmwareUpgradeServices::Status status);
        void IssueUpgrade(uint32_t address);
        void UpgradeDone(FirmwareUpgradeServices::Status status);
        void StartWirelessStack(uint8_t errorCode);
        void ReportWirelessStackNotStarted(uint8_t errorCode);
        void PollUntilDone(bool busySeen, void (WirelessCoprocessorUpgradeWb::*onDone)(FirmwareUpgradeServices::Status));
        void Poll();
        void Report(Result result, uint8_t errorCode);

        void WriteTail(const infra::Function<void()>& onDone);
        void WriteAligned();

        uint32_t JournalAddress() const;
        uint32_t JournalEnd() const;
        uint32_t ToFlashAddress(uint32_t address) const;

    private:
        hal::Flash& flash;
        uint32_t journalSector;
        FirmwareUpgradeServices& firmwareUpgradeServices;
        infra::Function<void(Outcome)> onOutcome;
        Config config;

        RunningFirmware runningFirmware = RunningFirmware::wirelessStack;
        Image image = Image::wirelessStack;
        std::optional<Record> lastRecord;
        uint32_t nextRecordAddress = 0;
        Record record{};
        std::array<Record, 8> records{};
        infra::AutoResetFunction<void()> onJournalRead;

        uint32_t imageAddress = 0;
        uint32_t imageSize = 0;
        uint32_t written = 0;
        uint32_t writeAddress = 0;
        std::array<uint8_t, sizeof(uint64_t)> tail{};
        std::size_t tailSize = 0;
        infra::ConstByteRange pending;
        infra::AutoResetFunction<void()> onWritten;
        infra::AutoResetFunction<void(bool)> onPrepared;

        infra::TimerRepeating pollTimer;
        void (WirelessCoprocessorUpgradeWb::*onFirmwareUpgradeServicesDone)(FirmwareUpgradeServices::Status) = nullptr;
        bool busySeen = false;
        uint8_t idlePolls = 0;
    };
}
