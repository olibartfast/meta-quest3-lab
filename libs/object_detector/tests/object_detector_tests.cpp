#include "object_detector/object_detector.h"

#include <cassert>
#include <chrono>
#include <string>
#include <thread>

namespace {

questlab::detection::DetectorStats WaitForSettled(
    questlab::detection::IObjectDetector* detector) {
    using questlab::detection::DetectorHealth;
    questlab::detection::DetectorStats stats = detector->GetStats();
    for (int attempt = 0;
         attempt < 200 && stats.health == DetectorHealth::Starting;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        stats = detector->GetStats();
    }
    return stats;
}

}  // namespace

int main() {
    using namespace questlab::detection;

    // One backend: Triton over KServe v2. The factory always builds it.
    DetectorConfig config;
    std::unique_ptr<IObjectDetector> detector = CreateObjectDetector(config);
    assert(detector != nullptr);
    assert(!detector->GetCapabilities().backendName.empty());

    // No manifest and no model: Prepare fails fast, health Disconnected.
    assert(detector->Start(config));
    DetectorStats stats = WaitForSettled(detector.get());
    assert(stats.health == DetectorHealth::Disconnected);
    assert(!stats.lastError.empty());
    detector->Stop();
    assert(detector->GetStats().health == DetectorHealth::Stopped);

    // A real manifest with a wrong pinned identity is rejected before any
    // network traffic.
    config.manifestPath = QUESTLAB_SEG_MANIFEST_PATH;
    config.tritonModel = "rfdetr_seg_small";
    config.expectedManifestIdentity = std::string(64, '0');
    assert(detector->Start(config));
    stats = WaitForSettled(detector.get());
    assert(stats.health == DetectorHealth::Disconnected);
    assert(stats.lastError.find("identity") != std::string::npos);
    detector->Stop();

    // Accepted contract but no model name: fail closed, never guess.
    config.expectedManifestIdentity.clear();
    config.tritonModel.clear();
    assert(detector->Start(config));
    stats = WaitForSettled(detector.get());
    assert(stats.health == DetectorHealth::Disconnected);
    assert(stats.lastError.find("model name") != std::string::npos);
    detector->Stop();

    return 0;
}
