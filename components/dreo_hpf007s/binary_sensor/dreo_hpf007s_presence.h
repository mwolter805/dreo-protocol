#pragma once

#include "../dreo_hpf007s.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"

namespace esphome::dreo_hpf007s {

// Presence as the MCU reports it while the fan is on. The MCU stops updating
// `dp23` while the fan is off and leaves the last value in place, so the
// coordinator clears this sensor's state at power-off; ESPHome's
// invalidation notifies the API of the missing state.
class DreoHpf007sPresence final : public binary_sensor::BinarySensor, public Component {
 public:
  explicit DreoHpf007sPresence(DreoHpf007s *parent) : parent_(parent) { parent->set_presence(this); }
  void dump_config() override;
  void publish_from_coordinator(bool state) { this->publish_state(state); }
  void invalidate_from_coordinator() { this->invalidate_state(); }

 protected:
  DreoHpf007s *parent_;
};

}  // namespace esphome::dreo_hpf007s
