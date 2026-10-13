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

    bool IsAvailable() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace questlab::rfdetr
