#pragma once

#include "../dreo_ceiling_fan.h"
#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"

namespace esphome::dreo_ceiling_fan {

class DreoCeilingFanFan final : public Component, public fan::Fan {
 public:
  explicit DreoCeilingFanFan(DreoCeilingFan *parent) : parent_(parent) { parent->set_fan(this); }
  void setup() override;
  void dump_config() override;
  fan::FanTraits get_traits() override;
  void publish_from_coordinator(bool state, optional<uint8_t> speed, optional<uint8_t> mode);

 protected:
  void control(const fan::FanCall &call) override;
  DreoCeilingFan *parent_;
};

}  // namespace esphome::dreo_ceiling_fan
