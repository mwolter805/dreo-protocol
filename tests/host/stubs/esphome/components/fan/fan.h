#pragma once

#include <optional>

namespace esphome::fan {

enum class FanDirection { FORWARD, REVERSE };

class FanTraits {
 public:
  FanTraits(bool oscillation = false, bool speed = false, bool direction = false, int speed_count = 0)
      : oscillation_(oscillation), speed_(speed), direction_(direction), speed_count_(speed_count) {}
  bool oscillation_;
  bool speed_;
  bool direction_;
  int speed_count_;
};

class Fan;

class FanCall {
 public:
  explicit FanCall(Fan &parent) : parent_(parent) {}
  FanCall &set_state(bool value) { state_ = value; return *this; }
  FanCall &set_oscillating(bool value) { oscillating_ = value; return *this; }
  FanCall &set_speed(int value) { speed_ = value; return *this; }
  FanCall &set_direction(FanDirection value) { direction_ = value; return *this; }
  const std::optional<bool> &get_state() const { return state_; }
  const std::optional<bool> &get_oscillating() const { return oscillating_; }
  const std::optional<int> &get_speed() const { return speed_; }
  const std::optional<FanDirection> &get_direction() const { return direction_; }
  void perform();

 protected:
  Fan &parent_;
  std::optional<bool> state_;
  std::optional<bool> oscillating_;
  std::optional<int> speed_;
  std::optional<FanDirection> direction_;
};

class FanRestoreState {
 public:
  FanCall to_call(Fan &fan);
};

class Fan {
 public:
  virtual ~Fan() = default;
  virtual FanTraits get_traits() = 0;
  FanCall make_call() { return FanCall(*this); }
  void publish_state() { publish_count++; }

  bool state{false};
  bool oscillating{false};
  int speed{0};
  FanDirection direction{FanDirection::FORWARD};
  size_t publish_count{0};

 protected:
  virtual void control(const FanCall &) = 0;
  std::optional<FanRestoreState> restore_state_() { return {}; }
  friend class FanCall;
};

inline void FanCall::perform() { this->parent_.control(*this); }
inline FanCall FanRestoreState::to_call(Fan &fan) { return fan.make_call(); }

}  // namespace esphome::fan

#define LOG_FAN(...) ((void) 0)
