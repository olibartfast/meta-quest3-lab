#include "xr_core/xr_event_fanout.h"

#include <algorithm>

namespace questlab {

void XrEventFanout::AddObserver(XrEventObserver* observer) {
    if (observer == nullptr ||
        std::find(observers_.begin(), observers_.end(), observer) !=
            observers_.end()) {
        return;
    }
    observers_.push_back(observer);
}

bool XrEventFanout::HandleEvent(const XrEventDataBuffer& event) {
    for (XrEventObserver* observer : observers_) {
        if (!observer->HandleEvent(event)) {
            return false;
        }
    }
    return true;
}

void XrEventFanout::Clear() {
    observers_.clear();
}

}  // namespace questlab
