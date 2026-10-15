#pragma once

#include "rfdetr_inference/model_contract.h"

#include <memory>
#include <string>
#include <vector>

namespace questlab::rfdetr {

// Triton-hosted RF-DETR inference over HTTP, mirroring OnnxSession::Run.
// Preprocessing and postprocessing stay local so results match the pinned
// host oracle; only tensor execution moves to the Triton server.
// Same wire as tritonic (Triton C++ HTTP client, r26.08).
class TritonSession {
public:
    TritonSession();
    ~TritonSession();
    TritonSession(TritonSession&&) noexcept;
    TritonSession& operator=(TritonSession&&) noexcept;

    TritonSession(const TritonSession&) = delete;
    TritonSession& operator=(const TritonSession&) = delete;

    bool Initialize(
        const std::string& serverUrl,
        const std::string& modelName,
        const std::string& modelVersion,
        const ModelContract& contract,
        std::string* error);

    bool Run(
        const std::vector<float>& inputTensor,
        std::vector<float>* boxes,
        std::vector<float>* logits,
        std::string* error);

    // Mask-capable run for seg contracts (dets + labels + masks), mirroring
    // OnnxSession::RunWithMasks: masks receives the raw third-output floats
    // and maskShape its contract shape; both may be null to skip mask
    // retrieval. Box-only contracts behave exactly like Run. Run itself
    // drops masks; prefer this overload when contract.hasMasks is true.
    bool RunWithMasks(
        const std::vector<float>& inputTensor,
        std::vector<float>* boxes,
        std::vector<float>* logits,
        std::vector<float>* masks,
        std::vector<int64_t>* maskShape,
        std::string* error);

    bool IsAvailable() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace questlab::rfdetr
