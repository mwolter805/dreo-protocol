#include "dreo_light.h"

#include <cmath>

#include "esphome/core/log.h"

namespace esphome::dreo {

static const char *const TAG = "dreo.light";

void DreoLightEffect::start() {
  if (this->state_ != nullptr)
    static_cast<DreoLight *>(this->state_->get_output())->select_effect(this->value_);
}

void DreoLight::setup_state(light::LightState *state) { this->state_ = state; }

void DreoLight::setup() {
  if (this->switch_id_.has_value()) {
    this->parent_->register_listener(*this->switch_id_, [this](const DreoDatapoint &datapoint) {
      if (datapoint.type != DreoDatapointType::BOOLEAN || this->state_ == nullptr)
        return;
      this->wire_state_ = datapoint.value_bool;
      auto call = this->state_->make_call();
      call.set_state(datapoint.value_bool);
      this->publish_mcu_call_(call);
    });
  }

  if (this->brightness_id_.has_value()) {
    this->parent_->register_listener(*this->brightness_id_, [this](const DreoDatapoint &datapoint) {
      if ((datapoint.type != DreoDatapointType::INTEGER && datapoint.type != DreoDatapointType::ENUM) ||
          this->state_ == nullptr)
        return;
      uint32_t value = datapoint.type == DreoDatapointType::INTEGER ? datapoint.value_int : datapoint.value_enum;
      auto brightness = this->brightness_for_value_(value);
      if (!brightness.has_value()) {
        ESP_LOGW(TAG, "Datapoint %u reported unknown brightness value %" PRIu32, *this->brightness_id_, value);
        return;
      }
      this->brightness_type_ = datapoint.type;
      this->wire_brightness_ = value;
      auto call = this->state_->make_call();
      call.set_brightness(*brightness);
      this->publish_mcu_call_(call);
    });
  }

  if (this->effect_id_.has_value()) {
    this->parent_->register_listener(*this->effect_id_, [this](const DreoDatapoint &datapoint) {
      if ((datapoint.type != DreoDatapointType::INTEGER && datapoint.type != DreoDatapointType::ENUM) ||
          this->state_ == nullptr)
        return;
      uint32_t value = datapoint.type == DreoDatapointType::INTEGER ? datapoint.value_int : datapoint.value_enum;
      const char *name = this->effect_name_for_value_(value);
      if (name == nullptr) {
        ESP_LOGW(TAG, "Datapoint %u reported unknown effect value %" PRIu32, *this->effect_id_, value);
        return;
      }
      this->effect_type_ = datapoint.type;
      this->wire_effect_ = value;
      if (this->wire_state_.has_value() && !*this->wire_state_)
        return;
      auto call = this->state_->make_call();
      call.set_effect(name);
      this->publish_mcu_call_(call, true);
    });
  }

  if (this->rgb_id_.has_value()) {
    this->parent_->register_listener(*this->rgb_id_, [this](const DreoDatapoint &datapoint) {
      if (datapoint.type != DreoDatapointType::INTEGER || this->state_ == nullptr)
        return;
      uint32_t value = static_cast<uint32_t>(datapoint.value_int) & 0x00FFFFFF;
      this->rgb_type_ = datapoint.type;
      this->wire_rgb_ = value;
      auto call = this->state_->make_call();
      call.set_rgb(static_cast<float>((value >> 16) & 0xFF) / 255.0f,
                   static_cast<float>((value >> 8) & 0xFF) / 255.0f,
                   static_cast<float>(value & 0xFF) / 255.0f);
      this->publish_mcu_call_(call);
    });
  }
}

void DreoLight::update_state(light::LightState *) {
  this->suppress_scheduled_write_ = this->publishing_from_mcu_;
}

void DreoLight::write_state(light::LightState *state) {
  if (this->suppress_scheduled_write_) {
    this->suppress_scheduled_write_ = false;
    return;
  }

  bool is_on = state->current_values.is_on();
  bool state_changed = this->switch_id_.has_value() && (!this->wire_state_.has_value() || *this->wire_state_ != is_on);
  if (state_changed) {
    if (this->parent_->force_set_boolean_datapoint_value(*this->switch_id_, is_on)) {
      this->wire_state_ = is_on;
    } else {
      this->restore_visible_state_();
      return;
    }
  }

  if (!is_on) {
    this->requested_effect_.reset();
    return;
  }

  if (this->requested_effect_.has_value()) {
    uint32_t value = *this->requested_effect_;
    this->requested_effect_.reset();
    if (this->effect_id_.has_value() && this->send_numeric_(*this->effect_id_, this->effect_type_, value)) {
      this->wire_effect_ = value;
    } else {
      this->restore_visible_state_();
    }
    return;
  }

  if (this->brightness_id_.has_value() && this->wire_brightness_.has_value()) {
    auto value = this->value_for_brightness_(state->current_values.get_brightness());
    if (value.has_value() && *value != *this->wire_brightness_) {
      if (!this->send_numeric_(*this->brightness_id_, this->brightness_type_, *value)) {
        this->restore_visible_state_();
        return;
      }
      this->wire_brightness_ = *value;
    }
  }

  if (this->rgb_id_.has_value() && this->wire_rgb_.has_value()) {
    uint32_t value = this->rgb_from_state_(state);
    if (value != *this->wire_rgb_) {
      bool selected_constant = false;
      if (this->constant_effect_.has_value() && this->effect_id_.has_value() &&
          (!this->wire_effect_.has_value() || *this->wire_effect_ != *this->constant_effect_)) {
        if (!this->send_numeric_(*this->effect_id_, this->effect_type_, *this->constant_effect_)) {
          this->restore_visible_state_();
          return;
        }
        this->wire_effect_ = *this->constant_effect_;
        selected_constant = true;
      }
      if (this->send_numeric_(*this->rgb_id_, this->rgb_type_, value)) {
        this->wire_rgb_ = value;
        if (selected_constant) {
          const char *name = this->effect_name_for_value_(*this->constant_effect_);
          if (name != nullptr) {
            auto call = this->state_->make_call();
            call.set_effect(name);
            this->publish_mcu_call_(call, true);
          }
        }
      } else {
        this->restore_visible_state_();
      }
    }
  }
}

light::LightTraits DreoLight::get_traits() {
  light::LightTraits traits;
  if (this->rgb_id_.has_value()) {
    traits.set_supported_color_modes({light::ColorMode::RGB});
  } else if (this->brightness_id_.has_value()) {
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
  } else {
    traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  }
  return traits;
}

void DreoLight::select_effect(uint32_t value) {
  if (!this->publishing_from_mcu_)
    this->requested_effect_ = value;
}

void DreoLight::publish_mcu_call_(light::LightCall &call, bool includes_effect) {
  this->publishing_from_mcu_ = true;
  if (!includes_effect)
    call.set_transition_length(0);
  call.set_save(false);
  call.perform();
  this->publishing_from_mcu_ = false;
}

void DreoLight::restore_visible_state_() {
  this->requested_effect_.reset();
  if (this->state_ == nullptr)
    return;
  auto call = this->state_->make_call();
  bool includes_effect = false;
  if (this->wire_state_.has_value())
    call.set_state(*this->wire_state_);
  if (this->wire_brightness_.has_value()) {
    auto brightness = this->brightness_for_value_(*this->wire_brightness_);
    if (brightness.has_value())
      call.set_brightness(*brightness);
  }
  if (this->wire_effect_.has_value() && (!this->wire_state_.has_value() || *this->wire_state_)) {
    const char *effect = this->effect_name_for_value_(*this->wire_effect_);
    if (effect != nullptr) {
      call.set_effect(effect);
      includes_effect = true;
    }
  }
  if (this->wire_rgb_.has_value()) {
    uint32_t value = *this->wire_rgb_;
    call.set_rgb(static_cast<float>((value >> 16) & 0xFF) / 255.0f,
                 static_cast<float>((value >> 8) & 0xFF) / 255.0f,
                 static_cast<float>(value & 0xFF) / 255.0f);
  }
  this->publish_mcu_call_(call, includes_effect);
}

bool DreoLight::send_numeric_(uint8_t id, const optional<DreoDatapointType> &type, uint32_t value) {
  if (!type.has_value()) {
    ESP_LOGW(TAG, "Waiting for datapoint %u type before writing", id);
    return false;
  }
  if (*type == DreoDatapointType::INTEGER)
    return this->parent_->force_set_integer_datapoint_value(id, value);
  if (*type == DreoDatapointType::ENUM)
    return this->parent_->force_set_enum_datapoint_value(id, value);
  ESP_LOGW(TAG, "Datapoint %u has unsupported light type %u", id, static_cast<uint8_t>(*type));
  return false;
}

optional<float> DreoLight::brightness_for_value_(uint32_t value) const {
  for (const auto &mapping : this->brightness_mappings_) {
    if (mapping.value == value)
      return mapping.brightness;
  }
  return {};
}

optional<uint32_t> DreoLight::value_for_brightness_(float brightness) const {
  if (this->brightness_mappings_.empty())
    return {};
  const DreoBrightnessMapping *best = &this->brightness_mappings_.front();
  float best_distance = std::fabs(brightness - best->brightness);
  for (const auto &mapping : this->brightness_mappings_) {
    float distance = std::fabs(brightness - mapping.brightness);
    if (distance < best_distance) {
      best = &mapping;
      best_distance = distance;
    }
  }
  return best->value;
}

const char *DreoLight::effect_name_for_value_(uint32_t value) const {
  for (const auto &mapping : this->effect_mappings_) {
    if (mapping.value == value)
      return mapping.name.c_str();
  }
  return nullptr;
}

uint32_t DreoLight::rgb_from_state_(light::LightState *state) const {
  const auto &values = state->current_values;
  float color_brightness = values.get_color_brightness();
  uint32_t red = std::lround(values.get_red() * color_brightness * 255.0f);
  uint32_t green = std::lround(values.get_green() * color_brightness * 255.0f);
  uint32_t blue = std::lround(values.get_blue() * color_brightness * 255.0f);
  return ((red & 0xFF) << 16) | ((green & 0xFF) << 8) | (blue & 0xFF);
}

void DreoLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Dreo Light");
  if (this->switch_id_.has_value())
    ESP_LOGCONFIG(TAG, "  Switch has datapoint ID %u", *this->switch_id_);
  if (this->brightness_id_.has_value())
    ESP_LOGCONFIG(TAG, "  Brightness has datapoint ID %u", *this->brightness_id_);
  if (this->effect_id_.has_value())
    ESP_LOGCONFIG(TAG, "  Effect has datapoint ID %u", *this->effect_id_);
  if (this->rgb_id_.has_value())
    ESP_LOGCONFIG(TAG, "  RGB has datapoint ID %u", *this->rgb_id_);
}

}  // namespace esphome::dreo
