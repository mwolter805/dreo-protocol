#include "dreo_ceiling_fan_fan.h"

#include <cstring>

#include "esphome/core/log.h"

namespace esphome::dreo_ceiling_fan {

static const char *const TAG = "dreo_ceiling_fan.fan";

void DreoCeilingFanFan::setup() { this->set_supported_preset_modes({"Normal", "Natural", "Sleep"}); }

void DreoCeilingFanFan::dump_config() { LOG_FAN("", "Dreo ceiling fan", this); }

fan::FanTraits DreoCeilingFanFan::get_traits() {
  // The preset list lives on the Fan (set in setup()); the traits object only
  // points at it, and only once wired. Returning a bare FanTraits advertised
  // no modes to Home Assistant even though setup() had registered them.
  fan::FanTraits traits(false, true, true, 12);
  this->wire_preset_modes_(traits);
  return traits;
}

void DreoCeilingFanFan::control(const fan::FanCall &call) {
  optional<uint8_t> speed{};
  optional<uint8_t> mode{};
  if (call.get_speed().has_value())
    speed = static_cast<uint8_t>(*call.get_speed());
  if (call.has_preset_mode()) {
    if (strcmp(call.get_preset_mode(), "Normal") == 0)
      mode = 1;
    else if (strcmp(call.get_preset_mode(), "Natural") == 0)
      mode = 2;
    else if (strcmp(call.get_preset_mode(), "Sleep") == 0)
      mode = 3;
  } else if (call.get_direction().has_value()) {
    mode = *call.get_direction() == fan::FanDirection::REVERSE ? 4 : 1;
  }
  this->parent_->control_fan(call.get_state().value_or(false), call.get_state().has_value() && !*call.get_state(),
                             speed, mode);
}

void DreoCeilingFanFan::publish_from_coordinator(bool state, optional<uint8_t> speed, optional<uint8_t> mode) {
  this->state = state;
  if (speed.has_value())
    this->speed = *speed;
  if (mode.has_value()) {
    this->direction = *mode == 4 ? fan::FanDirection::REVERSE : fan::FanDirection::FORWARD;
    if (*mode == 1)
      this->set_preset_mode_("Normal");
    else if (*mode == 2)
      this->set_preset_mode_("Natural");
    else if (*mode == 3)
      this->set_preset_mode_("Sleep");
    else
      this->clear_preset_mode_();
  }
  this->publish_state();
}

}  // namespace esphome::dreo_ceiling_fan
