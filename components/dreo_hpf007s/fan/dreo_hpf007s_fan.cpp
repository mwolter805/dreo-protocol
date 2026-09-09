#include "dreo_hpf007s_fan.h"

#include <cstring>

#include "esphome/core/log.h"

namespace esphome::dreo_hpf007s {

static const char *const TAG = "dreo_hpf007s.fan";
static const char *const PRESET_NAMES[] = {"Normal", "Natural", "Sleep", "Auto", "Turbo", "Custom"};
static constexpr size_t PRESET_COUNT = sizeof(PRESET_NAMES) / sizeof(PRESET_NAMES[0]);

void DreoHpf007sFan::setup() {
  this->set_supported_preset_modes({"Normal", "Natural", "Sleep", "Auto", "Turbo", "Custom"});
}

void DreoHpf007sFan::dump_config() { LOG_FAN("", "Dreo DR-HPF007S fan", this); }

fan::FanTraits DreoHpf007sFan::get_traits() {
  // Oscillation is two independent axes on this fan and is exposed as
  // switches, not as the single fan oscillation flag.
  fan::FanTraits traits(false, true, false, SPEED_MAX);
  this->wire_preset_modes_(traits);
  return traits;
}

void DreoHpf007sFan::control(const fan::FanCall &call) {
  optional<uint8_t> speed{};
  optional<uint8_t> mode{};
  if (call.get_speed().has_value())
    speed = static_cast<uint8_t>(*call.get_speed());
  if (call.has_preset_mode()) {
    for (size_t i = 0; i < PRESET_COUNT; i++) {
      if (strcmp(call.get_preset_mode(), PRESET_NAMES[i]) == 0)
        mode = static_cast<uint8_t>(i + 1);
    }
  }
  this->parent_->control_fan(call.get_state().value_or(false), call.get_state().has_value() && !*call.get_state(),
                             speed, mode);
}

void DreoHpf007sFan::publish_from_coordinator(bool state, optional<uint8_t> speed, optional<uint8_t> mode) {
  this->state = state;
  if (speed.has_value())
    this->speed = *speed;
  if (mode.has_value()) {
    if (*mode >= 1 && *mode <= PRESET_COUNT)
      this->set_preset_mode_(PRESET_NAMES[*mode - 1]);
    else
      this->clear_preset_mode_();
  }
  this->publish_state();
}

}  // namespace esphome::dreo_hpf007s
