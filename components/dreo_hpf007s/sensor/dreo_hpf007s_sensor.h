#pragma once

#include "../dreo_hpf007s.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome::dreo_hpf007s {

// The measured head position of one axis, straight from each `dp8` report.
class DreoHpf007sSensor final : public sensor::Sensor, public Component {
 public:
  DreoHpf007sSensor(DreoHpf007s *parent, Axis axis) : parent_(parent), axis_(axis) {
    parent->set_position_sensor(axis, this);
  }
  void dump_config() override;
  void invalidate_from_coordinator();
  void publish_from_coordinator(float value) { this->publish_state(value); }

 protected:
  DreoHpf007s *parent_;
  Axis axis_;
};

}  // namespace esphome::dreo_hpf007s
