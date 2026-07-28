#include "xr_core/xr_event_fanout.h"

#include <cstdio>
#include <vector>

namespace {

class Observer final : public questlab::XrEventObserver {
public:
    Observer(int id, std::vector<int>* calls, bool succeeds = true)
        : id_(id), calls_(calls), succeeds_(succeeds) {}

    bool HandleEvent(const XrEventDataBuffer&) override {
        calls_->push_back(id_);
        return succeeds_;
    }

private:
    int id_;
    std::vector<int>* calls_;
    bool succeeds_;
};

int failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

}  // namespace

int main() {
    XrEventDataBuffer event{};
    std::vector<int> calls;
    Observer first(1, &calls);
    Observer second(2, &calls);
    Observer failure(3, &calls, false);
    Observer skipped(4, &calls);
    questlab::XrEventFanout fanout;

    fanout.AddObserver(nullptr);
    fanout.AddObserver(&first);
    fanout.AddObserver(&first);
    fanout.AddObserver(&second);
    Expect(fanout.HandleEvent(event), "successful fan-out");
    Expect(
        calls == std::vector<int>({1, 2}),
        "null ignored, duplicates suppressed, order preserved");

    calls.clear();
    fanout.Clear();
    fanout.AddObserver(&first);
    fanout.AddObserver(&failure);
    fanout.AddObserver(&skipped);
    Expect(!fanout.HandleEvent(event), "failure propagated");
    Expect(
        calls == std::vector<int>({1, 3}),
        "dispatch stops after observer failure");

    calls.clear();
    fanout.Clear();
    Expect(fanout.HandleEvent(event), "empty fan-out succeeds");
    Expect(calls.empty(), "clear removes observers");

    if (failures == 0) {
        std::puts("All XR event fan-out tests passed");
    }
    return failures == 0 ? 0 : 1;
}
