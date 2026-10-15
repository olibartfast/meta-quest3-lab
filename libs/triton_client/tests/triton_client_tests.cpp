#include "triton_client/triton_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

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

// One-shot scripted HTTP server on 127.0.0.1: used to capture the client's
// request and reply with a canned response.
class MockServer {
public:
    ~MockServer() { Stop(); }

    bool Start(std::string* error) {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) {
            *error = "mock socket failed";
            return false;
        }
        int reuse = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(
                listener_, reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) != 0 ||
            listen(listener_, 1) != 0) {
            *error = "mock bind/listen failed";
            return false;
        }
        socklen_t length = sizeof(address);
        getsockname(
            listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this]() { Serve(); });
        return true;
    }

    void Stop() {
        if (listener_ >= 0) {
            shutdown(listener_, SHUT_RDWR);
            close(listener_);
            listener_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    uint16_t port() const { return port_; }

    void SetRawResponse(const std::string& response) {
        std::lock_guard<std::mutex> lock(mutex_);
        response_ = response;
    }

    std::string Request() {
        if (thread_.joinable()) {
            thread_.join();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return request_;
    }

private:
    void Serve() {
        const int client = accept(listener_, nullptr, nullptr);
        if (client < 0) {
            return;
        }
        std::string request;
        std::string header;
        char character = 0;
        while (header.size() < 64U * 1024U) {
            if (!ReceiveAll(client, &character, 1)) {
                break;
            }
            header.push_back(character);
            if (header.size() >= 4U &&
                header.compare(header.size() - 4U, 4U, "\r\n\r\n") == 0) {
                break;
            }
        }
        request = header;
        const std::string lower = ToLower(header);
        // Line-start match: "content-length:" also appears inside
        // "inference-header-content-length:".
        const std::string needle = "\r\ncontent-length:";
        const size_t position = lower.find(needle);
        if (position != std::string::npos) {
            const size_t valueStart = position + needle.size();
            const size_t end = lower.find("\r\n", valueStart);
            const size_t value = std::stoul(lower.substr(
                valueStart, end - valueStart));
            std::vector<uint8_t> body(value);
            if (value > 0U && ReceiveAll(client, body.data(), value)) {
                request.append(
                    reinterpret_cast<const char*>(body.data()), value);
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request_ = request;
        }
        std::string response;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response = response_;
        }
        if (!response.empty()) {
            SendAll(client, response.data(), response.size());
        }
        close(client);
    }

    static std::string ToLower(std::string value) {
        for (char& character : value) {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        return value;
    }

    int listener_ = -1;
    uint16_t port_ = 0;
    std::thread thread_;
    std::mutex mutex_;
    std::string request_;
    std::string response_;
};

std::vector<uint8_t> KServeBinaryBody(
    const std::string& json, const std::vector<std::vector<float>>& blocks) {
    std::vector<uint8_t> body(json.begin(), json.end());
    for (const std::vector<float>& block : blocks) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(block.data());
        body.insert(body.end(), bytes, bytes + block.size() * sizeof(float));
    }
    return body;
}

std::string ContentLengthResponse(
    int status,
    const std::string& reason,
    size_t inferenceHeaderLength,
    const std::vector<uint8_t>& body) {
    std::string response = "HTTP/1.1 " + std::to_string(status) + " " +
                           reason + "\r\n" +
                           "Content-Length: " + std::to_string(body.size()) +
                           "\r\n";
    if (inferenceHeaderLength > 0U) {
        response += "Inference-Header-Content-Length: " +
                    std::to_string(inferenceHeaderLength) + "\r\n";
    }
    response += "Content-Type: application/octet-stream\r\n\r\n";
    response.append(
        reinterpret_cast<const char*>(body.data()), body.size());
    return response;
}

std::string ChunkedResponse(
    int status, size_t inferenceHeaderLength,
    const std::vector<uint8_t>& body) {
    std::string response = "HTTP/1.1 " + std::to_string(status) + " OK\r\n" +
                           "Inference-Header-Content-Length: " +
                           std::to_string(inferenceHeaderLength) + "\r\n" +
                           "Transfer-Encoding: chunked\r\n" +
                           "Content-Type: application/octet-stream\r\n\r\n";
    const size_t chunk = 97U;
    for (size_t offset = 0; offset < body.size(); offset += chunk) {
        const size_t size = std::min(chunk, body.size() - offset);
        char header[16];
        std::snprintf(header, sizeof(header), "%zx\r\n", size);
        response += header;
        response.append(
            reinterpret_cast<const char*>(body.data() + offset), size);
        response += "\r\n";
    }
    response += "0\r\n\r\n";
    return response;
}

void TestHealthProbe() {
    MockServer server;
    std::string error;
    assert(server.Start(&error));
    server.SetRawResponse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

    questlab::triton::TritonHttpClient client;
    assert(client.Initialize(
        "127.0.0.1:" + std::to_string(server.port()), &error));
    assert(client.IsServerLive(&error));
    const std::string request = server.Request();
    assert(request.rfind("GET /v2/health/live HTTP/1.1", 0) == 0);
    assert(request.find("Connection: close") != std::string::npos);
}

void TestModelReadyProbe() {
    MockServer server;
    std::string error;
    assert(server.Start(&error));
    server.SetRawResponse("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

    questlab::triton::TritonHttpClient client;
    assert(client.Initialize(
        "127.0.0.1:" + std::to_string(server.port()), &error));
    assert(client.IsModelReady("rfdetr_seg_small", "1", &error));
    const std::string request = server.Request();
    assert(
        request.rfind(
            "GET /v2/models/rfdetr_seg_small/ready?version=1 HTTP/1.1",
            0) == 0);
}

void TestBinaryInference() {
    MockServer server;
    std::string error;
    assert(server.Start(&error));
    const std::string json = R"({"outputs":[{"name":"dets"},{"name":"labels"}]})";
    server.SetRawResponse(ContentLengthResponse(
        200, "OK", json.size(),
        KServeBinaryBody(json, {{1.5F, -2.5F}, {3.0F, 4.0F}})));

    questlab::triton::TritonHttpClient client;
    assert(client.Initialize(
        "127.0.0.1:" + std::to_string(server.port()), &error));

    questlab::triton::FloatTensor input;
    input.name = "input";
    input.shape = {1, 1, 2};
    input.values = {0.25F, 0.5F};
    std::vector<questlab::triton::FloatTensor> outputs(2);
    outputs[0].name = "dets";
    outputs[0].shape = {1, 2};
    outputs[1].name = "labels";
    outputs[1].shape = {2};

    assert(client.Infer("test_model", "1", input, &outputs, &error));
    assert(outputs[0].values.size() == 2U && outputs[0].values[0] == 1.5F &&
           outputs[0].values[1] == -2.5F);
    assert(outputs[1].values.size() == 2U && outputs[1].values[0] == 3.0F &&
           outputs[1].values[1] == 4.0F);

    const std::string request = server.Request();
    assert(
        request.rfind(
            "POST /v2/models/test_model/infer?version=1 HTTP/1.1", 0) == 0);
    assert(
        request.find("Content-Type: application/octet-stream") !=
        std::string::npos);
    assert(
        request.find("Inference-Header-Content-Length: ") !=
        std::string::npos);
    assert(request.find("\"name\":\"input\"") != std::string::npos);
    assert(request.find("\"shape\":[1,1,2]") != std::string::npos);
    assert(request.find("\"binary_data_size\":8") != std::string::npos);
    assert(request.find("\"name\":\"dets\"") != std::string::npos);
    assert(request.find("\"name\":\"labels\"") != std::string::npos);
    assert(request.find("\"binary_data\":true") != std::string::npos);
}

void TestChunkedResponse() {
    MockServer server;
    std::string error;
    assert(server.Start(&error));
    const std::string json = R"({"outputs":[{"name":"dets"}]})";
    server.SetRawResponse(ChunkedResponse(
        200, json.size(),
        KServeBinaryBody(json, {{7.0F, 8.0F, 9.0F}})));

    questlab::triton::TritonHttpClient client;
    assert(client.Initialize(
        "127.0.0.1:" + std::to_string(server.port()), &error));

    questlab::triton::FloatTensor input;
    input.name = "input";
    input.shape = {1, 3};
    input.values = {1.0F, 2.0F, 3.0F};
    std::vector<questlab::triton::FloatTensor> outputs(1);
    outputs[0].name = "dets";
    outputs[0].shape = {3};

    assert(client.Infer("test_model", "", input, &outputs, &error));
    assert(outputs[0].values == std::vector<float>({7.0F, 8.0F, 9.0F}));
}

void TestHttpError() {
    MockServer server;
    std::string error;
    assert(server.Start(&error));
    const std::string message = R"({"error":"model not ready"})";
    server.SetRawResponse(ContentLengthResponse(
        400, "Bad Request", 0U,
        std::vector<uint8_t>(message.begin(), message.end())));

    questlab::triton::TritonHttpClient client;
    assert(client.Initialize(
        "127.0.0.1:" + std::to_string(server.port()), &error));

    questlab::triton::FloatTensor input;
    input.name = "input";
    input.shape = {1};
    input.values = {0.0F};
    std::vector<questlab::triton::FloatTensor> outputs(1);
    outputs[0].name = "dets";
    outputs[0].shape = {1};

    assert(!client.Infer("test_model", "", input, &outputs, &error));
    assert(error.find("HTTP 400") != std::string::npos);
    assert(error.find("model not ready") != std::string::npos);
}

void TestPayloadSizeMismatch() {
    MockServer server;
    std::string error;
    assert(server.Start(&error));
    const std::string json = R"({"outputs":[{"name":"dets"}]})";
    std::vector<uint8_t> body = KServeBinaryBody(json, {{1.0F}});
    body.push_back(0U);  // one stray byte
    server.SetRawResponse(
        ContentLengthResponse(200, "OK", json.size(), body));

    questlab::triton::TritonHttpClient client;
    assert(client.Initialize(
        "127.0.0.1:" + std::to_string(server.port()), &error));

    questlab::triton::FloatTensor input;
    input.name = "input";
    input.shape = {1};
    input.values = {0.0F};
    std::vector<questlab::triton::FloatTensor> outputs(1);
    outputs[0].name = "dets";
    outputs[0].shape = {1};

    assert(!client.Infer("test_model", "", input, &outputs, &error));
    assert(error.find("size") != std::string::npos);
}

void TestInvalidConfiguration() {
    questlab::triton::TritonHttpClient client;
    std::string error;
    assert(!client.Initialize("no-port", &error));
    assert(client.Initialize("127.0.0.1:1", &error));
    assert(!client.IsServerLive(&error));
    assert(!error.empty());
}

}  // namespace

int main() {
    TestHealthProbe();
    TestModelReadyProbe();
    TestBinaryInference();
    TestChunkedResponse();
    TestHttpError();
    TestPayloadSizeMismatch();
    TestInvalidConfiguration();
    return 0;
}
