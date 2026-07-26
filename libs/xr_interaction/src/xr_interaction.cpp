#include "xr_interaction/xr_interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace questlab::interaction {
namespace {

float Component(const math::Vec3& vector, std::size_t axis) {
    if (axis == 0) {
        return vector.x;
    }
    if (axis == 1) {
        return vector.y;
    }
    return vector.z;
}

}  // namespace

std::optional<float> IntersectRayAabb(
    const Ray& ray,
    const Aabb& box,
    float maxDistance) {
    if (maxDistance < 0.0F ||
        box.halfExtents.x < 0.0F ||
        box.halfExtents.y < 0.0F ||
        box.halfExtents.z < 0.0F) {
        return std::nullopt;
    }

    float nearDistance = 0.0F;
    float farDistance = maxDistance;
    constexpr float kParallelEpsilon = 1.0e-7F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float origin = Component(ray.origin, axis);
        const float direction = Component(ray.direction, axis);
        const float center = Component(box.center, axis);
        const float extent = Component(box.halfExtents, axis);
        const float minimum = center - extent;
        const float maximum = center + extent;

        if (std::abs(direction) <= kParallelEpsilon) {
            if (origin < minimum || origin > maximum) {
                return std::nullopt;
            }
            continue;
        }

        float first = (minimum - origin) / direction;
        float second = (maximum - origin) / direction;
        if (first > second) {
            std::swap(first, second);
        }
        nearDistance = std::max(nearDistance, first);
        farDistance = std::min(farDistance, second);
        if (nearDistance > farDistance) {
            return std::nullopt;
        }
    }
    return nearDistance;
}

SelectionState::SelectionState(
    float pressThreshold,
    float releaseThreshold)
    : pressThreshold_(pressThreshold),
      releaseThreshold_(releaseThreshold) {
    if (releaseThreshold_ > pressThreshold_) {
        std::swap(releaseThreshold_, pressThreshold_);
    }
}

SelectionFrameResult SelectionState::Update(
    const SelectionFrameInput& input) {
    SelectionFrameResult result;
    std::array<bool, 2> triggerRising{};
    bool clearRequested = false;

    for (std::size_t hand = 0; hand < 2; ++hand) {
        const float trigger =
            std::clamp(input.triggerValues[hand], 0.0F, 1.0F);
        if (!triggerPressed_[hand] && trigger >= pressThreshold_) {
            triggerPressed_[hand] = true;
            triggerRising[hand] = true;
        } else if (
            triggerPressed_[hand] && trigger <= releaseThreshold_) {
            triggerPressed_[hand] = false;
        }

        if (input.primaryButtons[hand] && !primaryPressed_[hand]) {
            clearRequested = true;
        }
        primaryPressed_[hand] = input.primaryButtons[hand];

        if (input.rays[hand].has_value()) {
            result.hitDistances[hand] = IntersectRayAabb(
                *input.rays[hand],
                input.target,
                input.maxRayDistance);
            result.hovered[hand] = result.hitDistances[hand].has_value();
        }
    }

    if (clearRequested) {
        result.cleared = selected_;
        selected_ = false;
        result.selected = false;
        return result;
    }

    if (!selected_) {
        std::optional<std::size_t> winningHand;
        float winningDistance = std::numeric_limits<float>::max();
        for (std::size_t hand = 0; hand < 2; ++hand) {
            if (!triggerRising[hand] ||
                !result.hitDistances[hand].has_value()) {
                continue;
            }
            const float distance = *result.hitDistances[hand];
            if (!winningHand.has_value() || distance < winningDistance) {
                winningHand = hand;
                winningDistance = distance;
            }
        }
        if (winningHand.has_value()) {
            selected_ = true;
            result.selectionTriggered = true;
            result.selectingHand =
                *winningHand == 0 ? Hand::Left : Hand::Right;
        }
    }
    result.selected = selected_;
    return result;
}

void SelectionState::Reset() {
    triggerPressed_ = {};
    primaryPressed_ = {};
    selected_ = false;
}

}  // namespace questlab::interaction
