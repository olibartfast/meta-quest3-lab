#include "rfdetr_inference/postprocessing.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace questlab::rfdetr {
namespace {

struct Candidate {
    float score = 0.0F;
    size_t flatIndex = 0;
};

float Sigmoid(float value) {
    if (value >= 0.0F) {
        return 1.0F / (1.0F + std::exp(-value));
    }
    const float exponential = std::exp(value);
    return exponential / (1.0F + exponential);
}

}  // namespace

bool PostprocessDetections(
    const std::vector<float>& normalizedCxcywh,
    const std::vector<float>& classLogits,
    int32_t sourceWidth,
    int32_t sourceHeight,
    const ModelContract& contract,
    std::vector<Detection>* detections,
    std::string* error) {
    if (contract.hasMasks) {
        if (error != nullptr) {
            *error = "Seg contract needs PostprocessDetectionsWithMasks";
        }
        return false;
    }
    return PostprocessDetectionsWithMasks(
        normalizedCxcywh,
        classLogits,
        {},
        sourceWidth,
        sourceHeight,
        0.0F,
        contract,
        detections,
        error);
}

bool PostprocessDetectionsWithMasks(
    const std::vector<float>& normalizedCxcywh,
    const std::vector<float>& classLogits,
    const std::vector<float>& maskLogits,
    int32_t sourceWidth,
    int32_t sourceHeight,
    float maskThreshold,
    const ModelContract& contract,
    std::vector<Detection>* detections,
    std::string* error) {
    if (detections == nullptr) {
        if (error != nullptr) {
            *error = "Detection output pointer is null";
        }
        return false;
    }
    detections->clear();
    if (sourceWidth <= 0 || sourceHeight <= 0 ||
        (contract.outputs.size() != 2U && contract.outputs.size() != 3U) ||
        contract.outputs[0].shape.size() != 3U ||
        contract.outputs[1].shape.size() != 3U) {
        if (error != nullptr) {
            *error = "Invalid source or output contract dimensions";
        }
        return false;
    }
    if (contract.hasMasks) {
        if (contract.outputs.size() != 3U || contract.maskHeight <= 0 ||
            contract.maskWidth <= 0 || !std::isfinite(maskThreshold)) {
            if (error != nullptr) {
                *error = "Seg contract mask dimensions are invalid";
            }
            return false;
        }
        const size_t expectedMasks =
            static_cast<size_t>(contract.outputs[0].shape[1]) *
            static_cast<size_t>(contract.maskHeight) *
            static_cast<size_t>(contract.maskWidth);
        if (maskLogits.size() != expectedMasks) {
            if (error != nullptr) {
                *error = "RF-DETR mask tensor size does not match the manifest";
            }
            return false;
        }
        for (float value : maskLogits) {
            if (!std::isfinite(value)) {
                if (error != nullptr) {
                    *error = "RF-DETR mask output contains NaN or infinity";
                }
                return false;
            }
        }
    }
    const size_t queryCount =
        static_cast<size_t>(contract.outputs[0].shape[1]);
    const size_t classCount =
        static_cast<size_t>(contract.outputs[1].shape[2]);
    if (normalizedCxcywh.size() != queryCount * 4U ||
        classLogits.size() != queryCount * classCount) {
        if (error != nullptr) {
            *error = "RF-DETR output tensor size does not match the manifest";
        }
        return false;
    }
    for (float value : normalizedCxcywh) {
        if (!std::isfinite(value)) {
            if (error != nullptr) {
                *error = "RF-DETR box output contains NaN or infinity";
            }
            return false;
        }
    }

    std::vector<Candidate> candidates;
    candidates.reserve(classLogits.size());
    for (size_t index = 0; index < classLogits.size(); ++index) {
        if (!std::isfinite(classLogits[index])) {
            if (error != nullptr) {
                *error = "RF-DETR label output contains NaN or infinity";
            }
            return false;
        }
        candidates.push_back({Sigmoid(classLogits[index]), index});
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& first, const Candidate& second) {
            if (first.score != second.score) {
                return first.score > second.score;
            }
            return first.flatIndex < second.flatIndex;
        });
    const size_t selectedCount = std::min(
        static_cast<size_t>(contract.maximumDetections), candidates.size());
    detections->reserve(selectedCount);
    for (size_t index = 0; index < selectedCount; ++index) {
        const Candidate& candidate = candidates[index];
        if (candidate.score <= contract.confidenceThreshold) {
            continue;
        }
        const size_t queryIndex = candidate.flatIndex / classCount;
        const int32_t classId =
            static_cast<int32_t>(candidate.flatIndex % classCount);
        const auto className = contract.classNames.find(classId);
        if (className == contract.classNames.end()) {
            if (error != nullptr) {
                *error = "Selected RF-DETR logit has no COCO sparse label: " +
                         std::to_string(classId);
            }
            return false;
        }
        const size_t boxOffset = queryIndex * 4U;
        const float centerX = normalizedCxcywh[boxOffset];
        const float centerY = normalizedCxcywh[boxOffset + 1U];
        const float width = normalizedCxcywh[boxOffset + 2U];
        const float height = normalizedCxcywh[boxOffset + 3U];
        if (width < 0.0F || height < 0.0F) {
            if (error != nullptr) {
                *error = "RF-DETR box output contains a negative extent";
            }
            return false;
        }
        Detection detection;
        detection.classId = classId;
        detection.className = className->second;
        detection.confidence = candidate.score;
        detection.boxXyxy = {
            std::clamp(
                (centerX - width * 0.5F) * static_cast<float>(sourceWidth),
                0.0F,
                static_cast<float>(sourceWidth)),
            std::clamp(
                (centerY - height * 0.5F) * static_cast<float>(sourceHeight),
                0.0F,
                static_cast<float>(sourceHeight)),
            std::clamp(
                (centerX + width * 0.5F) * static_cast<float>(sourceWidth),
                0.0F,
                static_cast<float>(sourceWidth)),
            std::clamp(
                (centerY + height * 0.5F) * static_cast<float>(sourceHeight),
                0.0F,
                static_cast<float>(sourceHeight)),
        };
        if (detection.boxXyxy[2] < detection.boxXyxy[0] ||
            detection.boxXyxy[3] < detection.boxXyxy[1]) {
            if (error != nullptr) {
                *error = "RF-DETR box becomes inverted after source mapping";
            }
            return false;
        }
        if (contract.hasMasks) {
            // Nearest-neighbour upsample of this query's mask grid to
            // source pixels; a logit above threshold keeps the pixel.
            // Same convention as rf-detr-cpp-inference's
            // resize_threshold_mask at threshold 0.0 on raw logits.
            const int64_t gridH = contract.maskHeight;
            const int64_t gridW = contract.maskWidth;
            const size_t gridStep =
                static_cast<size_t>(gridH) * static_cast<size_t>(gridW);
            const size_t gridBase = queryIndex * gridStep;
            detection.hasMask = true;
            detection.maskWidth = sourceWidth;
            detection.maskHeight = sourceHeight;
            detection.mask.resize(
                static_cast<size_t>(sourceWidth) *
                static_cast<size_t>(sourceHeight));
            for (int32_t y = 0; y < sourceHeight; ++y) {
                const int64_t gridY =
                    static_cast<int64_t>(y) * gridH / sourceHeight;
                for (int32_t x = 0; x < sourceWidth; ++x) {
                    const int64_t gridX =
                        static_cast<int64_t>(x) * gridW / sourceWidth;
                    const float logit = maskLogits
                        [gridBase +
                         static_cast<size_t>(gridY) *
                             static_cast<size_t>(gridW) +
                         static_cast<size_t>(gridX)];
                    detection.mask
                        [static_cast<size_t>(y) *
                             static_cast<size_t>(sourceWidth) +
                         static_cast<size_t>(x)] =
                            logit > maskThreshold ? 1U : 0U;
                }
            }
        }
        detections->push_back(std::move(detection));
    }
    return true;
}

