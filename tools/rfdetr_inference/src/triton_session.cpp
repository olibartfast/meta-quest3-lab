#include "rfdetr_inference/triton_session.h"

#if defined(QUESTLAB_HAS_TRITON)
#include <http_client.h>
namespace tc = triton::client;
#endif

namespace questlab::rfdetr {

class TritonSession::Impl {
public:
#if defined(QUESTLAB_HAS_TRITON)
    std::unique_ptr<tc::InferenceServerHttpClient> client;
#endif
    std::string serverUrl;
    std::string modelName = "rfdetr_nano";
    std::string modelVersion = "1";
    std::vector<int64_t> inputShape;
    bool initialized = false;
};

TritonSession::TritonSession() : impl_(std::make_unique<Impl>()) {}
TritonSession::~TritonSession() = default;
TritonSession::TritonSession(TritonSession&&) noexcept = default;
TritonSession& TritonSession::operator=(TritonSession&&) noexcept = default;

bool TritonSession::Initialize(
    const std::string& serverUrl,
    const std::string& modelName,
    const std::string& modelVersion,
    const ModelContract& contract,
    std::string* error) {
#if !defined(QUESTLAB_HAS_TRITON)
    (void)serverUrl;
    (void)modelName;
    (void)modelVersion;
    (void)contract;
    if (error != nullptr) {
        *error = "This build has no Triton backend; configure with "
                 "-DQUESTLAB_ENABLE_TRITON=ON and TritonClientBuild_DIR "
                 "(see tools/triton/extract_triton_libs.sh, like tritonic)";
    }
    return false;
#else
    if (contract.input.shape.size() != 4U || contract.outputs.size() != 2U) {
        if (error != nullptr) {
            *error = "RF-DETR contract dimensions are invalid for Triton";
        }
        return false;
    }
    const tc::Error err =
        tc::InferenceServerHttpClient::Create(&impl_->client, serverUrl, false);
    if (!err.IsOk()) {
        if (error != nullptr) {
            *error = "Triton HTTP client creation failed: " + err.Message();
        }
        return false;
    }
    bool live = false;
    const tc::Error liveErr = impl_->client->IsServerLive(&live);
    if (!liveErr.IsOk() || !live) {
        if (error != nullptr) {
            *error = "Triton server is not live at " + serverUrl + ": " +
                     (liveErr.IsOk() ? "no response" : liveErr.Message());
        }
        impl_->client.reset();
        return false;
    }
    bool ready = false;
    const tc::Error readyErr =
        impl_->client->IsModelReady(&ready, modelName, modelVersion);
    if (!readyErr.IsOk() || !ready) {
        if (error != nullptr) {
            *error = "Triton model '" + modelName + "' is not ready: " +
                     (readyErr.IsOk() ? "not ready" : readyErr.Message());
        }
        impl_->client.reset();
        return false;
    }
    impl_->serverUrl = serverUrl;
    impl_->modelName = modelName;
    impl_->modelVersion = modelVersion;
    impl_->inputShape = contract.input.shape;
    impl_->initialized = true;
    return true;
#endif
}

bool TritonSession::Run(
    const std::vector<float>& inputTensor,
    std::vector<float>* boxes,
    std::vector<float>* logits,
    std::string* error) {
#if !defined(QUESTLAB_HAS_TRITON)
    (void)inputTensor;
    (void)boxes;
    (void)logits;
    if (error != nullptr) {
        *error = "Triton backend is not compiled in";
    }
    return false;
#else
    if (!impl_->initialized || impl_->client == nullptr) {
        if (error != nullptr) {
            *error = "Triton session is not initialized";
        }
        return false;
    }
    if (boxes == nullptr || logits == nullptr) {
        if (error != nullptr) {
            *error = "Triton output pointer is null";
        }
        return false;
    }
    const std::vector<int64_t> shape = impl_->inputShape;
    size_t expected = 1;
    for (int64_t dim : shape) {
        expected *= static_cast<size_t>(dim);
    }
    if (inputTensor.size() != expected) {
        if (error != nullptr) {
            *error = "Triton input tensor size does not match the contract";
        }
        return false;
    }
    // Triton HTTP client with max_batch_size=0 takes the full [1,3,384,384]
    // shape as-is (same convention as tritonic preprocessed mode).
    tc::InferInput* rawInput = nullptr;
    tc::Error err = tc::InferInput::Create(
        &rawInput, "input", shape, "FP32");
    if (!err.IsOk()) {
        if (error != nullptr) {
            *error = "Triton input creation failed: " + err.Message();
        }
        return false;
    }
    std::unique_ptr<tc::InferInput> input(rawInput);
    const auto* bytePtr =
        reinterpret_cast<const uint8_t*>(inputTensor.data());
    std::vector<uint8_t> rawBytes(
        bytePtr, bytePtr + inputTensor.size() * sizeof(float));
    err = input->AppendRaw(rawBytes);
    if (!err.IsOk()) {
        if (error != nullptr) {
            *error = "Triton input AppendRaw failed: " + err.Message();
        }
        return false;
    }
    tc::InferRequestedOutput* rawDets = nullptr;
    tc::InferRequestedOutput* rawLabels = nullptr;
    err = tc::InferRequestedOutput::Create(&rawDets, "dets");
    if (!err.IsOk()) {
        if (error != nullptr) {
            *error = "Triton output creation failed: " + err.Message();
        }
        return false;
    }
    err = tc::InferRequestedOutput::Create(&rawLabels, "labels");
    if (!err.IsOk()) {
        delete rawDets;
        if (error != nullptr) {
            *error = "Triton output creation failed: " + err.Message();
        }
        return false;
    }
    std::unique_ptr<tc::InferRequestedOutput> detsOut(rawDets);
    std::unique_ptr<tc::InferRequestedOutput> labelsOut(rawLabels);
    std::vector<tc::InferInput*> inputPtrs{input.get()};
    std::vector<const tc::InferRequestedOutput*> outputPtrs{
        detsOut.get(), labelsOut.get()};
    tc::InferOptions options(impl_->modelName);
    options.model_version_ = impl_->modelVersion;
    tc::InferResult* rawResult = nullptr;
    err = impl_->client->Infer(
        &rawResult, options, inputPtrs, outputPtrs);
    if (!err.IsOk()) {
        if (error != nullptr) {
            *error = "Triton inference failed: " + err.Message();
        }
        return false;
    }
    std::unique_ptr<tc::InferResult> result(rawResult);
    const uint8_t* detsBytesPtr = nullptr;
    size_t detsBytes = 0;
    err = result->RawData("dets", &detsBytesPtr, &detsBytes);
    if (!err.IsOk()) {
        if (error != nullptr) {
            *error = "Triton dets output missing: " + err.Message();
        }
        return false;
    }
    const uint8_t* labelsBytesPtr = nullptr;
    size_t labelsBytes = 0;
    err = result->RawData("labels", &labelsBytesPtr, &labelsBytes);
    if (!err.IsOk()) {
        if (error != nullptr) {
            *error = "Triton labels output missing: " + err.Message();
        }
        return false;
    }
    const auto* detsPtr = reinterpret_cast<const float*>(detsBytesPtr);
    const auto* labelsPtr = reinterpret_cast<const float*>(labelsBytesPtr);
    boxes->assign(detsPtr, detsPtr + detsBytes / sizeof(float));
    logits->assign(labelsPtr, labelsPtr + labelsBytes / sizeof(float));
    return true;
#endif
}

bool TritonSession::IsAvailable() const {
#if defined(QUESTLAB_HAS_TRITON)
    return impl_->initialized && impl_->client != nullptr;
#else
    return false;
#endif
}

}  // namespace questlab::rfdetr
