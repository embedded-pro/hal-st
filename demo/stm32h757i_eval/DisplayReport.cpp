#include "demo/stm32h757i_eval/DisplayReport.hpp"
#include "services/tracer/GlobalTracer.hpp"
#include DEVICE_HEADER

namespace main_
{
    namespace
    {
        uint32_t Value(const volatile uint32_t& reg)
        {
            return reg;
        }
    }

    void TraceDisplayReport(std::size_t framesShown, std::size_t underruns, uint32_t framebufferAddress)
    {
        auto trace = []()
        {
            return services::GlobalTracer().Trace();
        };

        trace() << "display dsi cr " << infra::hex << Value(DSI->CR) << " ccr " << Value(DSI->CCR) << " pconfr " << Value(DSI->PCONFR) << " pctlr " << Value(DSI->PCTLR) << " psr " << Value(DSI->PSR) << " isr0 " << Value(DSI->ISR[0]) << " isr1 " << Value(DSI->ISR[1]) << " wcfgr " << Value(DSI->WCFGR) << " wcr " << Value(DSI->WCR) << " wisr " << Value(DSI->WISR) << " wrpcr " << Value(DSI->WRPCR) << " clcr " << Value(DSI->CLCR) << " cltcr " << Value(DSI->CLTCR) << " dltcr " << Value(DSI->DLTCR);
        trace() << "display dsi video vmcr " << infra::hex << Value(DSI->VMCR) << " vpcr " << Value(DSI->VPCR) << " vccr " << Value(DSI->VCCR) << " vnpcr " << Value(DSI->VNPCR) << " vhsacr " << Value(DSI->VHSACR) << " vhbpcr " << Value(DSI->VHBPCR) << " vlcr " << Value(DSI->VLCR) << " vvsacr " << Value(DSI->VVSACR) << " vvbpcr " << Value(DSI->VVBPCR) << " vvfpcr " << Value(DSI->VVFPCR) << " vvacr " << Value(DSI->VVACR) << " lpcr " << Value(DSI->LPCR) << " lcolcr " << Value(DSI->LCOLCR);
        trace() << "display ltdc sscr " << infra::hex << Value(LTDC->SSCR) << " bpcr " << Value(LTDC->BPCR) << " awcr " << Value(LTDC->AWCR) << " twcr " << Value(LTDC->TWCR) << " gcr " << Value(LTDC->GCR) << " isr " << Value(LTDC->ISR) << " cdsr " << Value(LTDC->CDSR);
        trace() << "display ltdc layer1 cr " << infra::hex << Value(LTDC_Layer1->CR) << " whpcr " << Value(LTDC_Layer1->WHPCR) << " wvpcr " << Value(LTDC_Layer1->WVPCR) << " pfcr " << Value(LTDC_Layer1->PFCR) << " bfcr " << Value(LTDC_Layer1->BFCR) << " cfbar " << Value(LTDC_Layer1->CFBAR) << " cfblr " << Value(LTDC_Layer1->CFBLR) << " cfblnr " << Value(LTDC_Layer1->CFBLNR);
        trace() << "display gpio a moder " << infra::hex << Value(GPIOA->MODER) << " odr " << Value(GPIOA->ODR) << " idr " << Value(GPIOA->IDR) << ", f moder " << Value(GPIOF->MODER) << " odr " << Value(GPIOF->ODR) << " idr " << Value(GPIOF->IDR);
        trace() << "display rcc cr " << infra::hex << Value(RCC->CR) << " pllckselr " << Value(RCC->PLLCKSELR) << " pllcfgr " << Value(RCC->PLLCFGR) << " pll3divr " << Value(RCC->PLL3DIVR) << " d1ccipr " << Value(RCC->D1CCIPR);

        const auto* framebuffer = reinterpret_cast<const volatile uint32_t*>(framebufferAddress);
        trace() << "display frames " << framesShown << ", underruns " << underruns << ", framebuffer " << infra::hex << Value(framebuffer[0]) << " " << Value(framebuffer[1]) << ", middle " << Value(framebuffer[(240 * 800 + 400) / 2]);
    }
}
