#pragma once

#include <cmath>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace esphome::light {

enum class ColorMode { ON_OFF, BRIGHTNESS, RGB, COLOR_TEMPERATURE };

class LightTraits {
 public:
  void set_supported_color_modes(std::initializer_list<ColorMode> modes) { modes_ = modes; }
  void set_min_mireds(float value) { min_mireds_ = value; }
  void set_max_mireds(float value) { max_mireds_ = value; }
  std::vector<ColorMode> modes_;
  float min_mireds_{0};
  float max_mireds_{0};
};

class LightState;

class LightRemoteValuesListener {
 public:
  virtual ~LightRemoteValuesListener() = default;
  virtual void on_light_remote_values_update() = 0;
};

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
  float get_color_temperature() const { return color_temperature_; }

  bool state_{false};
  float brightness_{1.0f};
  float color_brightness_{1.0f};
  float red_{1.0f};
  float green_{1.0f};
  float blue_{1.0f};
  float color_temperature_{153.846f};
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
  LightCall &set_effect(const char *name) { effect_ = std::string(name); return *this; }
  LightCall &set_effect(const std::string &name) { effect_ = name; return *this; }
  LightCall &set_transition_length(uint32_t value) { transition_length_ = value; return *this; }
  LightCall &set_color_temperature(float value) { color_temperature_ = value; return *this; }
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
  std::optional<float> color_temperature_;
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
  void add_remote_values_listener(LightRemoteValuesListener *listener) { listeners_.push_back(listener); }
  size_t get_effect_count() const { return effects_.size(); }
  // Mirrors LightState's 1-indexed effect cursor; 0 means no effect.
  uint32_t get_current_effect_index() const { return active_effect_index_; }
  std::string get_effect_name_by_index(uint32_t index) const {
    if (index == 0 || index > effects_.size())
      return {};
    return std::string(effects_[index - 1]->get_name());
  }
  uint32_t effect_index_by_name(const std::string &name) const {
    for (size_t i = 0; i < effects_.size(); i++) {
      if (name == effects_[i]->get_name())
        return static_cast<uint32_t>(i) + 1;
    }
    return 0;
  }
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
  size_t effect_while_turning_off_count{0};
  uint32_t active_effect_index_{0};
  std::string current_effect;

 protected:
  friend class LightCall;
  LightOutput *output_;
  std::vector<LightEffect *> effects_;
  std::vector<LightRemoteValuesListener *> listeners_;
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
  if (this->color_temperature_.has_value())
    values.color_temperature_ = *this->color_temperature_;
  // Effect resolution follows LightCall::validate_/perform in ESPHome:
  //  - an effect is never started by a call that turns the light off;
  //  - an explicit turn-off stops whatever effect is running.
  // Both matter here: without the second rule a test could "prove" that an
  // effect survives an MCU-originated off report when on real hardware it does
  // not.
  const bool explicit_turn_off = this->state_.has_value() && !*this->state_;
  if (this->effect_.has_value() && !values.state_) {
    this->parent_->effect_while_turning_off_count++;
  } else if (this->effect_.has_value()) {
    const uint32_t index = this->parent_->effect_index_by_name(*this->effect_);
    if (index != this->parent_->active_effect_index_) {
      this->parent_->active_effect_index_ = index;
      this->parent_->current_effect = this->parent_->get_effect_name_by_index(index);
      if (index != 0)
        this->parent_->effects_[index - 1]->start();
    }
  } else if (explicit_turn_off && this->parent_->active_effect_index_ != 0) {
    this->parent_->active_effect_index_ = 0;
    this->parent_->current_effect.clear();
  }
  this->parent_->remote_values = values;
  for (auto *listener : this->parent_->listeners_)
    listener->on_light_remote_values_update();
  this->parent_->output_->update_state(this->parent_);
  this->parent_->pending_write_ = true;
  this->parent_->publish_count++;
}

}  // namespace esphome::light
