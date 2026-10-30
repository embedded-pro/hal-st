#include "validation/firmware/CanFactory.hpp"

#if defined(HAS_PERIPHERAL_FDCAN)

#include "BoardProfile.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/Tokenizer.hpp"
#include "validation/firmware/PinFactoryStm.hpp"
#include <array>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        using services::HilStatus;

        constexpr uint32_t maximumStandardId = 0x7ff;
        constexpr uint32_t maximumExtendedId = 0x1fffffff;
        constexpr uint32_t maximumBitRate = 1000000;

        constexpr uint32_t maximumTimeSegment1 = 256;
        constexpr uint32_t maximumTimeSegment2 = 128;
        constexpr uint32_t maximumJumpWidth = 128;
        constexpr uint32_t maximumPrescaler = 512;

        constexpr std::array<const char*, 7> openKeys{ { "rx", "tx", "bitrate", "timing", "filter", "loopback", "recover" } };

        constexpr std::array<const char*, 10> errorNames{ {
            "stuffError",
            "formError",
            "ackError",
            "bit1Error",
            "bit0Error",
            "crcError",
            "errorWarning",
            "errorPassive",
            "busOff",
            "messageLost",
        } };

        uint32_t KernelClock()
        {
            return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);
        }

        bool BitRateAchievable(uint32_t bitRate)
        {
            const auto timing = hal::FdCanStm::CalculateNominalBitTiming(KernelClock(), bitRate);
            return static_cast<uint64_t>(bitRate) * timing.prescaler * (1 + timing.timeSegment1 + timing.timeSegment2) == KernelClock();
        }

        HilStatus ParseFilter(infra::BoundedConstString text, hal::FdCanStm::Config& config)
        {
            infra::Tokenizer fields(text, ',');
            auto id = services::HilArguments::ParseNumber(fields.Token(0));
            auto mask = services::HilArguments::ParseNumber(fields.Token(1));
            auto extended = services::HilArguments::ParseNumber(fields.Token(2));
            auto match = fields.Size() == 4 ? services::HilArguments::ParseNumber(fields.Token(3)) : std::optional<uint32_t>(1);

            if (fields.Size() < 3 || fields.Size() > 4 || !id || !mask || !extended || !match || *extended > 1 || *match > 1)
                return HilStatus::usage;

            const auto maximumId = *extended != 0 ? maximumExtendedId : maximumStandardId;
            if (*id > maximumId || *mask > maximumId)
                return HilStatus::range;

            config.filter = hal::FdCanStm::Filter{ *id, *mask, *extended != 0, *match != 0 };
            return HilStatus::done;
        }

        HilStatus ParseTiming(infra::BoundedConstString text, hal::FdCanStm::Config& config)
        {
            infra::Tokenizer fields(text, ',');
            std::array<uint32_t, 4> values{};
            constexpr std::array<uint32_t, 4> maxima{ { maximumTimeSegment1, maximumTimeSegment2, maximumJumpWidth, maximumPrescaler } };

            if (fields.Size() != values.size())
                return HilStatus::usage;

            for (std::size_t i = 0; i != values.size(); ++i)
            {
                auto value = services::HilArguments::ParseNumber(fields.Token(i));
                if (!value)
                    return HilStatus::usage;

                if (*value == 0 || *value > maxima[i])
                    return HilStatus::range;

                values[i] = *value;
            }

            if (values[2] > values[1])
                return HilStatus::range;

            config.timing = hal::FdCanStm::NominalBitTiming{ values[3], values[0], values[1], values[2] };
            return HilStatus::done;
        }
    }

    CanFactoryStm::CanFactoryStm(const services::HilPinNaming& naming)
        : naming(naming)
    {}

    uint8_t CanFactoryStm::Instances() const
    {
        return board::canInstances;
    }

    infra::MemoryRange<const char* const> CanFactoryStm::OpenKeys() const
    {
        return infra::MakeRange(openKeys);
    }

    HilStatus CanFactoryStm::Prepare(uint8_t index, const services::HilArguments& arguments)
    {
        Request request;
        return Parse(index, arguments, request);
    }

    HilStatus CanFactoryStm::Open(uint8_t index, const services::HilArguments& arguments, services::HilPinOwner& pins, const infra::Function<void(const char* error)>& onError, hal::Can*& opened)
    {
        Request request;
        Parse(index, arguments, request);

        hal::GpioPin* rx = nullptr;
        hal::GpioPin* tx = nullptr;
        HilStatus status = pins.ClaimFunction(request.rx, Function(hal::PinConfigTypeStm::canRx), index, rx);
        if (status == HilStatus::done)
            status = pins.ClaimFunction(request.tx, Function(hal::PinConfigTypeStm::canTx), index, tx);
        if (status != HilStatus::done)
            return status;

        this->onError = onError;
        opened = &can.emplace(index, PinOrDummy(rx), PinOrDummy(tx), request.config, [this](hal::FdCanStm::Error error)
            {
                this->onError(errorNames[static_cast<std::size_t>(error)]);
            });

        return HilStatus::done;
    }

    void CanFactoryStm::Close(uint8_t index, const infra::Function<void()>& onClosed)
    {
        this->onClosed = onClosed;

        HAL_NVIC_DisableIRQ(hal::peripheralfdCanIt0Irq[index - 1]);
        infra::EventDispatcher::Instance().Schedule([this]()
            {
                can = std::nullopt;
                this->onClosed();
            });
    }

    HilStatus CanFactoryStm::Parse(uint8_t index, const services::HilArguments& arguments, Request& request) const
    {
        if (index == 0 || index > hal::peripheralfdCan.size())
            return HilStatus::range;

        HilStatus status = HilStatus::done;
        arguments.Pin("rx", naming, request.rx, status);
        arguments.Pin("tx", naming, request.tx, status);
        arguments.Number("bitrate", request.bitRate, 1, maximumBitRate, status);
        arguments.Flag("loopback", request.config.testMode, status);
        arguments.Flag("recover", request.config.autoBusOffRecovery, status);

        if (status == HilStatus::done && arguments.Has("filter"))
            status = ParseFilter(*arguments.Key("filter"), request.config);

        if (status == HilStatus::done && arguments.Has("timing"))
            status = arguments.Has("bitrate") ? HilStatus::usage : ParseTiming(*arguments.Key("timing"), request.config);
        else if (status == HilStatus::done && !BitRateAchievable(request.bitRate))
            status = HilStatus::range;
        else
            request.config.timing = request.bitRate;

        if (status != HilStatus::done)
            return status;

        if (!request.rx && !request.tx)
        {
            if (!board::defaultCan || board::defaultCan->index != index)
                return HilStatus::usage;

            request.rx = board::defaultCan->rx;
            request.tx = board::defaultCan->tx;
        }

        if (!request.rx || !request.tx)
            return HilStatus::usage;

        if (!SupportsFunction(*request.rx, hal::PinConfigTypeStm::canRx, index) || !SupportsFunction(*request.tx, hal::PinConfigTypeStm::canTx, index))
            return HilStatus::pin;

        return HilStatus::done;
    }
}

#endif
