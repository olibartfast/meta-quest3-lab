#pragma once

#include "xr_math/xr_math.h"

#include <array>
#include <cstddef>
#include <optional>

namespace questlab::interaction {

enum class Hand : std::size_t {
    Left = 0,
    Right = 1,
};

struct Ray {
    math::Vec3 origin{};
    math::Vec3 direction{0.0F, 0.0F, -1.0F};
};

struct Aabb {
    math::Vec3 center{};
    math::Vec3 halfExtents{0.5F, 0.5F, 0.5F};
};

std::optional<float> IntersectRayAabb(
    const Ray& ray,
    const Aabb& box,
    float maxDistance);

struct SelectionFrameInput {
    std::array<std::optional<Ray>, 2> rays{};
    std::array<float, 2> triggerValues{};
    std::array<bool, 2> primaryButtons{};
    Aabb target{};
    float maxRayDistance = 3.0F;
};

struct SelectionFrameResult {
    std::array<bool, 2> hovered{};
    std::array<std::optional<float>, 2> hitDistances{};
    bool selected = false;
    bool selectionTriggered = false;
    Hand selectingHand = Hand::Left;
    bool cleared = false;
};

class SelectionState {
public:
    SelectionState(
        float pressThreshold = 0.75F,
        float releaseThreshold = 0.55F);

    SelectionFrameResult Update(const SelectionFrameInput& input);
    bool IsSelected() const { return selected_; }
    void Reset();

private:
    float pressThreshold_ = 0.75F;
    float releaseThreshold_ = 0.55F;
    std::array<bool, 2> triggerPressed_{};
    std::array<bool, 2> primaryPressed_{};
    bool selected_ = false;
};

struct PinchFrameResult {
    bool active = false;
    bool started = false;
    bool ended = false;
    float distance = 0.0F;
    math::Vec3 center{};
};

class PinchState {
public:
    PinchState(
        float pressThreshold = 0.025F,
        float releaseThreshold = 0.040F);

    PinchFrameResult Update(
        const std::optional<math::Vec3>& thumbTip,
        const std::optional<math::Vec3>& indexTip);
    bool IsActive() const { return active_; }
    void Reset();

private:
    float pressThreshold_ = 0.025F;
    float releaseThreshold_ = 0.040F;
    bool active_ = false;
};

}  // namespace questlab::interaction
