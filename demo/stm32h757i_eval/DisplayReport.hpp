#pragma once

#include <cstddef>
#include <cstdint>

namespace main_
{
    void TraceDisplayReport(std::size_t framesShown, std::size_t underruns, uint32_t framebufferAddress);
}
