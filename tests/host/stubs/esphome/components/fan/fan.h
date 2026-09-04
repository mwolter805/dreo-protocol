#pragma once

#include <optional>
#include <cstring>
#include <string>
#include <vector>

namespace esphome::fan {

enum class FanDirection { FORWARD, REVERSE };

class Fan;

// Mirrors ESPHome 2026.8: the traits object does not own the preset list. It
// points at the vector the Fan owns, and only when the platform's
// get_traits() wires it. A platform that returns a fresh FanTraits without
// wiring advertises no preset modes, whatever setup() registered.
class FanTraits {
 public:
  FanTraits(bool oscillation = false, bool speed = false, bool direction = false, int speed_count = 0)
      : oscillation_(oscillation), speed_(speed), direction_(direction), speed_count_(speed_count) {}
  bool supports_oscillation() const { return oscillation_; }
  bool supports_speed() const { return speed_; }
  bool supports_direction() const { return direction_; }
  int supported_speed_count() const { return speed_count_; }
  const std::vector<const char *> &supported_preset_modes() const {
    static const std::vector<const char *> EMPTY{};
    return preset_modes_ == nullptr ? EMPTY : *preset_modes_;
  }
  bool supports_preset_modes() const { return preset_modes_ != nullptr && !preset_modes_->empty(); }

  bool oscillation_;
  bool speed_;
  bool direction_;
  int speed_count_;

 protected:
  void set_supported_preset_modes_(const std::vector<const char *> *preset_modes) { preset_modes_ = preset_modes; }
  const std::vector<const char *> *preset_modes_{nullptr};
  friend class Fan;
};

class FanCall {
 public:
  explicit FanCall(Fan &parent) : parent_(parent) {}
  FanCall &set_state(bool value) { state_ = value; return *this; }
  FanCall &set_oscillating(bool value) { oscillating_ = value; return *this; }
  FanCall &set_speed(int value) { speed_ = value; return *this; }
  FanCall &set_direction(FanDirection value) { direction_ = value; return *this; }
  FanCall &set_preset_mode(const char *value) { preset_mode_ = value; return *this; }
  const std::optional<bool> &get_state() const { return state_; }
  const std::optional<bool> &get_oscillating() const { return oscillating_; }
  const std::optional<int> &get_speed() const { return speed_; }
  const std::optional<FanDirection> &get_direction() const { return direction_; }
  const char *get_preset_mode() const { return preset_mode_; }
  bool has_preset_mode() const { return preset_mode_ != nullptr; }
  void perform();

 protected:
  Fan &parent_;
  std::optional<bool> state_;
  std::optional<bool> oscillating_;
  std::optional<int> speed_;
  std::optional<FanDirection> direction_;
  const char *preset_mode_{nullptr};
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
  void set_supported_preset_modes(std::initializer_list<const char *> modes) { supported_preset_modes_ = modes; }
  std::string get_preset_mode() const { return preset_mode_ == nullptr ? "" : preset_mode_; }

  bool state{false};
  bool oscillating{false};
  int speed{0};
  FanDirection direction{FanDirection::FORWARD};
  size_t publish_count{0};

 protected:
  virtual void control(const FanCall &) = 0;
  std::optional<FanRestoreState> restore_state_() { return {}; }
  // Mirrors Fan::set_preset_mode_() in ESPHome 2026.8: the name is resolved
  // against the Fan-owned list and a pointer into that list is stored; an
  // unknown name changes nothing.
  bool set_preset_mode_(const char *value) {
    if (value == nullptr || *value == '\0') {
      if (preset_mode_ == nullptr)
        return false;
      clear_preset_mode_();
      return true;
    }
    const char *validated = nullptr;
    for (const char *mode : supported_preset_modes_) {
      if (std::strcmp(mode, value) == 0) {
        validated = mode;
        break;
      }
    }
    if (validated == nullptr || preset_mode_ == validated)
      return false;
    preset_mode_ = validated;
    return true;
  }
  void clear_preset_mode_() { preset_mode_ = nullptr; }
  // Mirrors Fan::wire_preset_modes_(): hands the Fan-owned list to a traits
  // object. get_traits() must call this or the traits advertise nothing.
  void wire_preset_modes_(FanTraits &traits) {
    if (!supported_preset_modes_.empty())
      traits.set_supported_preset_modes_(&supported_preset_modes_);
  }
  std::vector<const char *> supported_preset_modes_{};
  const char *preset_mode_{nullptr};
  friend class FanCall;
};

inline void FanCall::perform() { this->parent_.control(*this); }
inline FanCall FanRestoreState::to_call(Fan &fan) { return fan.make_call(); }

}  // namespace esphome::fan

#define LOG_FAN(...) ((void) 0)
