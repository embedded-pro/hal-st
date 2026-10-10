#pragma once

#include "hal/interfaces/Surface.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstdint>

namespace main_
{
    inline hal::Surface MakeSurface(infra::ByteRange memory, hal::DisplaySize size, hal::SurfaceFormat format)
    {
        const auto stride = static_cast<uint32_t>(hal::BytesPerRow(size.width, format));
        return { infra::Head(memory, stride * size.height), size, stride, format };
    }
}
