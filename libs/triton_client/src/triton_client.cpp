#include "triton_client/triton_client.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <utility>

namespace questlab::triton {
namespace {

constexpr int kTimeoutMilliseconds = 20000;
constexpr size_t kMaximumHeaderBytes = 64U * 1024U;
constexpr size_t kMaximumBodyBytes = 512U * 1024U * 1024U;

bool SendAll(int socket, const void* bytes, size_t size) {
    const auto* cursor = static_cast<const uint8_t*>(bytes);
    while (size > 0U) {
        const ssize_t sent = send(socket, cursor, size, 0);
        if (sent <= 0) {
            return false;
        }
        cursor += static_cast<size_t>(sent);
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool ReceiveAll(int socket, void* bytes, size_t size) {
    auto* cursor = static_cast<uint8_t*>(bytes);
    while (size > 0U) {
        const ssize_t received = recv(socket, cursor, size, 0);
        if (received <= 0) {
            return false;
        }
        cursor += static_cast<size_t>(received);
        size -= static_cast<size_t>(received);
    }
    return true;
}

bool ParseHostPort(
    const std::string& baseUrl,
    std::string* host,
    std::string* port,
    std::string* error) {
    std::string value = baseUrl;
    if (value.rfind("http://", 0) == 0) {
        value = value.substr(7U);
    }
    const size_t colon = value.rfind(':');
    if (value.empty() || colon == std::string::npos || colon == 0U ||
        colon + 1U >= value.size()) {
        if (error != nullptr) {
            *error = "Triton URL must be host:port: " + baseUrl;
        }
        return false;
    }
    *host = value.substr(0, colon);
    *port = value.substr(colon + 1U);
    return true;
}

int Connect(const std::string& host, const std::string& port,
            std::string* error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses) != 0) {
        if (error != nullptr) {
            *error = "Cannot resolve Triton host " + host;
        }
        return -1;
    }
    int connected = -1;
    for (addrinfo* address = addresses; address != nullptr;
         address = address->ai_next) {
        connected = socket(
            address->ai_family, address->ai_socktype, address->ai_protocol);
        if (connected >= 0 &&
            connect(connected, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        if (connected >= 0) {
            close(connected);
            connected = -1;
        }
    }
    freeaddrinfo(addresses);
    if (connected < 0) {
        if (error != nullptr) {
            *error = "Cannot connect to Triton at " + host + ':' + port;
        }
        return -1;
    }
    timeval timeout{};
    timeout.tv_sec = kTimeoutMilliseconds / 1000;
    timeout.tv_usec = (kTimeoutMilliseconds % 1000) * 1000;
    setsockopt(
        connected, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(
        connected, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    return connected;
}

struct HttpResponse {
    int status = 0;
    size_t inferenceHeaderLength = 0;
    std::vector<uint8_t> body;
};

std::string Lowercase(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool ReadChunkedBody(
    int socket, std::vector<uint8_t>* body, std::string* error) {
    while (true) {
        std::string sizeLine;
        char character = 0;
        while (true) {
            if (!ReceiveAll(socket, &character, 1)) {
                if (error != nullptr) {
                    *error = "Triton chunked body size read failed";
                }
                return false;
            }
            if (character == '\n') {
                break;
            }
            if (character == '\r') {
                continue;
            }
            sizeLine.push_back(character);
            if (sizeLine.size() > 32U) {
                if (error != nullptr) {
                    *error = "Triton chunk size line is invalid";
                }
                return false;
            }
        }
        size_t chunkSize = 0;
        try {
            chunkSize = static_cast<size_t>(std::stoul(sizeLine, nullptr, 16));
        } catch (const std::exception&) {
            if (error != nullptr) {
                *error = "Triton chunk size is invalid: " + sizeLine;
            }
            return false;
        }
        if (chunkSize == 0U) {
            return true;
        }
        if (body->size() + chunkSize > kMaximumBodyBytes) {
            if (error != nullptr) {
                *error = "Triton response body is too large";
            }
            return false;
        }
        const size_t offset = body->size();
        body->resize(offset + chunkSize);
        if (!ReceiveAll(socket, body->data() + offset, chunkSize)) {
            if (error != nullptr) {
                *error = "Triton chunked body read failed";
            }
            return false;
        }
        char terminator[2] = {0, 0};
        if (!ReceiveAll(socket, terminator, sizeof(terminator))) {
            if (error != nullptr) {
                *error = "Triton chunk terminator read failed";
            }
            return false;
        }
    }
}

bool ReadHttpResponse(
    int socket,
    bool readBody,
    HttpResponse* response,
    std::string* error) {
    std::string header;
    char character = 0;
    while (header.size() < kMaximumHeaderBytes) {
        if (!ReceiveAll(socket, &character, 1)) {
            if (error != nullptr) {
                *error = "Triton HTTP header read failed";
            }
            return false;
        }
        header.push_back(character);
        if (header.size() >= 4U &&
            header.compare(header.size() - 4U, 4U, "\r\n\r\n") == 0) {
            break;
        }
    }
    if (header.size() < 4U ||
        header.compare(header.size() - 4U, 4U, "\r\n\r\n") != 0) {
        if (error != nullptr) {
            *error = "Triton HTTP header was incomplete";
        }
        return false;
    }
    const size_t statusEnd = header.find("\r\n");
    if (statusEnd == std::string::npos) {
        if (error != nullptr) {
            *error = "Triton HTTP status line is missing";
        }
        return false;
    }
    const std::string statusLine = header.substr(0, statusEnd);
    const size_t firstSpace = statusLine.find(' ');
    if (firstSpace == std::string::npos ||
        firstSpace + 4U > statusLine.size()) {
        if (error != nullptr) {
            *error = "Triton HTTP status line is invalid";
        }
        return false;
    }
    try {
        response->status = std::stoi(statusLine.substr(firstSpace + 1U, 3U));
    } catch (const std::exception&) {
        if (error != nullptr) {
            *error = "Triton HTTP status code is invalid";
        }
        return false;
    }

    const std::string lowerHeader = Lowercase(header);
    const auto headerValue = [&lowerHeader](const std::string& key) {
        // Match at line starts so "content-length" does not match inside
        // "inference-header-content-length".
        const std::string needle = "\r\n" + key + ":";
        size_t position = lowerHeader.find(needle);
        if (position == std::string::npos) {
            return std::string();
        }
        position += needle.size();
        const size_t end = lowerHeader.find("\r\n", position);
        std::string value = lowerHeader.substr(
            position, end == std::string::npos
                          ? std::string::npos
                          : end - position);
        const size_t first = value.find_first_not_of(" \t");
        const size_t last = value.find_last_not_of(" \t");
        if (first == std::string::npos) {
            return std::string();
        }
        return value.substr(first, last - first + 1U);
    };

    if (!readBody) {
        return true;
    }
    const std::string inferenceHeaderLength =
        headerValue("inference-header-content-length");
    if (!inferenceHeaderLength.empty()) {
        try {
            response->inferenceHeaderLength =
                static_cast<size_t>(std::stoull(inferenceHeaderLength));
        } catch (const std::exception&) {
            if (error != nullptr) {
                *error = "Triton inference header length is invalid";
            }
            return false;
        }
    }
    const std::string transferEncoding = headerValue("transfer-encoding");
    const std::string contentLength = headerValue("content-length");
    if (transferEncoding.find("chunked") != std::string::npos) {
        return ReadChunkedBody(socket, &response->body, error);
    }
    if (!contentLength.empty()) {
        size_t length = 0;
        try {
            length = static_cast<size_t>(std::stoull(contentLength));
        } catch (const std::exception&) {
            if (error != nullptr) {
                *error = "Triton content length is invalid";
            }
            return false;
        }
        if (length > kMaximumBodyBytes) {
            if (error != nullptr) {
                *error = "Triton response body is too large";
            }
            return false;
        }
        response->body.resize(length);
        if (!ReceiveAll(socket, response->body.data(), length)) {
            if (error != nullptr) {
                *error = "Triton response body read failed";
            }
            return false;
        }
        return true;
    }
    // The client always sends "Connection: close", so an unbounded body can
    // be read to end of stream.
    uint8_t buffer[64U * 1024U];
    while (response->body.size() < kMaximumBodyBytes) {
        const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
        if (received == 0) {
            return true;
        }
        if (received < 0) {
            if (error != nullptr) {
                *error = "Triton response body read failed";
            }
            return false;
        }
        response->body.insert(
            response->body.end(), buffer, buffer + received);
    }
    if (error != nullptr) {
        *error = "Triton response body is too large";
    }
    return false;
}

bool RunRequest(
    int socket,
    const std::string& method,
    const std::string& path,
    const std::string& host,
    const std::string& port,
    size_t inferenceHeaderLength,
    const std::vector<uint8_t>& body,
    bool readBody,
    HttpResponse* response,
    std::string* error) {
    std::ostringstream request;
    request << method << ' ' << path << " HTTP/1.1\r\n"
            << "Host: " << host << ':' << port << "\r\n"
            << "Connection: close\r\n";
    if (!body.empty()) {
        request << "Content-Type: application/octet-stream\r\n"
                << "Inference-Header-Content-Length: "
                << inferenceHeaderLength << "\r\n"
                << "Content-Length: " << body.size() << "\r\n";
    }
    request << "\r\n";
    const std::string head = request.str();
    if (!SendAll(socket, head.data(), head.size()) ||
        (!body.empty() && !SendAll(socket, body.data(), body.size()))) {
        if (error != nullptr) {
            *error = "Triton request send failed";
        }
        return false;
    }
    return ReadHttpResponse(socket, readBody, response, error);
}

bool BodyLength(const std::vector<int64_t>& shape, size_t* count) {
    if (shape.empty()) {
        return false;
    }
    size_t product = 1;
    for (const int64_t dimension : shape) {
        if (dimension <= 0) {
            return false;
        }
        product *= static_cast<size_t>(dimension);
    }
    *count = product;
    return true;
}

bool InferImpl(
    const std::string& host,
    const std::string& port,
    const std::string& modelName,
    const std::string& modelVersion,
    const FloatTensor& input,
    std::vector<FloatTensor>* outputs,
    std::string* error) {
    size_t inputCount = 0;
    if (input.name.empty() || !BodyLength(input.shape, &inputCount) ||
        input.values.size() != inputCount) {
        if (error != nullptr) {
            *error = "Triton input tensor is invalid";
        }
        return false;
    }
    if (outputs == nullptr || outputs->empty()) {
        if (error != nullptr) {
            *error = "Triton outputs were not requested";
        }
        return false;
    }
    std::vector<size_t> outputCounts(outputs->size(), 0);
    for (size_t index = 0; index < outputs->size(); ++index) {
        if ((*outputs)[index].name.empty() ||
            !BodyLength((*outputs)[index].shape, &outputCounts[index])) {
            if (error != nullptr) {
                *error = "Triton output tensor is invalid";
            }
            return false;
        }
    }

    std::ostringstream json;
    json << "{\"inputs\":[{\"name\":\"" << input.name
         << "\",\"shape\":[";
    for (size_t index = 0; index < input.shape.size(); ++index) {
        json << (index == 0U ? "" : ",") << input.shape[index];
    }
    json << "],\"datatype\":\"" << input.datatype
         << "\",\"parameters\":{\"binary_data_size\":"
         << inputCount * sizeof(float) << "}}],\"outputs\":[";
    for (size_t index = 0; index < outputs->size(); ++index) {
        json << (index == 0U ? "" : ",") << "{\"name\":\""
             << (*outputs)[index].name
             << "\",\"parameters\":{\"binary_data\":true}}";
    }
    json << "]}";

    const std::string jsonBody = json.str();
    std::vector<uint8_t> body;
    body.reserve(jsonBody.size() + inputCount * sizeof(float));
    body.insert(body.end(), jsonBody.begin(), jsonBody.end());
    const auto* inputBytes =
        reinterpret_cast<const uint8_t*>(input.values.data());
    body.insert(
        body.end(), inputBytes,
        inputBytes + input.values.size() * sizeof(float));

    std::string path = "/v2/models/" + modelName + "/infer";
    if (!modelVersion.empty()) {
        path += "?version=" + modelVersion;
    }
    const int socket = Connect(host, port, error);
    if (socket < 0) {
        return false;
    }
    HttpResponse response;
    const bool sent = RunRequest(
        socket, "POST", path, host, port, jsonBody.size(), body, true,
        &response, error);
    close(socket);
    if (!sent) {
        return false;
    }
    if (response.status != 200) {
        if (error != nullptr) {
            std::string message(
                response.body.begin(),
                response.body.begin() +
                    std::min<size_t>(response.body.size(), 256U));
            *error = "Triton inference failed: HTTP " +
                     std::to_string(response.status) + ": " + message;
        }
        return false;
    }
    if (response.inferenceHeaderLength == 0U) {
        if (error != nullptr) {
            *error = "Triton response did not use the binary tensor "
                     "extension";
        }
        return false;
    }
    size_t expectedSize = response.inferenceHeaderLength;
    for (const size_t count : outputCounts) {
        expectedSize += count * sizeof(float);
    }
    if (response.body.size() != expectedSize) {
        if (error != nullptr) {
            *error = "Triton binary response size does not match the "
                     "expected outputs";
        }
        return false;
    }
    size_t payloadOffset = response.inferenceHeaderLength;
    for (size_t index = 0; index < outputs->size(); ++index) {
        (*outputs)[index].values.resize(outputCounts[index]);
        std::memcpy(
            (*outputs)[index].values.data(),
            response.body.data() + payloadOffset,
            outputCounts[index] * sizeof(float));
        payloadOffset += outputCounts[index] * sizeof(float);
    }
    return true;
}

bool ProbeImpl(
    const std::string& host,
    const std::string& port,
    const std::string& path,
    std::string* error) {
    const int socket = Connect(host, port, error);
    if (socket < 0) {
        return false;
    }
    HttpResponse response;
    const bool sent = RunRequest(
        socket, "GET", path, host, port, 0U, {}, false, &response, error);
    close(socket);
    if (!sent) {
        return false;
    }
    if (response.status != 200) {
        if (error != nullptr) {
            *error = "Triton probe failed: HTTP " +
                     std::to_string(response.status);
        }
        return false;
    }
    return true;
}

}  // namespace

class TritonHttpClient::Impl {
public:
    std::string host;
    std::string port;
    bool initialized = false;
};

TritonHttpClient::TritonHttpClient() : impl_(std::make_unique<Impl>()) {}
TritonHttpClient::~TritonHttpClient() = default;
TritonHttpClient::TritonHttpClient(TritonHttpClient&&) noexcept = default;
TritonHttpClient& TritonHttpClient::operator=(TritonHttpClient&&) noexcept =
    default;

bool TritonHttpClient::Initialize(
    const std::string& baseUrl, std::string* error) {
    std::string host;
    std::string port;
    if (!ParseHostPort(baseUrl, &host, &port, error)) {
        return false;
    }
    impl_->host = std::move(host);
    impl_->port = std::move(port);
    impl_->initialized = true;
    return true;
}

bool TritonHttpClient::IsServerLive(std::string* error) {
    if (!impl_->initialized) {
        if (error != nullptr) {
            *error = "Triton client is not initialized";
        }
        return false;
    }
    return ProbeImpl(impl_->host, impl_->port, "/v2/health/live", error);
}

bool TritonHttpClient::IsModelReady(
    const std::string& modelName,
    const std::string& modelVersion,
    std::string* error) {
    if (!impl_->initialized) {
        if (error != nullptr) {
            *error = "Triton client is not initialized";
        }
        return false;
    }
    std::string path = "/v2/models/" + modelName + "/ready";
    if (!modelVersion.empty()) {
        path += "?version=" + modelVersion;
    }
    return ProbeImpl(impl_->host, impl_->port, path, error);
}

bool TritonHttpClient::Infer(
    const std::string& modelName,
    const std::string& modelVersion,
    const FloatTensor& input,
    std::vector<FloatTensor>* outputs,
    std::string* error) {
    if (!impl_->initialized) {
        if (error != nullptr) {
            *error = "Triton client is not initialized";
        }
        return false;
    }
    if (modelName.empty()) {
        if (error != nullptr) {
            *error = "Triton model name is empty";
        }
        return false;
    }
    return InferImpl(
        impl_->host, impl_->port, modelName, modelVersion, input, outputs,
        error);
}

}  // namespace questlab::triton
