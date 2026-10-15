#include "pose_solver/pose_solver.h"

namespace questlab::pose {

bool SolvePose(
    const Correspondence* /*correspondences*/,
    int /*count*/,
    const Intrinsics& /*intrinsics*/,
    const Options& /*options*/,
    Result* /*result*/,
    std::string* error) {
    if (error != nullptr) {
        *error = "not implemented";
    }
    return false;
}

}  // namespace questlab::pose
