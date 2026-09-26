#include "hal_st/middlewares/ble_middleware/WirelessCoprocessorUpgradeWb.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include DEVICE_HEADER
#include <algorithm>

namespace hal
{
    namespace
    {
        constexpr uint16_t recordMagic = 0x5543;
        constexpr uint8_t noError = 0x00;
        constexpr uint8_t unknownError = 0xff;
        constexpr uint8_t erasedByte = 0xff;
        constexpr uint32_t installUnconfirmed = 0x100;

        constexpr uint32_t wirelessStackMigrationArea = 0x4000;

        bool Erased(infra::ConstByteRange range)
        {
            return std::all_of(range.begin(), range.end(), [](uint8_t byte)
                {
                    return byte == erasedByte;
                });
        }

        uint32_t RoundUp(uint32_t value, uint32_t multiple)
        {
            return (value + multiple - 1) / multiple * multiple;
        }
    }

    WirelessCoprocessorUpgradeWb::WirelessCoprocessorUpgradeWb(hal::Flash& internalFlash, uint32_t journalSector, FirmwareUpgradeServices& firmwareUpgradeServices, const infra::Function<void(Outcome)>& onOutcome, const Config& config)
        : flash(internalFlash)
        , journalSector(journalSector)
        , firmwareUpgradeServices(firmwareUpgradeServices)
        , onOutcome(onOutcome)
        , config(config)
    {
        really_assert(journalSector + 1 < flash.NumberOfSectors());
        really_assert(flash.SizeOfSector(journalSector) % sizeof(records) == 0);
    }

    void WirelessCoprocessorUpgradeWb::Resume(RunningFirmware runningFirmware)
    {
        this->runningFirmware = runningFirmware;
        pollTimer.Cancel();

        ReadJournal([this]()
            {
                if (lastRecord)
                    image = lastRecord->image;

                if (this->runningFirmware == RunningFirmware::wirelessStack)
                    ContinueWithWirelessStack();
                else
                    ContinueWithFirmwareUpgradeServices();
            });
    }

