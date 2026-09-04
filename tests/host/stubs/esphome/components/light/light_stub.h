#pragma once

#include <cmath>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace esphome::light {

enum class ColorMode { ON_OFF, BRIGHTNESS, RGB };

class LightTraits {
 public:
  void set_supported_color_modes(std::initializer_list<ColorMode> modes) { modes_ = modes; }
  std::vector<ColorMode> modes_;
};

class LightState;

class LightEffect {
 public:
  explicit LightEffect(const char *name) : name_(name) {}
  virtual ~LightEffect() = default;
  virtual void start() {}
  virtual void apply() = 0;
  void init_internal(LightState *state) { state_ = state; }
  LightState *get_light_state() const { return state_; }
  const char *get_name() const { return name_; }

 protected:
  LightState *state_{nullptr};
  const char *name_;
};

class LightColorValues {
 public:
  bool is_on() const { return state_; }
  float get_brightness() const { return brightness_; }
  float get_color_brightness() const { return color_brightness_; }
  float get_red() const { return red_; }
  float get_green() const { return green_; }
  float get_blue() const { return blue_; }

  bool state_{false};
  float brightness_{1.0f};
  float color_brightness_{1.0f};
  float red_{1.0f};
  float green_{1.0f};
  float blue_{1.0f};
};

class LightOutput {
 public:
  virtual ~LightOutput() = default;
  virtual LightTraits get_traits() = 0;
  virtual void setup_state(LightState *) {}
  virtual void update_state(LightState *) {}
  virtual void write_state(LightState *) = 0;
};

class LightCall {
 public:
  explicit LightCall(LightState *parent) : parent_(parent) {}
  LightCall &set_state(bool value) { state_ = value; return *this; }
  LightCall &set_brightness(float value) { brightness_ = value; return *this; }
  LightCall &set_rgb(float red, float green, float blue) {
    red_ = red;
    green_ = green;
    blue_ = blue;
    return *this;
  }
  LightCall &set_effect(const char *name) { effect_ = name; return *this; }
  LightCall &set_transition_length(uint32_t value) { transition_length_ = value; return *this; }
  LightCall &set_save(bool) { return *this; }
  void perform();

 protected:
  LightState *parent_;
  std::optional<bool> state_;
  std::optional<float> brightness_;
  std::optional<float> red_;
  std::optional<float> green_;
  std::optional<float> blue_;
  std::optional<std::string> effect_;
  std::optional<uint32_t> transition_length_;
};

class LightState {
 public:
  explicit LightState(LightOutput *output) : output_(output) { output_->setup_state(this); }
  LightCall make_call() { return LightCall(this); }
  void add_effects(std::initializer_list<LightEffect *> effects) {
    effects_ = effects;
    for (auto *effect : effects_)
      effect->init_internal(this);
  }
  LightOutput *get_output() const { return output_; }
  size_t get_effect_count() const { return effects_.size(); }
  void flush() {
    if (pending_write_) {
      pending_write_ = false;
      output_->write_state(this);
    }
  }

  LightColorValues current_values;
  LightColorValues remote_values;
  size_t publish_count{0};
  size_t invalid_effect_transition_count{0};
  std::string current_effect;

 protected:
  friend class LightCall;
  LightOutput *output_;
  std::vector<LightEffect *> effects_;
  bool pending_write_{false};
};

inline void LightCall::perform() {
  if (this->effect_.has_value() && this->transition_length_.has_value())
    this->parent_->invalid_effect_transition_count++;
  auto &values = this->parent_->current_values;
  if (this->state_.has_value())
    values.state_ = *this->state_;
  if (this->brightness_.has_value())
    values.brightness_ = *this->brightness_;
  if (this->red_.has_value() || this->green_.has_value() || this->blue_.has_value()) {
    float red = this->red_.value_or(values.red_ * values.color_brightness_);
    float green = this->green_.value_or(values.green_ * values.color_brightness_);
    float blue = this->blue_.value_or(values.blue_ * values.color_brightness_);
    float maximum = std::max(red, std::max(green, blue));
    values.color_brightness_ = maximum;
    if (maximum > 0.0f) {
      values.red_ = red / maximum;
      values.green_ = green / maximum;
      values.blue_ = blue / maximum;
    }
  }
  if (this->effect_.has_value()) {
    for (auto *effect : this->parent_->effects_) {
      if (*this->effect_ == effect->get_name()) {
        this->parent_->current_effect = *this->effect_;
        effect->start();
        break;
      }
    }
  }
  this->parent_->remote_values = values;
  this->parent_->output_->update_state(this->parent_);
  this->parent_->pending_write_ = true;
  this->parent_->publish_count++;
}

}  // namespace esphome::light
