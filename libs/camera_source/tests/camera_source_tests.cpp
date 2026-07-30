#include "camera_source/latest_frame_queue.h"
#include "camera_source/yuv_converter.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

void TestLatestFrameWins() {
    questlab::camera::LatestFrameQueue queue;
    questlab::camera::RgbCapture first;
    first.frameId = 1;
    questlab::camera::RgbCapture second;
    second.frameId = 2;
    assert(!queue.Publish(std::move(first)));
    assert(queue.Publish(std::move(second)));
    questlab::camera::RgbCapture result;
    assert(queue.TryConsumeLatest(&result));
    assert(result.frameId == 2);
    assert(!queue.TryConsumeLatest(&result));
}

void TestStrideAwareYuvConversion() {
    questlab::camera::RgbCapture capture;
    capture.width = 2;
    capture.height = 2;
    capture.format = questlab::camera::PixelFormat::Yuv420888;
    capture.planes[0].bytes = {235, 235, 0, 0, 235, 235};
    capture.planes[0].rowStride = 4;
    capture.planes[0].pixelStride = 1;
    capture.planes[1].bytes = {128, 0};
    capture.planes[1].rowStride = 2;
    capture.planes[1].pixelStride = 1;
    capture.planes[2].bytes = {128, 0};
    capture.planes[2].rowStride = 2;
    capture.planes[2].pixelStride = 1;
    std::vector<uint8_t> rgba;
    assert(questlab::camera::ConvertYuv420ToRgba(capture, &rgba));
    assert(rgba.size() == 16);
    for (size_t offset = 0; offset < rgba.size(); offset += 4) {
        assert(rgba[offset] >= 250);
        assert(rgba[offset + 1] >= 250);
        assert(rgba[offset + 2] >= 250);
        assert(rgba[offset + 3] == 255);
    }
}

void TestReplayUsesPortableFactoryContract() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "questlab-camera-source-test";
    std::filesystem::create_directories(directory);
    {
        std::ofstream(directory / "y.bin", std::ios::binary)
            .write("\xeb\xeb\xeb\xeb", 4);
        std::ofstream(directory / "u.bin", std::ios::binary)
            .write("\x80", 1);
        std::ofstream(directory / "v.bin", std::ios::binary)
            .write("\x80", 1);
        std::ofstream manifest(directory / "manifest.qcam");
        manifest
            << "QUEST_CAMERA_FIXTURE_V1 2 2 2 1 1 1 1 1 "
               "y.bin u.bin v.bin\n"
            << "sensor_timestamp_ns 1234\n"
            << "intrinsics 100 101 1 1 0\n"
            << "distortion 0 0 0 0 0\n"
            << "camera_from_head 0 0 0 1 0.01 0.02 0.03\n";
    }
    const questlab::camera::CameraSourceConfig config{
        questlab::camera::CameraSourceKind::Replay,
        (directory / "manifest.qcam").string(),
    };
    std::unique_ptr<questlab::camera::IRgbCameraSource> source =
        questlab::camera::CreateCameraSource(config, {});
    assert(source != nullptr);
    assert(source->Start({2, 2, 30}));
    questlab::camera::RgbCapture capture;
    assert(source->TryConsumeLatest(&capture));
    assert(capture.width == 2);
    assert(capture.height == 2);
    assert(capture.intrinsics.valid);
    assert(capture.cameraFromHead.valid);
    std::vector<uint8_t> rgba;
    assert(questlab::camera::ConvertYuv420ToRgba(capture, &rgba));
    source->Stop();
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    TestLatestFrameWins();
    TestStrideAwareYuvConversion();
    TestReplayUsesPortableFactoryContract();
    return 0;
}
