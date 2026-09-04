#pragma once

#include <string>
#include <vector>

#include "esphome/components/dreo/dreo.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"

namespace esphome::dreo {

class DreoLight;

class DreoLightEffect final : public light::LightEffect {
 public:
  DreoLightEffect(const char *name, uint32_t value) : light::LightEffect(name), value_(value) {}
  void start() override;
  void apply() override {}

 protected:
  uint32_t value_;
};

struct DreoBrightnessMapping {
  uint32_t value;
  float brightness;
};

struct DreoEffectMapping {
  uint32_t value;
  std::string name;
};

class DreoLight final : public Component, public light::LightOutput {
 public:
  explicit DreoLight(Dreo *parent) : parent_(parent) {}
  void setup() override;
  void dump_config() override;
  void setup_state(light::LightState *state) override;
  void update_state(light::LightState *state) override;
  void write_state(light::LightState *state) override;
  light::LightTraits get_traits() override;

  void set_switch_id(uint8_t id) { this->switch_id_ = id; }
  void set_brightness_id(uint8_t id) { this->brightness_id_ = id; }
  void set_effect_id(uint8_t id) { this->effect_id_ = id; }
  void set_rgb_id(uint8_t id) { this->rgb_id_ = id; }
  void set_constant_effect(uint32_t value) { this->constant_effect_ = value; }
  void add_brightness_mapping(uint32_t value, float brightness) {
    this->brightness_mappings_.push_back({value, brightness});
  }
  void add_effect_mapping(uint32_t value, const std::string &name) {
    this->effect_mappings_.push_back({value, name});
  }
  void select_effect(uint32_t value);

 protected:
  void publish_mcu_call_(light::LightCall &call, bool includes_effect = false);
  void restore_visible_state_();
  bool send_numeric_(uint8_t id, const optional<DreoDatapointType> &type, uint32_t value);
  optional<float> brightness_for_value_(uint32_t value) const;
  optional<uint32_t> value_for_brightness_(float brightness) const;
  const char *effect_name_for_value_(uint32_t value) const;
  uint32_t rgb_from_state_(light::LightState *state) const;

  Dreo *parent_;
  light::LightState *state_{nullptr};
  bool publishing_from_mcu_{false};
  bool suppress_scheduled_write_{false};
  optional<uint32_t> requested_effect_{};
  optional<uint8_t> switch_id_{};
  optional<uint8_t> brightness_id_{};
  optional<uint8_t> effect_id_{};
  optional<uint8_t> rgb_id_{};
  optional<uint32_t> constant_effect_{};
  optional<DreoDatapointType> brightness_type_{};
  optional<DreoDatapointType> effect_type_{};
  optional<DreoDatapointType> rgb_type_{};
  optional<bool> wire_state_{};
  optional<uint32_t> wire_brightness_{};
  optional<uint32_t> wire_effect_{};
  optional<uint32_t> wire_rgb_{};
  std::vector<DreoBrightnessMapping> brightness_mappings_{};
  std::vector<DreoEffectMapping> effect_mappings_{};
};

}  // namespace esphome::dreo
