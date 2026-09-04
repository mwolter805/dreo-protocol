#include "dreo_ceiling_fan_light.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::dreo_ceiling_fan {

static const char *const TAG = "dreo_ceiling_fan.light";

void DreoCeilingFanLight::dump_config() { ESP_LOGCONFIG(TAG, "Dreo ceiling fan main light"); }

light::LightTraits DreoCeilingFanLight::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(1000000.0f / this->cold_white_kelvin_);
  traits.set_max_mireds(1000000.0f / this->warm_white_kelvin_);
  return traits;
}

uint8_t DreoCeilingFanLight::color_temperature_to_wire_(float mireds) const {
  const float kelvin = 1000000.0f / mireds;
  const float normalized = (kelvin - this->warm_white_kelvin_) /
                           (this->cold_white_kelvin_ - this->warm_white_kelvin_);
  return static_cast<uint8_t>(std::lround(std::clamp(normalized, 0.0f, 1.0f) * 100.0f));
}

float DreoCeilingFanLight::wire_to_color_temperature_(uint8_t value) const {
  const float kelvin = this->warm_white_kelvin_ +
                       (this->cold_white_kelvin_ - this->warm_white_kelvin_) *
                           std::clamp(value / 100.0f, 0.0f, 1.0f);
  return 1000000.0f / kelvin;
}

void DreoCeilingFanLight::write_state(light::LightState *state) {
  if (this->suppress_scheduled_write_) {
    this->suppress_scheduled_write_ = false;
    return;
  }
  const bool is_on = state->current_values.is_on();
  optional<uint8_t> brightness{};
  optional<uint8_t> color_temperature{};
  if (is_on) {
    brightness = static_cast<uint8_t>(std::clamp(
        std::lround(std::clamp(state->current_values.get_brightness(), 0.0f, 1.0f) * 100.0f), 1L, 100L));
    color_temperature = this->color_temperature_to_wire_(state->current_values.get_color_temperature());
  }
  this->parent_->control_main_light(is_on, !is_on, brightness, color_temperature);
}

void DreoCeilingFanLight::publish_from_coordinator(bool state, optional<uint8_t> brightness,
                                                optional<uint8_t> color_temperature) {
  if (this->state_ == nullptr)
    return;
  this->publishing_from_mcu_ = true;
  auto call = this->state_->make_call();
  call.set_state(state).set_transition_length(0).set_save(false);
  if (brightness.has_value())
    call.set_brightness(std::clamp(static_cast<float>(*brightness) / 100.0f, 0.01f, 1.0f));
  if (color_temperature.has_value())
    call.set_color_temperature(this->wire_to_color_temperature_(*color_temperature));
  call.perform();
  this->publishing_from_mcu_ = false;
}

}  // namespace esphome::dreo_ceiling_fan
