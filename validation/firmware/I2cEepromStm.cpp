#include "validation/firmware/I2cEepromStm.hpp"
#include <algorithm>
#include <chrono>

namespace validation
{
    I2cEepromStm::I2cEepromStm(hal::I2cMaster& master, const Config& config, const infra::Function<void(I2cEepromError error, uint32_t address)>& onError)
        : master(master)
        , config(config)
        , onError(onError)
    {}

    uint32_t I2cEepromStm::Size() const
    {
        return config.size;
    }

    void I2cEepromStm::WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        source = buffer;
        erasing = false;
        StartWrite(address, static_cast<uint32_t>(buffer.size()), onDone);
    }

    void I2cEepromStm::ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone)
    {
        this->onDone = onDone;
        this->address = address;
        destination = buffer;

        if (buffer.empty())
        {
            Done();
            return;
        }

        const auto size = PrepareAddress(address);
        transferring = true;
        master.SendData(hal::I2cAddress(config.address), Frame(size), hal::Action::repeatedStart, [this](hal::Result result, uint32_t)
            {
                AddressSent(result);
            });
    }

    void I2cEepromStm::Erase(infra::Function<void()> onDone)
    {
        source = infra::ConstByteRange();
        erasing = true;
        StartWrite(0, config.size, onDone);
    }

    bool I2cEepromStm::Busy() const
    {
        return transferring;
    }

    bool I2cEepromStm::Failed() const
    {
        return failed;
    }

    void I2cEepromStm::Recover()
    {
        failed = false;

        if (onDone != nullptr)
            onDone();
    }

    void I2cEepromStm::StartWrite(uint32_t address, uint32_t size, infra::Function<void()> onDone)
    {
        this->onDone = onDone;
        this->address = address;
        remaining = size;

        if (remaining == 0)
            Done();
        else
            WritePage();
    }

    void I2cEepromStm::WritePage()
    {
        chunk = std::min(remaining, config.page - address % config.page);

        const auto header = PrepareAddress(address);
        if (erasing)
            std::fill_n(frame.begin() + header, chunk, 0xff);
        else
            std::copy_n(source.begin(), chunk, frame.begin() + header);

        transferring = true;
        master.SendData(hal::I2cAddress(config.address), Frame(header + chunk), hal::Action::stop, [this](hal::Result result, uint32_t)
            {
                PageWritten(result);
            });
    }

    void I2cEepromStm::PageWritten(hal::Result result)
    {
        transferring = false;

        if (result != hal::Result::complete)
            Fail(result);
        else if (config.writeCycleMs == 0)
            NextPage();
        else
        {
            expired = false;
            deadline.Start(std::chrono::milliseconds(config.writeCycleMs), [this]()
                {
                    expired = true;
                });
            Poll();
        }
    }

    void I2cEepromStm::Poll()
    {
        // Only the word address: a 24Cxx acknowledges it once its write cycle ends, and it starts no new write
        transferring = true;
        master.SendData(hal::I2cAddress(config.address), Frame(config.addressBytes), hal::Action::stop, [this](hal::Result result, uint32_t)
            {
                Polled(result);
            });
    }

    void I2cEepromStm::Polled(hal::Result result)
    {
        transferring = false;

        if (result == hal::Result::partialComplete && !expired)
        {
            Poll();
            return;
        }

        deadline.Cancel();

        if (result == hal::Result::complete)
            NextPage();
        else
            Fail(result);
    }

    void I2cEepromStm::NextPage()
    {
        address += chunk;
        remaining -= chunk;

        if (!erasing)
            source = infra::DiscardHead(source, chunk);

        if (remaining == 0)
            Done();
        else
            WritePage();
    }

    void I2cEepromStm::AddressSent(hal::Result result)
    {
        if (result != hal::Result::complete)
        {
            transferring = false;
            Fail(result);
            return;
        }

        master.ReceiveData(hal::I2cAddress(config.address), destination, hal::Action::stop, [this](hal::Result result)
            {
                DataReceived(result);
            });
    }

    void I2cEepromStm::DataReceived(hal::Result result)
    {
        transferring = false;

        if (result == hal::Result::complete)
            Done();
        else
            Fail(result);
    }

    void I2cEepromStm::Fail(hal::Result result)
    {
        failed = true;
        onError(result == hal::Result::busError ? I2cEepromError::busError : I2cEepromError::nack, address);
    }

    void I2cEepromStm::Done()
    {
        onDone();
    }

    uint32_t I2cEepromStm::PrepareAddress(uint32_t address)
    {
        if (config.addressBytes == 1)
        {
            frame[0] = static_cast<uint8_t>(address);
            return 1;
        }

        frame[0] = static_cast<uint8_t>(address >> 8);
        frame[1] = static_cast<uint8_t>(address);
        return 2;
    }

    infra::ConstByteRange I2cEepromStm::Frame(uint32_t size) const
    {
        return infra::Head(infra::MakeRange(frame), size);
    }
}
