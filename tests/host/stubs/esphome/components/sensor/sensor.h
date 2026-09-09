#pragma once

#include <cstddef>

namespace esphome::sensor {

class Sensor {
 public:
  virtual ~Sensor() = default;
  void publish_state(float value) {
    has_state_ = true;
    state = value;
    publish_count++;
  }

  bool has_state() const { return has_state_; }
  void set_has_state(bool value) { has_state_ = value; }
  bool has_state_{false};
  float state{0.0f};
  size_t publish_count{0};
};

}  // namespace esphome::sensor

#define LOG_SENSOR(...) ((void) 0)
