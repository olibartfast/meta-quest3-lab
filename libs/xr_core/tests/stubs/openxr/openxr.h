#pragma once

#include <cstdint>

using XrTime = std::int64_t;
using XrInstance = std::uint64_t;
using XrSystemId = std::uint64_t;
using XrSession = std::uint64_t;

struct XrEventDataBuffer {
    std::int32_t type = 0;
};

struct XrCompositionLayerBaseHeader;
