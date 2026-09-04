#pragma once

#include <cstddef>

#include "esphome/core/preferences.h"

namespace esphome::number {

class NumberTraits {
 public:
  float get_min_value() const { return min_value_; }
  float get_max_value() const { return max_value_; }
  float min_value_{0.0f};
  float max_value_{100.0f};
};

class Number {
 public:
  virtual ~Number() = default;
  void publish_state(float value) {
    state = value;
    publish_count++;
  }
  template<typename T> ESPPreferenceObject make_entity_preference() { return {}; }

  NumberTraits traits;
  float state{0.0f};
  size_t publish_count{0};

 protected:
  virtual void control(float) = 0;
};

}

#define LOG_NUMBER(prefix, type, obj) do { } while (0)
