#pragma once

#include "hal/synchronous_interfaces/SynchronousRandomDataGenerator.hpp"
#include "hal_st/synchronous_stm32fxxx/SynchronousHardwareSemaphoreStm.hpp"
#include "infra/util/ProxyCreator.hpp"

namespace hal
{
    class SynchronousSynchronizedRandomDataGeneratorStm
        : public hal::SynchronousRandomDataGenerator
    {
    public:
        SynchronousSynchronizedRandomDataGeneratorStm(infra::CreatorBase<hal::SynchronousRandomDataGenerator, void()>& hwRngCreator, hal::SynchronousHardwareSemaphoreMasterStm& synchronousHardwareSemaphoreMasterStm);

        void GenerateRandomData(infra::ByteRange result) override;

    private:
        class Hsi48Enabler
        {
        public:
            Hsi48Enabler(hal::SynchronousHardwareSemaphoreMasterStm& synchronousHardwareSemaphoreMasterStm);
            ~Hsi48Enabler();

        private:
            bool disableHsi48OnFinalization = false;
        };

        infra::DelayedProxyCreator<hal::SynchronousRandomDataGenerator, void()> hwRngCreator;
        hal::SynchronousHardwareSemaphoreMasterStm& synchronousHardwareSemaphoreMasterStm;
    };
}
