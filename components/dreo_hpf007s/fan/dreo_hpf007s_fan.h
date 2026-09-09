#pragma once

#include "../dreo_hpf007s.h"
#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"

namespace esphome::dreo_hpf007s {

class DreoHpf007sFan final : public Component, public fan::Fan {
 public:
  explicit DreoHpf007sFan(DreoHpf007s *parent) : parent_(parent) { parent->set_fan(this); }
  void setup() override;
  void dump_config() override;
  fan::FanTraits get_traits() override;
  void publish_from_coordinator(bool state, optional<uint8_t> speed, optional<uint8_t> mode);

 protected:
  void control(const fan::FanCall &call) override;
  DreoHpf007s *parent_;
};

}  // namespace esphome::dreo_hpf007s
