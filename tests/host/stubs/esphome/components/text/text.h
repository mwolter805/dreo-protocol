#pragma once

#include <string>

namespace esphome::text {

class Text {
 public:
  virtual ~Text() = default;
  void publish_state(const std::string &value) {
    state = value;
    publish_count++;
  }
  std::string state;
  size_t publish_count{0};

 protected:
  virtual void control(const std::string &) = 0;
};

}  // namespace esphome::text

#define LOG_TEXT(...) ((void) 0)
