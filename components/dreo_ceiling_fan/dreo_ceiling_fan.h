#pragma once

#include <string>
#include <vector>

#include "../dreo/dreo.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"
#include "esphome/core/optional.h"

namespace esphome::dreo_ceiling_fan {

// The remote's RGB button cycles four presets. The stock application also
// reports the count back to the MCU, which is what `dp26` carries.
static constexpr uint8_t AMBIENT_PRESET_COUNT = 4;

class DreoCeilingFanFan;
class DreoCeilingFanLight;

// Coordinates a Dreo ceiling fan's state machine over the shared `dreo` hub:
// the master power gate over the fan, main light and ambient light, fan mode
// and speed, the colour-temperature main light, and the remote's ambient
// preset cursor. First built and verified on the DR-HCF010S; the datapoint
// map it assumes is that product's.
class DreoCeilingFan final : public Component, public light::LightRemoteValuesListener {
 public:
  explicit DreoCeilingFan(dreo::Dreo *parent) : parent_(parent) {}

  void setup() override;
  void dump_config() override;
  void on_light_remote_values_update() override;

  void set_fan(DreoCeilingFanFan *fan) { this->fan_ = fan; }
  void set_main_light(DreoCeilingFanLight *light) { this->main_light_ = light; }
  void set_ambient_light(light::LightState *light);
  // Preset effect names in remote-cursor order, so `dp25` value N selects the
  // Nth registered name. Exactly AMBIENT_PRESET_COUNT names are configured.
  void add_ambient_preset_effect(const std::string &name) { this->ambient_preset_effects_.push_back(name); }
  bool authorize_command(const dreo::DreoDatapointCommand &command) const;

  void control_fan(bool turn_on, bool turn_off, optional<uint8_t> speed, optional<uint8_t> mode);
  void control_main_light(bool turn_on, bool turn_off, optional<uint8_t> brightness,
                          optional<uint8_t> color_temperature);
  void control_ambient(bool state);

 protected:
  void handle_datapoint_(const dreo::DreoDatapoint &datapoint);
  void request_feature_state_(uint8_t child, bool state);
  void flush_fan_settings_();
  void flush_main_light_settings_();
  void publish_fan_();
  void publish_main_light_();
  void publish_ambient_();
  bool parents_ready_(uint8_t child) const;
  bool setting_allowed_(uint8_t child) const;
  bool ambient_effective_() const;
  void apply_preset_cursor_();
  void maintain_preset_count_();
  void send_predefine_sentinel_();
  void restore_local_effect_();
  void snapshot_ambient_();
  bool ambient_presentation_changed_() const;

  dreo::Dreo *parent_;
  DreoCeilingFanFan *fan_{nullptr};
  DreoCeilingFanLight *main_light_{nullptr};
  light::LightState *ambient_light_{nullptr};

  optional<bool> master_{};
  optional<bool> fan_power_{};
  optional<bool> main_light_power_{};
  optional<bool> ambient_power_{};
  optional<uint8_t> fan_mode_{};
  optional<uint8_t> fan_speed_{};
  optional<uint8_t> main_light_brightness_{};
  optional<uint8_t> main_light_color_temperature_{};

  optional<uint8_t> queued_fan_speed_{};
  optional<uint8_t> queued_fan_mode_{};
  optional<uint8_t> queued_main_light_brightness_{};
  optional<uint8_t> queued_main_light_color_temperature_{};
  bool publishing_ambient_{false};

  // Ambient preset handling. `dp25` is the remote's cursor, `dp26` the preset
  // count the MCU is told about, and `dp27`/`dp28` are the brightness (kept
  // local, exactly as the stock application does) and the sentinel that tells
  // the MCU an app-defined presentation is now in effect.
  std::vector<std::string> ambient_preset_effects_{};
  optional<uint8_t> preset_cursor_{};
  optional<uint8_t> preset_count_{};
  uint8_t applied_preset_cursor_{0};
  bool preset_count_written_{false};
  bool applying_preset_{false};
  std::string local_effect_{};

  // Last observed ambient presentation, used to tell a local change from one
  // this component published itself.
  bool ambient_snapshot_valid_{false};
  bool ambient_snapshot_state_{false};
  uint32_t ambient_snapshot_effect_{0};
  float ambient_snapshot_brightness_{0.0f};
  float ambient_snapshot_color_brightness_{0.0f};
  float ambient_snapshot_red_{0.0f};
  float ambient_snapshot_green_{0.0f};
  float ambient_snapshot_blue_{0.0f};
};

}  // namespace esphome::dreo_ceiling_fan
