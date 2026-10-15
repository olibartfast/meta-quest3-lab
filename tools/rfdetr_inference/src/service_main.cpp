#include "camera_source/camera_source.h"
#include "monitor_corners/monitor_corners.h"
#include "rfdetr_inference/model_contract.h"
#include "rfdetr_inference/onnx_session.h"
#include "rfdetr_inference/postprocessing.h"
#include "rfdetr_inference/preprocessing.h"
#include "rfdetr_inference/triton_session.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace {

constexpr size_t kIdentityBytes = 64;
constexpr uint32_t kMaximumDimension = 8192;

struct Options {
    std::filesystem::path manifestPath;
    std::filesystem::path modelPath;
    uint16_t port = 48110;
    std::string tritonUrl;
    std::string tritonModel = "rfdetr_nano";
    std::string tritonVersion = "1";
    // Object size for SolveQuadPose on seg-mask quads (QLD2 corners).
    // Class-agnostic: every masked detection gets a quad; the pose is
    // solved with this size for all of them.
    float objectWidthMeters = 0.55F;
    float objectHeightMeters = 0.32F;
};

int64_t SteadyNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool ParseOptions(int argc, char** argv, Options* options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if ((argument == "--manifest" || argument == "--model" ||
             argument == "--port" || argument == "--triton-url" ||
             argument == "--triton-model" ||
             argument == "--triton-version" ||
             argument == "--monitor-size" ||
             argument == "--object-size") &&
            index + 1 >= argc) {
            return false;
        }
        if (argument == "--manifest") {
            options->manifestPath = argv[++index];
        } else if (argument == "--model") {
            options->modelPath = argv[++index];
        } else if (argument == "--triton-url") {
            options->tritonUrl = argv[++index];
        } else if (argument == "--triton-model") {
            options->tritonModel = argv[++index];
        } else if (argument == "--triton-version") {
            options->tritonVersion = argv[++index];
        } else if (argument == "--port") {
            try {
                const unsigned long parsed = std::stoul(argv[++index]);
                if (parsed == 0 || parsed > 65535) {
                    return false;
                }
                options->port = static_cast<uint16_t>(parsed);
            } catch (const std::exception&) {
                return false;
            }
        } else if (
            argument == "--monitor-size" || argument == "--object-size") {
            try {
                const std::string value = argv[++index];
                const size_t comma = value.find(',');
                if (comma == std::string::npos) {
                    return false;
                }
                options->objectWidthMeters =
                    std::stof(value.substr(0, comma));
                options->objectHeightMeters =
                    std::stof(value.substr(comma + 1U));
                if (options->objectWidthMeters <= 0.0F ||
                    options->objectHeightMeters <= 0.0F) {
                    return false;
                }
            } catch (const std::exception&) {
                return false;
            }
        } else {
            return false;
        }
    }
    const bool useTriton = !options->tritonUrl.empty();
    return !options->manifestPath.empty() &&
           (useTriton || !options->modelPath.empty());
}

uint64_t HostToNetwork64(uint64_t value) {
    const uint32_t high = htonl(static_cast<uint32_t>(value >> 32U));
    const uint32_t low = htonl(static_cast<uint32_t>(value));
    return (static_cast<uint64_t>(low) << 32U) | high;
}

uint64_t NetworkToHost64(uint64_t value) {
    const uint32_t high = ntohl(static_cast<uint32_t>(value));
    const uint32_t low = ntohl(static_cast<uint32_t>(value >> 32U));
    return (static_cast<uint64_t>(high) << 32U) | low;
}

