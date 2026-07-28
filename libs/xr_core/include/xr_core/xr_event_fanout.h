#pragma once

#include "xr_core/passthrough_interface.h"

#include <vector>

namespace questlab {

class XrEventFanout final : public XrEventObserver {
public:
    void AddObserver(XrEventObserver* observer);
    bool HandleEvent(const XrEventDataBuffer& event) override;
    void Clear();

private:
    std::vector<XrEventObserver*> observers_;
};

}  // namespace questlab
