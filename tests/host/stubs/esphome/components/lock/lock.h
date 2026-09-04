#pragma once

#include <optional>

namespace esphome::lock {

enum LockState { LOCK_STATE_NONE, LOCK_STATE_LOCKED, LOCK_STATE_UNLOCKED };

class LockCall {
 public:
  LockCall &set_state(LockState value) { state_ = value; return *this; }
  const std::optional<LockState> &get_state() const { return state_; }

 protected:
  std::optional<LockState> state_;
};

class Lock {
 public:
  virtual ~Lock() = default;
  void publish_state(LockState value) {
    state = value;
    publish_count++;
  }
  LockState state{LOCK_STATE_NONE};
  size_t publish_count{0};

 protected:
  virtual void control(const LockCall &) = 0;
};

}  // namespace esphome::lock

#define LOG_LOCK(...) ((void) 0)
