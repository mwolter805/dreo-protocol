#include "dreo_ceiling_fan.h"

#include "fan/dreo_ceiling_fan_fan.h"
#include "light/dreo_ceiling_fan_light.h"
#include "esphome/core/log.h"

namespace esphome::dreo_ceiling_fan {

static const char *const TAG = "dreo_ceiling_fan";

// DR-HCF010S stock-protocol datapoints.
static constexpr uint8_t DP_MASTER = 1;
static constexpr uint8_t DP_FAN_POWER = 3;
static constexpr uint8_t DP_MAIN_LIGHT_POWER = 4;
static constexpr uint8_t DP_AMBIENT_POWER = 5;
static constexpr uint8_t DP_FAN_MODE = 6;
static constexpr uint8_t DP_FAN_SPEED = 7;
static constexpr uint8_t DP_MAIN_LIGHT_BRIGHTNESS = 8;
static constexpr uint8_t DP_MAIN_LIGHT_COLOR_TEMPERATURE = 9;
static constexpr uint8_t DP_PRESET_CURSOR = 25;  // rgbpresetsel, reported by the MCU
static constexpr uint8_t DP_PRESET_COUNT = 26;   // rgbpresetnum, written by this module
static constexpr uint8_t DP_PREDEFINE = 28;      // string sentinel, "0" on the wire

static const char *const PREDEFINE_SENTINEL = "0";

static bool near_(float a, float b) { return (a > b ? a - b : b - a) < 0.001f; }

void DreoCeilingFan::setup() {
  for (uint8_t id : {DP_MASTER, DP_FAN_POWER, DP_MAIN_LIGHT_POWER, DP_AMBIENT_POWER, DP_FAN_MODE, DP_FAN_SPEED,
                     DP_MAIN_LIGHT_BRIGHTNESS, DP_MAIN_LIGHT_COLOR_TEMPERATURE, DP_PRESET_CURSOR, DP_PRESET_COUNT}) {
    this->parent_->register_listener(id, [this](const dreo::DreoDatapoint &datapoint) {
      this->handle_datapoint_(datapoint);
    });
  }
  this->parent_->add_on_initialized_callback([this]() { this->maintain_preset_count_(); });
}

void DreoCeilingFan::dump_config() { ESP_LOGCONFIG(TAG, "Dreo ceiling fan coordinator"); }

void DreoCeilingFan::set_ambient_light(light::LightState *light) {
  this->ambient_light_ = light;
  if (light != nullptr) {
    light->add_remote_values_listener(this);
    this->snapshot_ambient_();
  }
}

bool DreoCeilingFan::ambient_effective_() const {
  return this->master_.value_or(false) && this->ambient_power_.value_or(false);
}

void DreoCeilingFan::snapshot_ambient_() {
  if (this->ambient_light_ == nullptr)
    return;
  const auto &values = this->ambient_light_->remote_values;
  this->ambient_snapshot_state_ = values.is_on();
  this->ambient_snapshot_effect_ = this->ambient_light_->get_current_effect_index();
  this->ambient_snapshot_brightness_ = values.get_brightness();
  this->ambient_snapshot_color_brightness_ = values.get_color_brightness();
  this->ambient_snapshot_red_ = values.get_red();
  this->ambient_snapshot_green_ = values.get_green();
  this->ambient_snapshot_blue_ = values.get_blue();
  this->ambient_snapshot_valid_ = true;
}

// "Presentation" deliberately excludes on/off: power is carried by dp01/dp05,
// and only a colour, brightness or effect change is what the stock application
// reported to the MCU with the dp28 sentinel.
bool DreoCeilingFan::ambient_presentation_changed_() const {
  if (this->ambient_light_ == nullptr || !this->ambient_snapshot_valid_)
    return false;
  const auto &values = this->ambient_light_->remote_values;
  return this->ambient_light_->get_current_effect_index() != this->ambient_snapshot_effect_ ||
         !near_(values.get_brightness(), this->ambient_snapshot_brightness_) ||
         !near_(values.get_color_brightness(), this->ambient_snapshot_color_brightness_) ||
         !near_(values.get_red(), this->ambient_snapshot_red_) ||
         !near_(values.get_green(), this->ambient_snapshot_green_) ||
         !near_(values.get_blue(), this->ambient_snapshot_blue_);
}

// Keep the MCU's preset count at the four presets this module actually offers.
// Two triggers: once when the hub finishes initializing, and again if a later
// report disagrees. The hub signals initialization before it parses the report
// that carried it, so at that first moment the count is legitimately unknown
// and one write goes out; `preset_count_written_` then prevents any retry loop
// if the MCU never echoes the value back.
void DreoCeilingFan::maintain_preset_count_() {
  if (this->preset_count_.has_value() && *this->preset_count_ == AMBIENT_PRESET_COUNT) {
    this->preset_count_written_ = true;
    return;
  }
  if (this->preset_count_written_)
    return;
  this->preset_count_written_ = true;
  this->parent_->set_integer_datapoint_value(DP_PRESET_COUNT, AMBIENT_PRESET_COUNT);
}

void DreoCeilingFan::send_predefine_sentinel_() {
  // dp28 is a signal, not a state: the MCU reports it as "0" in every status
  // report, so the value never differs from the constant sent here and the
  // ordinary setter would drop every send as unchanged. Force it.
  this->parent_->force_set_string_datapoint_value(DP_PREDEFINE, PREDEFINE_SENTINEL);
}

// dp25 = 0 means "the last effect", which is whatever is already selected, so
// it deliberately changes nothing. Any value past the configured presets is
// ignored rather than clamped: indexing outside the map would silently show
// the wrong preset.
void DreoCeilingFan::apply_preset_cursor_() {
  if (this->ambient_light_ == nullptr || !this->preset_cursor_.has_value())
    return;
  const uint8_t cursor = *this->preset_cursor_;
  if (cursor == 0 || cursor > this->ambient_preset_effects_.size())
    return;
  if (!this->ambient_effective_())
    return;
  if (cursor == this->applied_preset_cursor_)
    return;
  const std::string &name = this->ambient_preset_effects_[cursor - 1];
  if (name.empty())
    return;
  this->applied_preset_cursor_ = cursor;
  this->local_effect_ = name;
  this->applying_preset_ = true;
  this->ambient_light_->make_call().set_effect(name).set_save(false).perform();
  this->applying_preset_ = false;
  this->snapshot_ambient_();
}

// ESPHome stops the running effect on an explicit turn-off, so an ambient light
// that the MCU switched off comes back with no effect selected. Re-selecting
// the remembered one is what makes a later on report resume it.
void DreoCeilingFan::restore_local_effect_() {
  if (this->ambient_light_ == nullptr || this->local_effect_.empty())
    return;
  if (!this->ambient_light_->remote_values.is_on())
    return;
  if (this->ambient_light_->get_current_effect_index() != 0)
    return;
  this->applying_preset_ = true;
  this->ambient_light_->make_call().set_effect(this->local_effect_).set_save(false).perform();
  this->applying_preset_ = false;
  this->snapshot_ambient_();
}

bool DreoCeilingFan::parents_ready_(uint8_t child) const {
  return this->master_.has_value() && !this->parent_->is_datapoint_pending(DP_MASTER) &&
         ((child == DP_FAN_POWER && this->fan_power_.has_value() && !this->parent_->is_datapoint_pending(child)) ||
          (child == DP_MAIN_LIGHT_POWER && this->main_light_power_.has_value() &&
           !this->parent_->is_datapoint_pending(child)));
}

bool DreoCeilingFan::setting_allowed_(uint8_t child) const {
  if (!this->parents_ready_(child))
    return false;
  const bool child_on = child == DP_FAN_POWER ? *this->fan_power_ : *this->main_light_power_;
  return this->parent_->allow_sub_entity_control_while_off() || (*this->master_ && child_on);
}

bool DreoCeilingFan::authorize_command(const dreo::DreoDatapointCommand &command) const {
  switch (command.datapoint_id) {
    case DP_MASTER:
    case DP_FAN_POWER:
    case DP_MAIN_LIGHT_POWER:
    case DP_AMBIENT_POWER:
    // Both of these are module-maintenance writes the stock application makes
    // regardless of whether any output is on: the preset count it reports to
    // the MCU, and the sentinel that resets the remote's cursor.
    case DP_PRESET_COUNT:
    case DP_PREDEFINE:
      return true;
    case DP_FAN_MODE:
    case DP_FAN_SPEED:
      return this->setting_allowed_(DP_FAN_POWER);
    case DP_MAIN_LIGHT_BRIGHTNESS:
    case DP_MAIN_LIGHT_COLOR_TEMPERATURE:
      return this->setting_allowed_(DP_MAIN_LIGHT_POWER);
    default:
      return this->master_.has_value() && !this->parent_->is_datapoint_pending(DP_MASTER) && *this->master_;
  }
}

void DreoCeilingFan::handle_datapoint_(const dreo::DreoDatapoint &datapoint) {
  if (datapoint.id >= DP_MASTER && datapoint.id <= DP_AMBIENT_POWER) {
    if (datapoint.type != dreo::DreoDatapointType::BOOLEAN)
      return;
    switch (datapoint.id) {
      case DP_MASTER:
        this->master_ = datapoint.value_bool;
        break;
      case DP_FAN_POWER:
        this->fan_power_ = datapoint.value_bool;
        break;
      case DP_MAIN_LIGHT_POWER:
        this->main_light_power_ = datapoint.value_bool;
        break;
      case DP_AMBIENT_POWER:
        this->ambient_power_ = datapoint.value_bool;
        break;
    }
  } else {
    if (datapoint.type != dreo::DreoDatapointType::INTEGER)
      return;
    const auto value = static_cast<uint8_t>(datapoint.value_int);
    switch (datapoint.id) {
      case DP_FAN_MODE:
        this->fan_mode_ = value;
        break;
      case DP_FAN_SPEED:
        this->fan_speed_ = value;
        break;
      case DP_MAIN_LIGHT_BRIGHTNESS:
        this->main_light_brightness_ = value;
        break;
      case DP_MAIN_LIGHT_COLOR_TEMPERATURE:
        this->main_light_color_temperature_ = value;
        break;
      case DP_PRESET_CURSOR:
        this->preset_cursor_ = value;
        break;
      case DP_PRESET_COUNT:
        this->preset_count_ = value;
        this->maintain_preset_count_();
        break;
    }
  }

  this->flush_fan_settings_();
  this->flush_main_light_settings_();
  this->publish_fan_();
  this->publish_main_light_();
  this->publish_ambient_();
  this->apply_preset_cursor_();
}

void DreoCeilingFan::request_feature_state_(uint8_t child, bool state) {
  if (!state) {
    const bool fan_effective = this->master_.value_or(false) && this->fan_power_.value_or(false);
    const bool light_effective = this->master_.value_or(false) && this->main_light_power_.value_or(false);
    const bool ambient_effective = this->master_.value_or(false) && this->ambient_power_.value_or(false);
    const unsigned active = static_cast<unsigned>(fan_effective) + static_cast<unsigned>(light_effective) +
                            static_cast<unsigned>(ambient_effective);
    if (active <= 1 && this->master_.value_or(false))
      this->parent_->set_boolean_datapoint_value(DP_MASTER, false);
    else
      this->parent_->set_boolean_datapoint_value(child, false);
    return;
  }

  if (!this->master_.value_or(false)) {
    for (uint8_t other : {DP_FAN_POWER, DP_MAIN_LIGHT_POWER, DP_AMBIENT_POWER}) {
      if (other != child)
        this->parent_->set_boolean_datapoint_value(other, false);
    }
  }
  this->parent_->set_boolean_datapoint_value(child, true);
  this->parent_->set_boolean_datapoint_value(DP_MASTER, true);
}

void DreoCeilingFan::control_fan(bool turn_on, bool turn_off, optional<uint8_t> speed, optional<uint8_t> mode) {
  if (turn_off) {
    this->queued_fan_speed_.reset();
    this->queued_fan_mode_.reset();
    this->request_feature_state_(DP_FAN_POWER, false);
    return;
  }
  const bool effective = this->master_.value_or(false) && this->fan_power_.value_or(false);
  if (turn_on && !effective) {
    if (speed.has_value())
      this->queued_fan_speed_ = speed;
    if (mode.has_value())
      this->queued_fan_mode_ = mode;
    if (!this->parent_->is_datapoint_pending(DP_MASTER) && !this->parent_->is_datapoint_pending(DP_FAN_POWER))
      this->request_feature_state_(DP_FAN_POWER, true);
    return;
  }
  if (mode.has_value())
    this->parent_->set_integer_datapoint_value(DP_FAN_MODE, *mode);
  if (speed.has_value())
    this->parent_->set_integer_datapoint_value(DP_FAN_SPEED, *speed);
  if (turn_on)
    this->request_feature_state_(DP_FAN_POWER, true);
}

void DreoCeilingFan::control_main_light(bool turn_on, bool turn_off, optional<uint8_t> brightness,
                                    optional<uint8_t> color_temperature) {
  if (turn_off) {
    this->queued_main_light_brightness_.reset();
    this->queued_main_light_color_temperature_.reset();
    this->request_feature_state_(DP_MAIN_LIGHT_POWER, false);
    return;
  }
  const bool effective = this->master_.value_or(false) && this->main_light_power_.value_or(false);
  if (turn_on && !effective) {
    if (brightness.has_value())
      this->queued_main_light_brightness_ = brightness;
    if (color_temperature.has_value())
      this->queued_main_light_color_temperature_ = color_temperature;
    if (!this->parent_->is_datapoint_pending(DP_MASTER) &&
        !this->parent_->is_datapoint_pending(DP_MAIN_LIGHT_POWER))
      this->request_feature_state_(DP_MAIN_LIGHT_POWER, true);
    return;
  }
  if (brightness.has_value())
    this->parent_->set_integer_datapoint_value(DP_MAIN_LIGHT_BRIGHTNESS, *brightness);
  if (color_temperature.has_value())
    this->parent_->set_integer_datapoint_value(DP_MAIN_LIGHT_COLOR_TEMPERATURE, *color_temperature);
  if (turn_on)
    this->request_feature_state_(DP_MAIN_LIGHT_POWER, true);
}

void DreoCeilingFan::control_ambient(bool state) { this->request_feature_state_(DP_AMBIENT_POWER, state); }

void DreoCeilingFan::flush_fan_settings_() {
  if (!this->master_.value_or(false) || !this->fan_power_.value_or(false) ||
      this->parent_->is_datapoint_pending(DP_MASTER) || this->parent_->is_datapoint_pending(DP_FAN_POWER))
    return;
  if (this->queued_fan_mode_.has_value()) {
    const uint8_t value = *this->queued_fan_mode_;
    this->queued_fan_mode_.reset();
    this->parent_->set_integer_datapoint_value(DP_FAN_MODE, value);
  }
  if (this->queued_fan_speed_.has_value()) {
    const uint8_t value = *this->queued_fan_speed_;
    this->queued_fan_speed_.reset();
    this->parent_->set_integer_datapoint_value(DP_FAN_SPEED, value);
  }
}

void DreoCeilingFan::flush_main_light_settings_() {
  if (!this->master_.value_or(false) || !this->main_light_power_.value_or(false) ||
      this->parent_->is_datapoint_pending(DP_MASTER) || this->parent_->is_datapoint_pending(DP_MAIN_LIGHT_POWER))
    return;
  if (this->queued_main_light_brightness_.has_value()) {
    const uint8_t value = *this->queued_main_light_brightness_;
    this->queued_main_light_brightness_.reset();
    this->parent_->set_integer_datapoint_value(DP_MAIN_LIGHT_BRIGHTNESS, value);
  }
  if (this->queued_main_light_color_temperature_.has_value()) {
    const uint8_t value = *this->queued_main_light_color_temperature_;
    this->queued_main_light_color_temperature_.reset();
    this->parent_->set_integer_datapoint_value(DP_MAIN_LIGHT_COLOR_TEMPERATURE, value);
  }
}

void DreoCeilingFan::publish_fan_() {
  if (this->fan_ != nullptr)
    this->fan_->publish_from_coordinator(this->master_.value_or(false) && this->fan_power_.value_or(false),
                                         this->fan_speed_, this->fan_mode_);
}

void DreoCeilingFan::publish_main_light_() {
  if (this->main_light_ != nullptr)
    this->main_light_->publish_from_coordinator(
        this->master_.value_or(false) && this->main_light_power_.value_or(false), this->main_light_brightness_,
        this->main_light_color_temperature_);
}

void DreoCeilingFan::publish_ambient_() {
  if (this->ambient_light_ == nullptr || !this->master_.has_value() || !this->ambient_power_.has_value())
    return;
  this->publishing_ambient_ = true;
  auto call = this->ambient_light_->make_call();
  call.set_state(*this->master_ && *this->ambient_power_).set_transition_length(0).set_save(false).perform();
  this->publishing_ambient_ = false;
  this->snapshot_ambient_();
  // A separate call: ESPHome refuses an effect combined with a transition.
  this->restore_local_effect_();
}

void DreoCeilingFan::on_light_remote_values_update() {
  if (this->ambient_light_ == nullptr)
    return;
  // Anything this component published itself is adopted as the new baseline
  // without being mistaken for a user action.
  if (this->publishing_ambient_ || this->applying_preset_) {
    this->snapshot_ambient_();
    return;
  }
  if (!this->master_.has_value() || !this->ambient_power_.has_value())
    return;

  // An explicit turn-off stops the running effect in ESPHome; that is power,
  // not a presentation change, and must not forget the remembered effect.
  const bool presentation_changed =
      this->ambient_light_->remote_values.is_on() && this->ambient_presentation_changed_();
  const uint32_t effect_index = this->ambient_light_->get_current_effect_index();
  this->snapshot_ambient_();

  if (presentation_changed) {
    this->local_effect_ = this->ambient_light_->get_effect_name_by_index(effect_index);
    // The remote's cursor no longer describes what is showing. Tell the MCU
    // with dp28, as the stock application did, but remember the cursor it
    // currently reports as already applied: the MCU does not reset dp25 on
    // dp28 (measured 2026-09-03), and its reply to this very write repeats
    // the old value, which must not re-select the preset the user just left.
    // A later remote press changes dp25 and still applies its preset.
    this->applied_preset_cursor_ = this->preset_cursor_.value_or(0);
    this->send_predefine_sentinel_();
  }
  this->control_ambient(this->ambient_light_->remote_values.is_on());
}

}  // namespace esphome::dreo_ceiling_fan
