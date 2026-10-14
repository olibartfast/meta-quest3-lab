// Anchor translation unit for the quest_vision shared library.
//
// The library is an umbrella: all computer-vision objects come from the
// statically linked pipeline archives (xr_math, camera_source,
// detection_projection, detection_fusion, object_detector), pulled
// whole-archive. The only public surface is the C ABI in
// quest_vision_c_api.h (version via QvVersion); this TU carries the
// configured version string so a mismatch fails the build, not at load.
#include "quest_vision/version.h"

static_assert(
    sizeof(QUEST_VISION_VERSION_STRING) > sizeof("quest_vision "),
    "quest_vision version string is unset");
