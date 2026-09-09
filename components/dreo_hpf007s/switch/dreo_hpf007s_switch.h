#pragma once

#include "../dreo_hpf007s.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

namespace esphome::dreo_hpf007s {

// One oscillation axis. Both axes share `dp5`, so the coordinator composes the
// combined value and the switch only follows reports.
class DreoHpf007sAxisSwitch final : public switch_::Switch, public Component {
 public:
  DreoHpf007sAxisSwitch(DreoHpf007s *parent, Axis axis) : parent_(parent), axis_(axis) {
    parent->set_axis_switch(axis, this);
  }
  void dump_config() override;
  void publish_from_coordinator(bool state) { this->publish_state(state); }

 protected:
  void write_state(bool state) override { this->parent_->control_axis(this->axis_, state); }
  DreoHpf007s *parent_;
  Axis axis_;
};

}  // namespace esphome::dreo_hpf007s
