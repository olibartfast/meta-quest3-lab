#include "xr_spatial_anchors/anchor_record.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

}  // namespace

int main() {
    questlab::AnchorUuid expected;
    for (std::size_t index = 0; index < expected.bytes.size(); ++index) {
        expected.bytes[index] = static_cast<std::uint8_t>(index * 11);
    }

    const std::string formatted = questlab::FormatAnchorUuid(expected);
    questlab::AnchorUuid parsed;
    Expect(
        questlab::ParseAnchorUuid(formatted, &parsed) && parsed == expected,
        "UUID uppercase round trip");

    std::string lowercase = formatted;
    for (char& value : lowercase) {
        if (value >= 'A' && value <= 'F') {
            value = static_cast<char>(value - 'A' + 'a');
        }
    }
    Expect(
        questlab::ParseAnchorUuid(lowercase, &parsed) && parsed == expected,
        "UUID lowercase input");
    Expect(
        !questlab::ParseAnchorUuid(formatted.substr(1), &parsed),
        "UUID wrong length rejected");
    std::string invalidHex = formatted;
    invalidHex[4] = 'Z';
    Expect(
        !questlab::ParseAnchorUuid(invalidHex, &parsed),
        "UUID non-hex input rejected");

    const std::string record = questlab::SerializeAnchorRecord(expected);
    Expect(
        questlab::ParseAnchorRecord(record, &parsed) && parsed == expected,
        "record round trip");
    Expect(
        questlab::ParseAnchorRecord(record + "\n", &parsed),
        "record trailing newlines accepted");
    Expect(
        !questlab::ParseAnchorRecord(
            "questlab-anchor-v2\n" + formatted + "\n",
            &parsed),
        "unknown record version rejected");
    Expect(
        !questlab::ParseAnchorRecord("questlab-anchor-v1", &parsed),
        "record missing UUID rejected");
    Expect(
        !questlab::ParseAnchorRecord(record + "garbage", &parsed),
        "record trailing data rejected");

    if (failures == 0) {
        std::puts("All spatial-anchor record tests passed");
    }
    return failures == 0 ? 0 : 1;
}
