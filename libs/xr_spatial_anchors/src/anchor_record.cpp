#include "xr_spatial_anchors/anchor_record.h"

#include <cctype>

namespace questlab {
namespace {

constexpr char kRecordHeader[] = "questlab-anchor-v1";
constexpr char kHexDigits[] = "0123456789ABCDEF";

int HexValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    const unsigned char upper =
        static_cast<unsigned char>(std::toupper(
            static_cast<unsigned char>(value)));
    if (upper >= 'A' && upper <= 'F') {
        return upper - 'A' + 10;
    }
    return -1;
}

}  // namespace

bool operator==(const AnchorUuid& left, const AnchorUuid& right) {
    return left.bytes == right.bytes;
}

std::string FormatAnchorUuid(const AnchorUuid& uuid) {
    std::string result;
    result.reserve(kAnchorUuidSize * 2);
    for (std::uint8_t byte : uuid.bytes) {
        result.push_back(kHexDigits[byte >> 4]);
        result.push_back(kHexDigits[byte & 0x0F]);
    }
    return result;
}

bool ParseAnchorUuid(std::string_view text, AnchorUuid* uuid) {
    if (uuid == nullptr || text.size() != kAnchorUuidSize * 2) {
        return false;
    }
    AnchorUuid parsed;
    for (std::size_t index = 0; index < kAnchorUuidSize; ++index) {
        const int high = HexValue(text[index * 2]);
        const int low = HexValue(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        parsed.bytes[index] =
            static_cast<std::uint8_t>((high << 4) | low);
    }
    *uuid = parsed;
    return true;
}

std::string SerializeAnchorRecord(const AnchorUuid& uuid) {
    return std::string(kRecordHeader) + "\n" +
        FormatAnchorUuid(uuid) + "\n";
}

bool ParseAnchorRecord(std::string_view text, AnchorUuid* uuid) {
    const std::size_t firstNewline = text.find('\n');
    if (firstNewline == std::string_view::npos ||
        text.substr(0, firstNewline) != kRecordHeader) {
        return false;
    }
    const std::size_t valueStart = firstNewline + 1;
    std::size_t valueEnd = text.find('\n', valueStart);
    if (valueEnd == std::string_view::npos) {
        valueEnd = text.size();
    }
    const std::string_view trailing =
        valueEnd < text.size() ? text.substr(valueEnd + 1) :
                                 std::string_view{};
    if (!trailing.empty() &&
        trailing.find_first_not_of("\r\n") != std::string_view::npos) {
        return false;
    }
    std::string_view value = text.substr(valueStart, valueEnd - valueStart);
    if (!value.empty() && value.back() == '\r') {
        value.remove_suffix(1);
    }
    return ParseAnchorUuid(value, uuid);
}

}  // namespace questlab