bool SendAll(int socket, const void* bytes, size_t size) {
    const auto* cursor = static_cast<const uint8_t*>(bytes);
    while (size > 0U) {
        const ssize_t sent = send(socket, cursor, size, 0);
        if (sent <= 0) return false;
        cursor += static_cast<size_t>(sent);
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool ReceiveAll(int socket, void* bytes, size_t size) {
    auto* cursor = static_cast<uint8_t*>(bytes);
    while (size > 0U) {
        const ssize_t received = recv(socket, cursor, size, 0);
        if (received <= 0) return false;
        cursor += static_cast<size_t>(received);
        size -= static_cast<size_t>(received);
    }
    return true;
}

bool SendU32(int socket, uint32_t value) {
    value = htonl(value);
    return SendAll(socket, &value, sizeof(value));
}

bool ReceiveU32(int socket, uint32_t* value) {
    uint32_t wire = 0;
    if (!ReceiveAll(socket, &wire, sizeof(wire))) return false;
    *value = ntohl(wire);
    return true;
}

bool SendU64(int socket, uint64_t value) {
    value = HostToNetwork64(value);
    return SendAll(socket, &value, sizeof(value));
}

bool ReceiveU64(int socket, uint64_t* value) {
    uint64_t wire = 0;
    if (!ReceiveAll(socket, &wire, sizeof(wire))) return false;
    *value = NetworkToHost64(wire);
    return true;
}

bool SendFloat(int socket, float value) {
    static_assert(sizeof(float) == sizeof(uint32_t));
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return SendU32(socket, bits);
}

bool ReceiveFloat(int socket, float* value) {
    uint32_t wire = 0;
    if (!ReceiveU32(socket, &wire)) {
        return false;
    }
    static_assert(sizeof(float) == sizeof(uint32_t));
    std::memcpy(value, &wire, sizeof(*value));
    return true;
}

// Mask-capable inference dispatch: ONNX sessions serve seg contracts via
// RunWithMasks; Triton seg outputs are not wired yet and fail closed.
bool RunServiceMasks(
    questlab::rfdetr::OnnxSession* session,
    const std::vector<float>& input,
    std::vector<float>* boxes,
    std::vector<float>* logits,
    std::vector<float>* masks,
    std::vector<int64_t>* maskShape,
    std::string* error) {
    return session->RunWithMasks(input, boxes, logits, masks, maskShape,
                                 error);
}

bool RunServiceMasks(
    questlab::rfdetr::TritonSession*,
    const std::vector<float>&,
    std::vector<float>*,
    std::vector<float>*,
    std::vector<float>*,
    std::vector<int64_t>*,
    std::string* error) {
    if (error != nullptr) {
        *error = "Seg contracts need an ONNX session; Triton seg outputs "
                 "are not wired";
    }
    return false;
}

struct ServedCorners {
    bool hasQuad = false;
    std::array<float, 8> quadXy{};
    bool hasPose = false;
    std::array<float, 4> quatXyzw{};
    std::array<float, 3> position{};
    float reprojectionPixels = -1.0F;
};

template <typename Session>
bool ServeClient(
    int client,
    const questlab::rfdetr::ModelContract& contract,
    Session* session,
    const Options& options) {
    std::array<char, 4> magic{};
    std::array<char, kIdentityBytes> clientIdentity{};
    if (!ReceiveAll(client, magic.data(), magic.size()) ||
        (magic != std::array<char, 4>{'Q', 'L', 'D', '1'} &&
         magic != std::array<char, 4>{'Q', 'L', 'D', '2'}) ||
        !ReceiveAll(
            client, clientIdentity.data(), clientIdentity.size())) {
        return false;
    }
    // QLD2 (screen-corner quads + poses) is served from seg masks; a box
    // contract has no mask source, so it fails closed here.
    const bool corners = magic[3] == '2';
    if (corners && !contract.hasMasks) {
        return false;
    }
    std::array<char, kIdentityBytes> serverIdentity{};
    std::copy(
        contract.onnxSha256.begin(),
        contract.onnxSha256.end(),
        serverIdentity.begin());
    const bool identityMatches = clientIdentity == serverIdentity;
    const char replyMagic[4] = {
        identityMatches ? 'Q' : 'E',
        identityMatches ? 'L' : 'R',
        identityMatches ? 'D' : 'R',
        magic[3],
    };
    if (!SendAll(client, replyMagic, sizeof(replyMagic)) ||
        !SendAll(client, serverIdentity.data(), serverIdentity.size()) ||
        !identityMatches) {
        return false;
    }

    while (true) {
        std::array<char, 4> frameMagic{};
        uint64_t frameId = 0;
        uint64_t captureTimestamp = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t byteCount = 0;
        float fx = 0.0F;
        float fy = 0.0F;
        float cx = 0.0F;
        float cy = 0.0F;
        if (!ReceiveAll(client, frameMagic.data(), frameMagic.size())) {
            return true;
        }
        if (frameMagic != std::array<char, 4>{'F', 'R', 'M', '1'} ||
            !ReceiveU64(client, &frameId) ||
            !ReceiveU64(client, &captureTimestamp) ||
            !ReceiveU32(client, &width) ||
            !ReceiveU32(client, &height) ||
            !ReceiveU32(client, &byteCount) || width == 0 || height == 0 ||
            width > kMaximumDimension || height > kMaximumDimension ||
            static_cast<uint64_t>(width) * height * 4U != byteCount ||
            (corners && (!ReceiveFloat(client, &fx) ||
                         !ReceiveFloat(client, &fy) ||
                         !ReceiveFloat(client, &cx) ||
                         !ReceiveFloat(client, &cy)))) {
            return false;
        }
        questlab::camera::RgbCapture capture;
        capture.frameId = frameId;
        capture.sensorTimestampNanoseconds =
            static_cast<int64_t>(captureTimestamp);
        capture.width = static_cast<int32_t>(width);
        capture.height = static_cast<int32_t>(height);
        capture.format = questlab::camera::PixelFormat::Rgba8888;
        capture.planes[0].bytes.resize(byteCount);
        capture.planes[0].rowStride = static_cast<int32_t>(width * 4U);
        capture.planes[0].pixelStride = 4;
        if (!ReceiveAll(
                client,
                capture.planes[0].bytes.data(),
                capture.planes[0].bytes.size())) {
            return false;
        }

        const int64_t inferenceStart = SteadyNanoseconds();
        std::vector<uint8_t> rgba;
        std::vector<float> input;
        std::vector<float> boxes;
        std::vector<float> logits;
        std::vector<float> maskLogits;
        std::vector<int64_t> maskShape;
        std::vector<questlab::rfdetr::Detection> detections;
        std::vector<ServedCorners> detectionsCorners;
        std::string error;
        bool inferOk = questlab::rfdetr::PreprocessCapture(
            capture, contract, &rgba, &input, &error);
        if (inferOk) {
            if (contract.hasMasks) {
                inferOk = RunServiceMasks(
                              session, input, &boxes, &logits, &maskLogits,
                              &maskShape, &error) &&
                    questlab::rfdetr::PostprocessDetectionsWithMasks(
                              boxes,
                              logits,
                              maskLogits,
                              capture.width,
                              capture.height,
                              0.0F,
                              contract,
                              &detections,
                              &error);
            } else {
                inferOk = session->Run(input, &boxes, &logits, &error) &&
                    questlab::rfdetr::PostprocessDetections(
                              boxes,
                              logits,
                              capture.width,
                              capture.height,
                              contract,
                              &detections,
                              &error);
            }
        }
        if (!inferOk) {
            std::cerr << "Frame " << frameId << " failed: " << error << '\n';
            return false;
        }
        if (corners) {
            // Class-agnostic: every masked detection gets a quad attempt.
            // A mask that is not quadrilateral yields no corners rather
            // than a guess; same for a failed pose solve.
            detectionsCorners.resize(detections.size());
            const questlab::corners::Intrinsics intrinsics{
                fx, fy, cx, cy};
            for (size_t index = 0; index < detections.size(); ++index) {
                const auto& detection = detections[index];
                auto& served = detectionsCorners[index];
                if (!detection.hasMask) {
                    continue;
                }
                questlab::corners::Quad quad{};
                if (!questlab::corners::FindScreenQuadFromMask(
                        detection.mask.data(), detection.maskWidth,
                        detection.maskHeight, &quad, &error)) {
                    continue;
                }
                served.hasQuad = true;
                for (size_t corner = 0; corner < 4; ++corner) {
                    served.quadXy[corner * 2U] = quad.corners[corner].x;
                    served.quadXy[corner * 2U + 1U] =
                        quad.corners[corner].y;
                }
                questlab::corners::Pose pose{};
                if (!questlab::corners::SolveQuadPose(
                        quad, intrinsics, options.objectWidthMeters,
                        options.objectHeightMeters, &pose, &served.reprojectionPixels,
                        &error)) {
                    continue;
                }
                served.hasPose = true;
                for (size_t component = 0; component < 4; ++component) {
                    served.quatXyzw[component] = pose.quat[component];
                }
                for (size_t component = 0; component < 3; ++component) {
                    served.position[component] = pose.position[component];
                }
            }
        }
        const int64_t inferenceEnd = SteadyNanoseconds();
        const char detectionMagic[4] = {'D', 'E', 'T', '1'};
        if (!SendAll(client, detectionMagic, sizeof(detectionMagic)) ||
            !SendU64(client, frameId) ||
            !SendU64(client, static_cast<uint64_t>(inferenceStart)) ||
            !SendU64(client, static_cast<uint64_t>(inferenceEnd)) ||
            !SendU32(client, static_cast<uint32_t>(detections.size()))) {
            return false;
        }
        for (size_t index = 0; index < detections.size(); ++index) {
            const auto& detection = detections[index];
            if (!SendU32(client, static_cast<uint32_t>(detection.classId)) ||
                !SendFloat(client, detection.confidence)) {
                return false;
            }
            for (float coordinate : detection.boxXyxy) {
                if (!SendFloat(client, coordinate)) return false;
            }
            if (!SendU32(
                    client,
                    static_cast<uint32_t>(detection.className.size())) ||
                !SendAll(
                    client,
                    detection.className.data(),
                    detection.className.size())) {
                return false;
            }
            if (corners) {
                // QLD2 corner extension, mirroring the client's reader:
                // presence flag, quad, pose flag, pose.
                const ServedCorners& served = detectionsCorners[index];
                if (!SendU32(client, served.hasQuad ? 1U : 0U)) {
                    return false;
                }
                if (served.hasQuad) {
                    for (float coordinate : served.quadXy) {
                        if (!SendFloat(client, coordinate)) return false;
                    }
                }
                if (!SendU32(client, served.hasPose ? 1U : 0U)) {
                    return false;
                }
                if (served.hasPose) {
                    for (float component : served.quatXyzw) {
                        if (!SendFloat(client, component)) return false;
                    }
                    for (float component : served.position) {
                        if (!SendFloat(client, component)) return false;
                    }
                    if (!SendFloat(client, served.reprojectionPixels)) {
                        return false;
                    }
                }
            }
        }
        std::cout << "frame=" << frameId
                  << " detections=" << detections.size()
                  << " inference_ms="
                  << static_cast<double>(inferenceEnd - inferenceStart) / 1.0e6
                  << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseOptions(argc, argv, &options)) {
        std::cerr << "Usage: rfdetr_service --manifest PATH "
                     "[--model PATH | --triton-url URL] [--triton-model NAME] "
                     "[--triton-version VER] [--port 48110] "
                     "[--object-size W,H]\n";
        return 2;
    }
    questlab::rfdetr::ModelContract contract;
    std::string error;
    if (!questlab::rfdetr::LoadModelContract(
            options.manifestPath, &contract, &error)) {
        std::cerr << "Manifest rejected: " << error << '\n';
        return 1;
    }
    const bool useTriton = !options.tritonUrl.empty();
    questlab::rfdetr::OnnxSession onnxSession;
    questlab::rfdetr::TritonSession tritonSession;
    if (useTriton) {
        if (!tritonSession.Initialize(
                options.tritonUrl,
                options.tritonModel,
                options.tritonVersion,
                contract,
                &error)) {
            std::cerr << "Triton initialization failed: " << error << '\n';
            return 1;
        }
    } else {
        if (!onnxSession.Initialize(options.modelPath, contract, &error)) {
            std::cerr << "ONNX Runtime initialization failed: " << error
                      << '\n';
            return 1;
        }
    }
    const size_t inputCount = std::accumulate(
        contract.input.shape.begin(),
        contract.input.shape.end(),
        size_t{1},
        [](size_t product, int64_t dimension) {
            return product * static_cast<size_t>(dimension);
        });
    std::vector<float> warmupInput(inputCount, 0.0F);
    std::vector<float> warmupBoxes;
    std::vector<float> warmupLogits;
    const bool warmupOk = useTriton
        ? tritonSession.Run(warmupInput, &warmupBoxes, &warmupLogits, &error)
        : onnxSession.Run(warmupInput, &warmupBoxes, &warmupLogits, &error);
    if (!warmupOk) {
        std::cerr << "Warm-up failed: " << error << '\n';
        return 1;
    }

    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::cerr << "Cannot create service socket\n";
        return 1;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(options.port);
    if (bind(
            server,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0 ||
        listen(server, 1) != 0) {
        std::cerr << "Cannot bind service to 127.0.0.1:"
                  << options.port << '\n';
        close(server);
        return 1;
    }
    std::cout << "RF-DETR service ready on 127.0.0.1:" << options.port
              << " manifest=" << contract.onnxSha256
              << (useTriton ? " backend=triton" : " backend=onnxruntime")
              << '\n';
    while (true) {
        const int client = accept(server, nullptr, nullptr);
        if (client < 0) {
            break;
        }
        std::cout << "client connected\n";
        const bool served = useTriton
            ? ServeClient(client, contract, &tritonSession, options)
            : ServeClient(client, contract, &onnxSession, options);
        if (!served) {
            std::cerr << "client session rejected or failed\n";
        }
        close(client);
    }
    close(server);
    return 0;
}
