#include "hal_st/stm32fxxx/QuadSpiStm.hpp"
#include "infra/event/EventDispatcher.hpp"

#if defined(HAS_PERIPHERAL_QUADSPI)

namespace hal
{
    QuadSpiStm::QuadSpiStm(GpioPinStm& clock, GpioPinStm& slaveSelect, GpioPinStm& data0, GpioPinStm& data1, GpioPinStm& data2, GpioPinStm& data3, const Config& config)
        : clock(clock, PinConfigTypeStm::quadSpiClock, 0)
        , slaveSelect(slaveSelect, PinConfigTypeStm::quadSpiSlaveSelect, 0)
        , data0(data0, PinConfigTypeStm::quadSpiData0, 0)
        , data1(data1, PinConfigTypeStm::quadSpiData1, 0)
        , data2(data2, PinConfigTypeStm::quadSpiData2, 0)
        , data3(data3, PinConfigTypeStm::quadSpiData3, 0)
        , handle()
    {
        __QSPI_CLK_ENABLE();

        handle.Instance = QUADSPI;

        handle.Init.ClockPrescaler = config.prescaler;
        // An indirect read stalls when the FIFO is full and resumes only once 4 bytes are free, so a threshold above
        // the FIFO size minus 4 never raises FTF again while HAL_QSPI_Receive drains it one byte at a time
        handle.Init.FifoThreshold = 1;
        handle.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
        handle.Init.FlashSize = config.flashSizeLog2 - 1;
        handle.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
        handle.Init.ClockMode = QSPI_CLOCK_MODE_0;
#if !defined(STM32WB)
        handle.Init.FlashID = QSPI_FLASH_ID_1;
        handle.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
#endif

        HAL_QSPI_Init(&handle);
    }

    QuadSpiStm::~QuadSpiStm()
    {
        HAL_QSPI_DeInit(&handle);
        __QSPI_CLK_DISABLE();
    }

    void QuadSpiStm::SendData(const Header& header, infra::ConstByteRange data, Lines lines, const infra::Function<void()>& actionOnCompletion)
    {
        assert(!onDone);
        onDone = actionOnCompletion;

        QSPI_CommandTypeDef command = CreateConfig(header, data.size(), lines);
        HAL_StatusTypeDef status = HAL_QSPI_Command(&handle, &command, HAL_QSPI_TIMEOUT_DEFAULT_VALUE);
        if (status == HAL_OK && !data.empty())
            status = HAL_QSPI_Transmit(&handle, const_cast<uint8_t*>(data.begin()), HAL_QSPI_TIMEOUT_DEFAULT_VALUE);

        Complete(status);
    }

    void QuadSpiStm::ReceiveData(const Header& header, infra::ByteRange data, Lines lines, const infra::Function<void()>& actionOnCompletion)
    {
        assert(!onDone);
        onDone = actionOnCompletion;

        QSPI_CommandTypeDef command = CreateConfig(header, data.size(), lines);
        HAL_StatusTypeDef status = HAL_QSPI_Command(&handle, &command, HAL_QSPI_TIMEOUT_DEFAULT_VALUE);
        if (status == HAL_OK && !data.empty())
            status = HAL_QSPI_Receive(&handle, data.begin(), HAL_QSPI_TIMEOUT_DEFAULT_VALUE);

        Complete(status);
    }

    void QuadSpiStm::PollStatus(const Header& header, uint8_t nofBytes, uint32_t match, uint32_t mask, Lines lines, const infra::Function<void()>& actionOnCompletion)
    {
        onDone = actionOnCompletion;

        QSPI_CommandTypeDef command = CreateConfig(header, nofBytes, lines);

        QSPI_AutoPollingTypeDef config = {};
        config.Match = match;
        config.Mask = mask;
        config.Interval = 16;
        config.StatusBytesSize = nofBytes;
        config.MatchMode = QSPI_MATCH_MODE_AND;
        config.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

        HAL_StatusTypeDef status = HAL_QSPI_AutoPolling(&handle, &command, &config, HAL_MAX_DELAY);
        assert(status == HAL_OK);

        infra::EventDispatcher::Instance().Schedule([this]()
            {
                onDone();
            });
    }

    void QuadSpiStm::Complete(HAL_StatusTypeDef status)
    {
        if (status != HAL_OK)
        {
            // The blocking HAL calls leave the handle READY or ERROR after a timeout, and HAL_QSPI_Abort acts only on a busy handle
            handle.State = HAL_QSPI_STATE_BUSY;
            HAL_QSPI_Abort(&handle);
            onDone = nullptr;
            return;
        }

        infra::EventDispatcher::Instance().Schedule([this]()
            {
                onDone();
            });
    }

    QSPI_CommandTypeDef QuadSpiStm::CreateConfig(const Header& header, uint32_t dataSize, Lines lines)
    {
        QSPI_CommandTypeDef command;

        command.Instruction = header.instruction.value_or(0);
        command.Address = VectorToAddress(header.address);
        command.AddressSize = ((header.address.size() - 1) & 0x03) << 12;
        command.AlternateBytes = VectorToAddress(header.alternate);
        command.AlternateBytesSize = ((header.alternate.size() - 1) & 0x03) << 16;
        command.DummyCycles = header.nofDummyCycles;
        command.InstructionMode = header.instruction ? (lines.instructionLines == 4 ? QSPI_INSTRUCTION_4_LINES : QSPI_INSTRUCTION_1_LINE) : QSPI_INSTRUCTION_NONE;
        command.AddressMode = !header.address.empty() ? (lines.addressLines == 4 ? QSPI_ADDRESS_4_LINES : QSPI_ADDRESS_1_LINE) : QSPI_ADDRESS_NONE;
        command.AlternateByteMode = !header.alternate.empty() ? (lines.alternateLines == 4 ? QSPI_ALTERNATE_BYTES_4_LINES : QSPI_ALTERNATE_BYTES_1_LINE) : QSPI_ALTERNATE_BYTES_NONE;
        command.DataMode = dataSize != 0 ? (lines.dataLines == 4 ? QSPI_DATA_4_LINES : QSPI_DATA_1_LINE) : QSPI_DATA_NONE;
        command.NbData = dataSize;
        command.DdrMode = QSPI_DDR_MODE_DISABLE;
#if !defined(STM32WB)
        command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
#endif
        command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

        return command;
    }
}

#endif