    void WirelessCoprocessorUpgradeWb::Prepare(Image image, uint32_t size, const infra::Function<void(bool fits)>& onDone)
    {
        really_assert(size != 0);

        auto secureFlashStart = ToFlashAddress(firmwareUpgradeServices.SecureFlashStartAddress());
        auto span = RoundUp(size, flash.SizeOfSector(journalSector));
        auto room = span + (image == Image::wirelessStack ? wirelessStackMigrationArea : 0);
        auto lowest = flash.AddressOfSector(journalSector + 1);
        onPrepared = onDone;

        if (secureFlashStart < lowest || secureFlashStart - lowest < room)
        {
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    onPrepared(false);
                });
            return;
        }

        this->image = image;
        imageAddress = secureFlashStart - room;
        imageSize = size;
        written = 0;
        writeAddress = imageAddress;
        tailSize = 0;

        EraseJournal([this]()
            {
                flash.EraseSectors(flash.SectorOfAddress(imageAddress), flash.SectorOfAddress(imageAddress + RoundUp(imageSize, flash.SizeOfSector(journalSector))), [this]()
                    {
                        onPrepared(true);
                    });
            });
    }

    void WirelessCoprocessorUpgradeWb::Write(infra::ConstByteRange data, const infra::Function<void()>& onDone)
    {
        really_assert(written + data.size() <= imageSize);

        written += data.size();
        pending = data;
        onWritten = onDone;

        if (tailSize != 0)
        {
            auto size = std::min(tail.size() - tailSize, pending.size());
            std::copy_n(pending.begin(), size, tail.begin() + tailSize);
            tailSize += size;
            pending = infra::DiscardHead(pending, size);

            if (tailSize == tail.size())
            {
                WriteTail([this]()
                    {
                        WriteAligned();
                    });
                return;
            }
        }

        WriteAligned();
    }

    void WirelessCoprocessorUpgradeWb::Install()
    {
        really_assert(imageSize != 0 && written == imageSize);

        if (tailSize == 0)
        {
            RequestInstall();
            return;
        }

        std::fill(tail.begin() + tailSize, tail.end(), erasedByte);
        tailSize = tail.size();
        WriteTail([this]()
            {
                RequestInstall();
            });
    }

    void WirelessCoprocessorUpgradeWb::DeleteWirelessStack()
    {
        image = Image::wirelessStack;
        EraseJournal([this]()
            {
                AppendRecord(Step::deleteRequested, 0, [this]()
                    {
                        if (runningFirmware == RunningFirmware::firmwareUpgradeServices)
                            IssueDelete();
                        else
                            firmwareUpgradeServices.RequestFirmwareUpgradeServices();
                    });
            });
    }

    void WirelessCoprocessorUpgradeWb::ReadJournal(const infra::Function<void()>& onDone)
    {
        lastRecord = std::nullopt;
        nextRecordAddress = JournalAddress();
        onJournalRead = onDone;
        ReadNextRecords();
    }

    void WirelessCoprocessorUpgradeWb::ReadNextRecords()
    {
        if (nextRecordAddress == JournalEnd())
        {
            onJournalRead();
            return;
        }

        flash.ReadBuffer(infra::MakeByteRange(records), nextRecordAddress, [this]()
            {
                for (const auto& entry : records)
                {
                    if (Erased(infra::MakeByteRange(entry)))
                    {
                        onJournalRead();
                        return;
                    }

                    if (entry.magic != recordMagic)
                    {
                        EraseJournal([this]()
                            {
                                onJournalRead();
                            });
                        return;
                    }

                    lastRecord = entry;
                    nextRecordAddress += sizeof(Record);
                }

                ReadNextRecords();
            });
    }

    void WirelessCoprocessorUpgradeWb::EraseJournal(const infra::Function<void()>& onDone)
    {
        lastRecord = std::nullopt;
        nextRecordAddress = JournalAddress();
        flash.EraseSectors(journalSector, journalSector + 1, onDone);
    }

    void WirelessCoprocessorUpgradeWb::AppendRecord(Step step, uint32_t value, const infra::Function<void()>& onDone)
    {
        really_assert(nextRecordAddress < JournalEnd());

        record = Record{ recordMagic, step, image, value };
        lastRecord = record;
        flash.WriteBuffer(infra::MakeByteRange(record), nextRecordAddress, onDone);
        nextRecordAddress += sizeof(Record);
    }

    void WirelessCoprocessorUpgradeWb::ContinueWithWirelessStack()
    {
        if (!lastRecord)
        {
            Report(Result::idle, noError);
            return;
        }

        switch (lastRecord->step)
        {
            case Step::deleteRequested:
            case Step::installRequested:
                firmwareUpgradeServices.RequestFirmwareUpgradeServices();
                break;
            case Step::deleteIssued:
                EraseJournal([this]()
                    {
                        Report(Result::deleteFailed, unknownError);
                    });
                break;
            case Step::upgradeIssued:
                EraseJournal([this]()
                    {
                        Report(Result::installUnconfirmed, noError);
                    });
                break;
            case Step::wirelessStackStartIssued:
            case Step::wirelessStackStartRetried:
            {
                auto installResult = lastRecord->value;
                EraseJournal([this, installResult]()
                    {
                        ReportWirelessStackStarted(installResult);
                    });
                break;
            }
            default:
                EraseJournal([this]()
                    {
                        Report(Result::idle, noError);
                    });
                break;
        }
    }

    void WirelessCoprocessorUpgradeWb::ContinueWithFirmwareUpgradeServices()
    {
        if (!lastRecord)
        {
            Report(Result::idle, noError);
            return;
        }

        switch (lastRecord->step)
        {
            case Step::deleteRequested:
                IssueDelete();
                break;
            case Step::deleteIssued:
                PollUntilDone(&WirelessCoprocessorUpgradeWb::DeleteDone);
                break;
            case Step::installRequested:
                IssueUpgrade(lastRecord->value);
                break;
            case Step::upgradeIssued:
                PollUntilDone(&WirelessCoprocessorUpgradeWb::UpgradeDone);
                break;
            case Step::wirelessStackStartIssued:
                StartWirelessStack(Step::wirelessStackStartRetried, lastRecord->value);
                break;
            case Step::wirelessStackStartRetried:
            {
                auto installResult = lastRecord->value;
                EraseJournal([this, installResult]()
                    {
                        ReportWirelessStackNotStarted(installResult);
                    });
                break;
            }
            default:
                EraseJournal([this]()
                    {
                        Report(Result::idle, noError);
                    });
                break;
        }
    }

    void WirelessCoprocessorUpgradeWb::RequestInstall()
    {
        AppendRecord(Step::installRequested, FLASH_BASE + imageAddress, [this]()
            {
                if (runningFirmware == RunningFirmware::firmwareUpgradeServices)
                    IssueUpgrade(FLASH_BASE + imageAddress);
                else
                    firmwareUpgradeServices.RequestFirmwareUpgradeServices();
            });
    }

    void WirelessCoprocessorUpgradeWb::IssueDelete()
    {
        AppendRecord(Step::deleteIssued, 0, [this]()
            {
                if (firmwareUpgradeServices.DeleteWirelessStack())
                    PollUntilDone(&WirelessCoprocessorUpgradeWb::DeleteDone);
                else
                    EraseJournal([this]()
                        {
                            Report(Result::deleteFailed, unknownError);
                        });
            });
    }

    void WirelessCoprocessorUpgradeWb::DeleteDone(FirmwareUpgradeServices::Status status)
    {
        if (status.state == FirmwareUpgradeServices::State::idle)
        {
            Report(Result::readyForImage, noError);
            return;
        }

        auto errorCode = status.errorCode;
        EraseJournal([this, errorCode]()
            {
                Report(Result::deleteFailed, errorCode);
            });
    }

    void WirelessCoprocessorUpgradeWb::IssueUpgrade(uint32_t address)
    {
        AppendRecord(Step::upgradeIssued, address, [this]()
            {
                if (firmwareUpgradeServices.Upgrade(lastRecord->value))
                    PollUntilDone(&WirelessCoprocessorUpgradeWb::UpgradeDone);
                else
                    StartWirelessStack(Step::wirelessStackStartIssued, unknownError);
            });
    }

    void WirelessCoprocessorUpgradeWb::UpgradeDone(FirmwareUpgradeServices::Status status)
    {
        if (status.state == FirmwareUpgradeServices::State::error)
            StartWirelessStack(Step::wirelessStackStartIssued, status.errorCode);
        else
            StartWirelessStack(Step::wirelessStackStartIssued, busySeen ? noError : installUnconfirmed);
    }

    void WirelessCoprocessorUpgradeWb::StartWirelessStack(Step step, uint32_t installResult)
    {
        AppendRecord(step, installResult, [this, installResult]()
            {
                if (!firmwareUpgradeServices.StartWirelessStack())
                    EraseJournal([this, installResult]()
                        {
                            ReportWirelessStackNotStarted(installResult);
                        });
            });
    }

    void WirelessCoprocessorUpgradeWb::ReportWirelessStackStarted(uint32_t installResult)
    {
        if (installResult == installUnconfirmed)
            Report(Result::installUnconfirmed, noError);
        else if (installResult == noError)
            Report(Result::installed, noError);
        else
            Report(Result::installFailed, static_cast<uint8_t>(installResult));
    }

    void WirelessCoprocessorUpgradeWb::ReportWirelessStackNotStarted(uint32_t installResult)
    {
        if (installResult == installUnconfirmed)
            Report(Result::installUnconfirmed, noError);
        else if (installResult == noError && image == Image::firmwareUpgradeServices)
            Report(Result::installed, noError);
        else
            Report(Result::installFailed, installResult != noError ? static_cast<uint8_t>(installResult) : unknownError);
    }

    void WirelessCoprocessorUpgradeWb::PollUntilDone(void (WirelessCoprocessorUpgradeWb::*onDone)(FirmwareUpgradeServices::Status))
    {
        busySeen = false;
        idlePolls = 0;
        onFirmwareUpgradeServicesDone = onDone;
        pollTimer.Start(config.statusPollInterval, [this]()
            {
                Poll();
            });
    }

    void WirelessCoprocessorUpgradeWb::Poll()
    {
        auto status = firmwareUpgradeServices.GetStatus();

        if (status.state == FirmwareUpgradeServices::State::idle && !busySeen && ++idlePolls < config.idlePollsBeforeDone)
            return;

        if (status.state != FirmwareUpgradeServices::State::idle && status.state != FirmwareUpgradeServices::State::error)
        {
            busySeen = true;
            return;
        }

        pollTimer.Cancel();
        (this->*onFirmwareUpgradeServicesDone)(status);
    }

    void WirelessCoprocessorUpgradeWb::Report(Result result, uint8_t errorCode)
    {
        if (onOutcome)
            onOutcome(Outcome{ result, errorCode });
    }

    void WirelessCoprocessorUpgradeWb::WriteTail(const infra::Function<void()>& onDone)
    {
        flash.WriteBuffer(infra::MakeByteRange(tail), writeAddress, onDone);
        writeAddress += tail.size();
        tailSize = 0;
    }

    void WirelessCoprocessorUpgradeWb::WriteAligned()
    {
        auto alignedSize = pending.size() - pending.size() % tail.size();
        auto aligned = infra::Head(pending, alignedSize);
        auto rest = infra::DiscardHead(pending, alignedSize);

        if (!rest.empty())
        {
            std::copy(rest.begin(), rest.end(), tail.begin());
            tailSize = rest.size();
        }

        if (aligned.empty())
        {
            infra::EventDispatcher::Instance().Schedule([this]()
                {
                    onWritten();
                });
            return;
        }

        flash.WriteBuffer(aligned, writeAddress, [this]()
            {
                onWritten();
            });
        writeAddress += alignedSize;
    }

    uint32_t WirelessCoprocessorUpgradeWb::JournalAddress() const
    {
        return flash.AddressOfSector(journalSector);
    }

    uint32_t WirelessCoprocessorUpgradeWb::JournalEnd() const
    {
        return JournalAddress() + flash.SizeOfSector(journalSector);
    }

    uint32_t WirelessCoprocessorUpgradeWb::ToFlashAddress(uint32_t address) const
    {
        return address - FLASH_BASE;
    }
}