float BoxIou(
    const std::array<float, 4>& first,
    const std::array<float, 4>& second) {
    const float intersectionWidth = std::max(
        0.0F, std::min(first[2], second[2]) - std::max(first[0], second[0]));
    const float intersectionHeight = std::max(
        0.0F, std::min(first[3], second[3]) - std::max(first[1], second[1]));
    const float intersection = intersectionWidth * intersectionHeight;
    const float firstArea = std::max(0.0F, first[2] - first[0]) *
                            std::max(0.0F, first[3] - first[1]);
    const float secondArea = std::max(0.0F, second[2] - second[0]) *
                             std::max(0.0F, second[3] - second[1]);
    const float unionArea = firstArea + secondArea - intersection;
    return unionArea > 0.0F ? intersection / unionArea : 0.0F;
}

ComparisonResult CompareDetections(
    const std::vector<Detection>& expected,
    const std::vector<Detection>& actual,
    const ModelContract& contract) {
    if (expected.size() != actual.size()) {
        return {
            false,
            "Detection count differs: expected " +
                std::to_string(expected.size()) + ", actual " +
                std::to_string(actual.size()),
        };
    }
    std::vector<size_t> expectedOrder(expected.size());
    for (size_t index = 0; index < expectedOrder.size(); ++index) {
        expectedOrder[index] = index;
    }
    std::sort(
        expectedOrder.begin(),
        expectedOrder.end(),
        [&](size_t first, size_t second) {
            return expected[first].confidence > expected[second].confidence;
        });
    std::vector<bool> used(actual.size(), false);
    for (size_t expectedIndex : expectedOrder) {
        size_t bestIndex = actual.size();
        float bestIou = -1.0F;
        for (size_t actualIndex = 0; actualIndex < actual.size(); ++actualIndex) {
            if (used[actualIndex] ||
                actual[actualIndex].classId != expected[expectedIndex].classId ||
                actual[actualIndex].className !=
                    expected[expectedIndex].className) {
                continue;
            }
            const float iou = BoxIou(
                expected[expectedIndex].boxXyxy,
                actual[actualIndex].boxXyxy);
            if (iou > bestIou) {
                bestIou = iou;
                bestIndex = actualIndex;
            }
        }
        if (bestIndex == actual.size()) {
            return {
                false,
                "No exact class ID and label match for expected class ID " +
                    std::to_string(expected[expectedIndex].classId) +
                    " (" + expected[expectedIndex].className + ")",
            };
        }
        const float confidenceDelta = std::fabs(
            expected[expectedIndex].confidence - actual[bestIndex].confidence);
        if (confidenceDelta > contract.maximumAbsoluteConfidenceDelta) {
            std::ostringstream message;
            message << "Confidence delta " << confidenceDelta
                    << " exceeds "
                    << contract.maximumAbsoluteConfidenceDelta;
            return {false, message.str()};
        }
        if (bestIou < contract.minimumBoxIou) {
            std::ostringstream message;
            message << "Box IoU " << bestIou << " is below "
                    << contract.minimumBoxIou;
            return {false, message.str()};
        }
        used[bestIndex] = true;
    }
    return {true, "Detections match"};
}

