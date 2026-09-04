#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {

inline uint32_t encode_uint32(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3) {
  return (static_cast<uint32_t>(byte0) << 24) | (static_cast<uint32_t>(byte1) << 16) |
         (static_cast<uint32_t>(byte2) << 8) | static_cast<uint32_t>(byte3);
}

inline uint32_t &millis_storage() {
  static uint32_t now = 0;
  return now;
}
inline uint32_t millis() { return millis_storage(); }
inline void set_millis(uint32_t now) { millis_storage() = now; }
inline void advance_millis(uint32_t amount) { millis_storage() += amount; }

constexpr size_t format_hex_pretty_size(size_t size) { return size * 3 + 1; }
inline const char *format_hex_pretty_to(char *buffer, const uint8_t *, size_t) {
  buffer[0] = '\0';
  return buffer;
}

}  // namespace esphome
