#pragma once

#include <string>

namespace esphome::text_sensor {

class TextSensor {
 public:
  virtual ~TextSensor() = default;
  void publish_state(const std::string &value) {
    state = value;
    has_state_ = true;
    publish_count++;
  }
  bool has_state() const { return has_state_; }
  void set_has_state(bool value) { has_state_ = value; }

  std::string state;
  size_t publish_count{0};

 protected:
  bool has_state_{false};
};

}  // namespace esphome::text_sensor

#define LOG_TEXT_SENSOR(...) ((void) 0)
