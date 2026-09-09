#pragma once

#include "../dreo_hpf007s.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome::dreo_hpf007s {

// The detected target's zone (`dp24`) while the fan is on. Cleared at
// power-off for the same reason as the presence sensor; a standard text
// sensor has no invalidation route, so the missing state is sent through the
// controller registry directly.
class DreoHpf007sZone final : public text_sensor::TextSensor, public Component {
 public:
  explicit DreoHpf007sZone(DreoHpf007s *parent) : parent_(parent) { parent->set_zone(this); }
  void dump_config() override;
  void publish_from_coordinator(const std::string &value) { this->publish_state(value); }
  void invalidate_from_coordinator();

 protected:
  DreoHpf007s *parent_;
};

}  // namespace esphome::dreo_hpf007s
