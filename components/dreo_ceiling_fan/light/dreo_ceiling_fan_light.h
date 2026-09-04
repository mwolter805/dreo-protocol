#pragma once

#include "../dreo_ceiling_fan.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"

namespace esphome::dreo_ceiling_fan {

class DreoCeilingFanLight final : public Component, public light::LightOutput {
 public:
  explicit DreoCeilingFanLight(DreoCeilingFan *parent) : parent_(parent) { parent->set_main_light(this); }
  void dump_config() override;
  void setup_state(light::LightState *state) override { this->state_ = state; }
  void update_state(light::LightState *) override { this->suppress_scheduled_write_ = this->publishing_from_mcu_; }
  void write_state(light::LightState *state) override;
  light::LightTraits get_traits() override;
  void set_warm_white_color_temperature(float mireds) { this->warm_white_kelvin_ = 1000000.0f / mireds; }
  void set_cold_white_color_temperature(float mireds) { this->cold_white_kelvin_ = 1000000.0f / mireds; }
  void publish_from_coordinator(bool state, optional<uint8_t> brightness, optional<uint8_t> color_temperature);

 protected:
  uint8_t color_temperature_to_wire_(float mireds) const;
  float wire_to_color_temperature_(uint8_t value) const;
  DreoCeilingFan *parent_;
  light::LightState *state_{nullptr};
  float warm_white_kelvin_{2700.0f};
  float cold_white_kelvin_{6500.0f};
  bool publishing_from_mcu_{false};
  bool suppress_scheduled_write_{false};
};

}  // namespace esphome::dreo_ceiling_fan
