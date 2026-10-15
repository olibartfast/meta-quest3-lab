#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace questlab::triton {

// One FP32 tensor for the KServe v2 HTTP protocol. For outputs the caller
// pre-fills name/shape (from the pinned model contract); the binary
// response carries no per-tensor header beyond the leading JSON block, so
// the client slices the payload by the expected shapes and verifies the
// total size instead of parsing JSON.
struct FloatTensor {
    std::string name;
    std::string datatype = "FP32";
    std::vector<int64_t> shape;
    std::vector<float> values;
};

// Minimal Triton Inference Server client over the KServe v2 HTTP protocol
// (binary tensor data extension), built with POSIX sockets so it can run
// inside the headset APK. One connection per request; the connection is
// closed by the server after each response.
class TritonHttpClient {
public:
    TritonHttpClient();
    ~TritonHttpClient();
    TritonHttpClient(TritonHttpClient&&) noexcept;
    TritonHttpClient& operator=(TritonHttpClient&&) noexcept;
    TritonHttpClient(const TritonHttpClient&) = delete;
    TritonHttpClient& operator=(const TritonHttpClient&) = delete;

    // baseUrl is "host:port" (an optional "http://" prefix is accepted),
    // e.g. "127.0.0.1:8000".
    bool Initialize(const std::string& baseUrl, std::string* error);

    // Server liveness and per-model readiness probes.
    bool IsServerLive(std::string* error);
    bool IsModelReady(
        const std::string& modelName,
        const std::string& modelVersion,
        std::string* error);

    // Runs one inference. outputs is prefilled with the names and shapes of
    // the binary outputs to request; their values are filled on success in
    // the same order. modelVersion may be empty for the server default.
    bool Infer(
        const std::string& modelName,
        const std::string& modelVersion,
        const FloatTensor& input,
        std::vector<FloatTensor>* outputs,
        std::string* error);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace questlab::triton
