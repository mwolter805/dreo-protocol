#pragma once

#include <cstddef>

namespace esphome::switch_ {

class Switch {
 public:
  virtual ~Switch() = default;
  void publish_state(bool value) {
    state = value;
    publish_count++;
  }
  void make_call() {}

  bool state{false};
  size_t publish_count{0};

 protected:
  virtual void write_state(bool) = 0;
};

}

#define LOG_SWITCH(prefix, type, obj) do { } while (0)
