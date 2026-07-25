#pragma once

#include <openxr/openxr.h>
#include <vulkan/vulkan.h>

#include <string>

namespace questlab {

std::string XrResultName(XrInstance instance, XrResult result);
bool CheckXr(XrInstance instance, XrResult result, const char* operation);
bool CheckVk(VkResult result, const char* operation);
void LogInfo(const char* format, ...);
void LogError(const char* format, ...);

}  // namespace questlab
