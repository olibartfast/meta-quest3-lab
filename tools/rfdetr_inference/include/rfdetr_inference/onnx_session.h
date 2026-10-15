#pragma once

#include "rfdetr_inference/model_contract.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace questlab::rfdetr {

struct RuntimeOptions {
    std::string executionProvider;
    int32_t xnnpackThreads = 0;
};

class OnnxSession {
public:
    OnnxSession();
    ~OnnxSession();
    OnnxSession(OnnxSession&&) noexcept;
    OnnxSession& operator=(OnnxSession&&) noexcept;

    OnnxSession(const OnnxSession&) = delete;
    OnnxSession& operator=(const OnnxSession&) = delete;

    bool Initialize(
        const std::filesystem::path& modelPath,
        const ModelContract& contract,
        std::string* error);

    bool Initialize(
        const std::filesystem::path& modelPath,
        const ModelContract& contract,
        const RuntimeOptions& runtimeOptions,
        std::string* error);

    bool Run(
        const std::vector<float>& inputTensor,
        std::vector<float>* boxes,
        std::vector<float>* logits,
        std::string* error);

    // Mask-capable run for seg contracts (dets + labels + masks). masks
    // receives the raw third-output floats and maskShape its tensor shape;
    // both may be null to skip mask retrieval. Box-only contracts behave
    // exactly like Run. Run itself drops masks; prefer this overload when
    // contract.hasMasks is true.
    bool RunWithMasks(
        const std::vector<float>& inputTensor,
        std::vector<float>* boxes,
        std::vector<float>* logits,
        std::vector<float>* masks,
        std::vector<int64_t>* maskShape,
        std::string* error);

    bool IsAvailable() const;
    void RequestCancellation();
    std::string RuntimeDescription() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace questlab::rfdetr
