#ifndef HAL_QUAD_SPI_STM_HPP
#define HAL_QUAD_SPI_STM_HPP

#include "generated/stm32fxxx/PeripheralTable.hpp"
#include "hal/cortex_m/InterruptCortex.hpp"
#include "hal/interfaces/QuadSpi.hpp"
#include "hal_st/stm32fxxx/GpioStm.hpp"
#include "infra/util/AutoResetFunction.hpp"

#if defined(HAS_PERIPHERAL_QUADSPI)

namespace hal
{
    struct QuadSpiStmConfig
    {
        uint32_t prescaler = 2;
        uint32_t flashSizeLog2 = 24;
        cortex::InterruptPriority priority{ cortex::InterruptPriority::normal };
    };

    class QuadSpiStm
        : public QuadSpi
        , private cortex::InterruptHandler
    {
    public:
        using Config = QuadSpiStmConfig;

        QuadSpiStm(GpioPinStm& clock, GpioPinStm& slaveSelect, GpioPinStm& data0, GpioPinStm& data1, GpioPinStm& data2, GpioPinStm& data3, const Config& config = Config());
        ~QuadSpiStm();

        void SendData(const Header& header, infra::ConstByteRange data, Lines lines, const infra::Function<void()>& actionOnCompletion) override;
        void ReceiveData(const Header& header, infra::ByteRange data, Lines lines, const infra::Function<void()>& actionOnCompletion) override;
        void PollStatus(const Header& header, uint8_t nofBytes, uint32_t match, uint32_t mask, Lines lines, const infra::Function<void()>& actionOnCompletion) override;

    private:
        void Invoke() override;

        struct Handle
            : QSPI_HandleTypeDef
        {
            QuadSpiStm* owner = nullptr;
        };

        static void OnCompleted(QSPI_HandleTypeDef* handle);
        void RegisterCallbacks();
        void Begin(const infra::Function<void()>& actionOnCompletion);
        void Started(HAL_StatusTypeDef status);
        void PostCompletion();
        QSPI_CommandTypeDef CreateConfig(const Header& header, uint32_t dataSize, Lines lines);

    private:
        PeripheralPinStm clock;
        PeripheralPinStm slaveSelect;
        PeripheralPinStm data0;
        PeripheralPinStm data1;
        PeripheralPinStm data2;
        PeripheralPinStm data3;
        infra::AutoResetFunction<void()> onDone;
        Handle handle;
    };
}

#endif

#endif
