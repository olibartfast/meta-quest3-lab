#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace questlab {

inline constexpr std::size_t kAnchorUuidSize = 16;

struct AnchorUuid {
    std::array<std::uint8_t, kAnchorUuidSize> bytes{};
};

bool operator==(const AnchorUuid& left, const AnchorUuid& right);
std::string FormatAnchorUuid(const AnchorUuid& uuid);
bool ParseAnchorUuid(std::string_view text, AnchorUuid* uuid);
std::string SerializeAnchorRecord(const AnchorUuid& uuid);
bool ParseAnchorRecord(std::string_view text, AnchorUuid* uuid);

}  // namespace questlab
