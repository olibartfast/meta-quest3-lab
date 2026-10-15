#pragma once

#include "rfdetr_inference/model_contract.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace questlab::rfdetr {

struct Detection {
    int32_t classId = -1;
    std::string className;
    float confidence = 0.0F;
    std::array<float, 4> boxXyxy{};
    // Screen-corner extension (QLD2 services): quad in source pixels
    // (TL, TR, BR, BL). Present only when the service fitted a quad; the
    // diagnostic preview outlines the quad instead of the box.
    bool hasCorners = false;
    std::array<float, 8> cornerXy{};
    // Instance mask from a seg contract, decoded to source-image pixels
    // (row-major binary, size maskWidth * maskHeight). Empty when the
    // contract has no masks or this detection carries none.
    bool hasMask = false;
    int32_t maskWidth = 0;
    int32_t maskHeight = 0;
    std::vector<uint8_t> mask;
};

struct ComparisonResult {
    bool matches = false;
    std::string message;
};

bool PostprocessDetections(
    const std::vector<float>& normalizedCxcywh,
    const std::vector<float>& classLogits,
    int32_t sourceWidth,
    int32_t sourceHeight,
    const ModelContract& contract,
    std::vector<Detection>* detections,
    std::string* error);

// Seg-contract overload: attaches each kept detection's query mask,
// decoded from mask logits [queries * maskH * maskW] to a source-size
// binary mask (nearest-neighbour upsample, logit > maskThreshold keeps
// the pixel). Mirrors rf-detr-cpp-inference's resize_threshold_mask at
// threshold 0.0 on raw logits. Box-only contracts behave exactly like
// PostprocessDetections; masks stay empty.
bool PostprocessDetectionsWithMasks(
    const std::vector<float>& normalizedCxcywh,
    const std::vector<float>& classLogits,
    const std::vector<float>& maskLogits,
    int32_t sourceWidth,
    int32_t sourceHeight,
    float maskThreshold,
    const ModelContract& contract,
    std::vector<Detection>* detections,
    std::string* error);

// Row-major binary-mask RLE (run-length, value uint32 pairs) for the
// streaming wire format: 300 full-frame float masks are too large to
// send per frame. Runs cap at one row so decoding needs no width.
std::vector<uint32_t> EncodeMaskRle(
    const std::vector<uint8_t>& mask,
    int32_t width,
    int32_t height);

bool DecodeMaskRle(
    const std::vector<uint32_t>& runs,
    int32_t width,
    int32_t height,
    std::vector<uint8_t>* mask,
    std::string* error);

float BoxIou(
    const std::array<float, 4>& first,
    const std::array<float, 4>& second);

ComparisonResult CompareDetections(
    const std::vector<Detection>& expected,
    const std::vector<Detection>& actual,
    const ModelContract& contract);

}  // namespace questlab::rfdetr