std::vector<uint32_t> EncodeMaskRle(
    const std::vector<uint8_t>& mask,
    int32_t width,
    int32_t height) {
    std::vector<uint32_t> runs;
    if (width <= 0 || height <= 0 ||
        mask.size() !=
            static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return runs;
    }
    // One run list per row: (runLength, value) pairs, so a dropped tail
    // corrupts one row, never the row alignment of the whole mask.
    for (int32_t y = 0; y < height; ++y) {
        const size_t base =
            static_cast<size_t>(y) * static_cast<size_t>(width);
        uint32_t length = 1;
        for (int32_t x = 1; x < width; ++x) {
            if (mask[base + static_cast<size_t>(x)] ==
                mask[base + static_cast<size_t>(x) - 1]) {
                ++length;
                continue;
            }
            runs.push_back(length);
            runs.push_back(mask[base + static_cast<size_t>(x) - 1] ? 1U : 0U);
            length = 1;
        }
        runs.push_back(length);
        runs.push_back(
            mask[base + static_cast<size_t>(width) - 1] ? 1U : 0U);
    }
    return runs;
}

bool DecodeMaskRle(
    const std::vector<uint32_t>& runs,
    int32_t width,
    int32_t height,
    std::vector<uint8_t>* mask,
    std::string* error) {
    if (mask == nullptr) {
        if (error != nullptr) {
            *error = "Mask output pointer is null";
        }
        return false;
    }
    mask->clear();
    if (width <= 0 || height <= 0 || runs.size() % 2U != 0U) {
        if (error != nullptr) {
            *error = "Invalid mask RLE dimensions";
        }
        return false;
    }
    mask->reserve(
        static_cast<size_t>(width) * static_cast<size_t>(height));
    size_t cursor = 0;
    for (int32_t y = 0; y < height; ++y) {
        int32_t filled = 0;
        while (filled < width) {
            if (cursor + 2U > runs.size()) {
                if (error != nullptr) {
                    *error = "Truncated mask RLE run";
                }
                mask->clear();
                return false;
            }
            const uint32_t length = runs[cursor];
            const uint32_t value = runs[cursor + 1U];
            cursor += 2U;
            if (length == 0U || value > 1U ||
                filled + static_cast<int32_t>(length) > width) {
                if (error != nullptr) {
                    *error = "Invalid mask RLE run";
                }
                mask->clear();
                return false;
            }
            mask->insert(
                mask->end(), length, value ? 1U : 0U);
            filled += static_cast<int32_t>(length);
        }
    }
    if (cursor != runs.size()) {
        if (error != nullptr) {
            *error = "Trailing mask RLE data";
        }
        mask->clear();
        return false;
    }
    return true;
}

}  // namespace questlab::rfdetr
