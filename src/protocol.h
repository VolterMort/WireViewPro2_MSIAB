#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cfloat>

namespace wireview {
constexpr std::size_t frame_size = 100;
// Append new sources to preserve existing Afterburner sensor indices.
constexpr std::size_t connection_source = 26;
constexpr std::size_t cable_rating_source = 27;
constexpr std::size_t source_count = 28;
using Values = std::array<float, source_count>;
inline std::uint16_t u16(const std::uint8_t* b) {
    return std::uint16_t(b[0]) | (std::uint16_t(b[1]) << 8);
}
inline std::uint32_t u32(const std::uint8_t* b) {
    return std::uint32_t(u16(b)) | (std::uint32_t(u16(b + 2)) << 16);
}
inline int i16(const std::uint8_t* b) {
    const auto v = u16(b);
    return v < 32768 ? v : int(v) - 65536;
}
inline Values unavailable() {
    Values values;
    values.fill(FLT_MAX); // Afterburner SDK samples use FLT_MAX for unavailable data.
    values[connection_source] = 0;
    return values;
}
// WireView Pro II sensor response: little endian, Pack=4, exactly 100 bytes.
// Decode offsets explicitly so compiler packing cannot change the wire format.
inline bool decode(const std::uint8_t* b, std::size_t size, Values& output) {
    if (!b || size != frame_size || b[10] > 100 || b[11] || b[95] || u16(b + 8) > 6000)
        return false;
    auto v = unavailable();
    double total_power = 0;
    for (int pin = 0; pin < 6; ++pin) {
        const auto p = b + 12 + pin * 12;
        const int mv = i16(p);
        const auto ma = u32(p + 4);
        if (mv < -1000 || mv > 20000 || ma > 60000 || u16(p + 2)) return false;
        const float volts = mv / 1000.0f;
        const float amps = ma / 1000.0f;
        v[3 + pin * 3] = volts;
        v[4 + pin * 3] = amps;
        v[5 + pin * 3] = volts * amps;
        total_power += double(mv) * ma / 1000000.0;
    }
    v[0] = float(total_power);
    // Like wireview-hwmon, retain the firmware's aggregate current reading.
    v[1] = u32(b + 88) / 1000.0f;
    if (v[1] > 360) return false;
    v[2] = u16(b + 92) / 1000.0f;
    if (v[2] > 20) return false;
    for (int t = 0; t < 4; ++t) {
        const int raw = i16(b + t * 2);
        if (raw >= -400 && raw <= 2000) v[21 + t] = raw / 10.0f;
    }
    v[25] = b[10];
    v[connection_source] = 1;
    // hpwr_cap at byte 94 is an enum, not a watt value. Unknown values leave
    // only this source unavailable; the remaining measurements remain valid.
    constexpr float cable_ratings[] = {600, 450, 300, 150};
    if (b[94] < 4) v[cable_rating_source] = cable_ratings[b[94]];
    output = v;
    return true;
}
}
