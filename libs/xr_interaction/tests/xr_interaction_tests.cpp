#include "xr_interaction/xr_interaction.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using questlab::interaction::Aabb;
using questlab::interaction::Hand;
using questlab::interaction::IntersectRayAabb;
using questlab::interaction::Ray;
using questlab::interaction::SelectionFrameInput;
using questlab::interaction::SelectionState;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

Ray ForwardRay(float x = 0.0F) {
    return {{x, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}};
}

}  // namespace

int main() {
    const Aabb box{{0.0F, 0.0F, -2.0F}, {0.25F, 0.25F, 0.25F}};
    const auto hit = IntersectRayAabb(ForwardRay(), box, 3.0F);
    Require(hit.has_value() && std::abs(*hit - 1.75F) < 0.0001F,
            "forward ray hits the front face");
    Require(!IntersectRayAabb(ForwardRay(1.0F), box, 3.0F).has_value(),
            "parallel ray outside the slab misses");
    Require(IntersectRayAabb(
                {{0.0F, 0.0F, -2.0F}, {1.0F, 0.0F, 0.0F}},
                box,
                3.0F).value_or(-1.0F) == 0.0F,
            "ray starting inside returns zero");
    Require(!IntersectRayAabb(
                {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
                box,
                3.0F).has_value(),
            "box behind ray misses");
    Require(!IntersectRayAabb(ForwardRay(), box, 1.5F).has_value(),
            "maximum ray distance is enforced");

    SelectionState state;
    SelectionFrameInput input;
    input.target = box;
    input.rays[0] = ForwardRay();
    input.triggerValues[0] = 0.74F;
    Require(!state.Update(input).selectionTriggered,
            "below press threshold does not select");
    input.triggerValues[0] = 0.75F;
    Require(state.Update(input).selectionTriggered,
            "exact press threshold selects");
    Require(!state.Update(input).selectionTriggered,
            "held trigger emits one edge");

    input.primaryButtons[1] = true;
    const auto cleared = state.Update(input);
    Require(cleared.cleared && !cleared.selected,
            "primary rising edge clears selection");
    input.primaryButtons[1] = false;
    input.triggerValues[0] = 0.56F;
    state.Update(input);
    input.triggerValues[0] = 0.55F;
    state.Update(input);
    input.triggerValues = {0.75F, 0.75F};
    input.rays[1] = ForwardRay();
    const auto simultaneous = state.Update(input);
    Require(simultaneous.selectionTriggered &&
                simultaneous.selectingHand == Hand::Left,
            "simultaneous equal hits choose left deterministically");

    std::cout << "xr_interaction tests passed\n";
    return 0;
}
