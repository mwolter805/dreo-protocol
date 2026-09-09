#pragma once

#include <cstddef>

namespace esphome::binary_sensor {

class BinarySensor;

}  // namespace esphome::binary_sensor

namespace esphome {

// Mirrors the part of ESPHome 2026.8's controller registry that
// BinarySensor::set_new_state() and the Dreo text-sensor adapter notify: a
// count of updates and whether the last one carried a missing state.
class BinarySensorRegistryStub {
 public:
  static void notify(bool has_state) {
    notify_count++;
    last_missing_state = !has_state;
  }
  inline static size_t notify_count{0};
  inline static bool last_missing_state{false};
};

}  // namespace esphome

namespace esphome::binary_sensor {

// Mirrors ESPHome 2026.8's StatefulEntityBase<bool>: publish_state() sets the
// value, invalidate_state() clears has_state() and both notify the registry.
class BinarySensor {
 public:
  virtual ~BinarySensor() = default;
  void publish_state(bool value) {
    state = value;
    has_state_ = true;
    publish_count++;
    esphome::BinarySensorRegistryStub::notify(true);
  }
  void invalidate_state() {
    if (!has_state_)
      return;
    has_state_ = false;
    invalidate_count++;
    esphome::BinarySensorRegistryStub::notify(false);
  }
  bool has_state() const { return has_state_; }

  bool state = false;
  size_t publish_count = 0;
  size_t invalidate_count = 0;

 protected:
  bool has_state_{false};
};

}  // namespace esphome::binary_sensor
