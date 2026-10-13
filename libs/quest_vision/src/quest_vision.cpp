// Anchor translation unit for the quest_vision shared library.
//
// The library is an umbrella: all computer-vision objects come from the
// statically linked pipeline archives (xr_math, camera_source,
// detection_projection, detection_fusion, object_detector). This file
// exists so the SHARED target always produces quest_vision.so /
// quest_vision.dll, and carries the single version symbol consumers can
// probe at runtime.
#include <string>

namespace questlab::vision {

const char* Version() {
    return "quest_vision 1.0.0";
}

}  // namespace questlab::vision
