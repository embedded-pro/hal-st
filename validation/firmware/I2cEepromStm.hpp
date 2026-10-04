#pragma once

#include "hal/interfaces/Eeprom.hpp"
#include "hal/interfaces/I2c.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/Function.hpp"
#include <array>
#include <cstdint>

namespace validation
{
    enum class I2cEepromError : uint8_t
    {
        nack,
        busError,
    };

    class I2cEepromStm
        : public hal::Eeprom
    {
    public:
        static constexpr uint32_t maximumPage = 256;
        static constexpr uint32_t maximumAddressBytes = 2;

        struct Config
        {
            uint8_t address = 0x50;
            uint32_t size = 32768;
            uint32_t page = 64;
            uint8_t addressBytes = 2;
            uint32_t writeCycleMs = 10;
        };

        I2cEepromStm(hal::I2cMaster& master, const Config& config, const infra::Function<void(I2cEepromError error, uint32_t address)>& onError);

        uint32_t Size() const override;
        void WriteBuffer(infra::ConstByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void ReadBuffer(infra::ByteRange buffer, uint32_t address, infra::Function<void()> onDone) override;
        void Erase(infra::Function<void()> onDone) override;

        bool Busy() const;
        bool Failed() const;
        void Recover();

    private:
        void StartWrite(uint32_t address, uint32_t size, infra::Function<void()> onDone);
        void WritePage();
        void PageWritten(hal::Result result);
        void Poll();
        void Polled(hal::Result result);
        void NextPage();
        void AddressSent(hal::Result result);
        void DataReceived(hal::Result result);
        void Fail(hal::Result result);
        void Done();
        uint32_t PrepareAddress(uint32_t address);
        infra::ConstByteRange Frame(uint32_t size) const;

    private:
        hal::I2cMaster& master;
        Config config;
        infra::Function<void(I2cEepromError error, uint32_t address)> onError;
        infra::AutoResetFunction<void()> onDone;

        infra::ConstByteRange source;
        infra::ByteRange destination;
        bool erasing = false;
        uint32_t address = 0;
        uint32_t remaining = 0;
        uint32_t chunk = 0;

        bool transferring = false;
        bool failed = false;
        bool expired = false;
        infra::TimerSingleShot deadline;
        std::array<uint8_t, maximumAddressBytes + maximumPage> frame{};
    };
}
