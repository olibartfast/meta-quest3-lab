#pragma once

#include <openxr/openxr.h>

#include <vector>

namespace questlab {

class XrEventObserver {
public:
    virtual ~XrEventObserver() = default;
    virtual bool HandleEvent(const XrEventDataBuffer& event) = 0;
};

class XrUnderlayProvider {
public:
    virtual ~XrUnderlayProvider() = default;
    virtual bool AppendUnderlayLayers(
        XrTime displayTime,
        std::vector<const XrCompositionLayerBaseHeader*>* layers) = 0;
};

class XrPassthroughInterface :
    public XrEventObserver,
    public XrUnderlayProvider {
public:
    ~XrPassthroughInterface() override = default;

    virtual bool Initialize(
        XrInstance instance,
        XrSystemId systemId,
        XrSession session) = 0;
    virtual bool SetActive(bool active) = 0;
    virtual void Shutdown() = 0;
};

}  // namespace questlab
