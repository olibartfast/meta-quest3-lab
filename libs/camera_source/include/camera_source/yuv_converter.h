#pragma once

#include "camera_source/camera_source.h"

#include <cstdint>
#include <vector>

namespace questlab::camera {

bool ConvertYuv420ToRgba(
    const RgbCapture& capture,
    std::vector<uint8_t>* rgba);

}  // namespace questlab::camera
