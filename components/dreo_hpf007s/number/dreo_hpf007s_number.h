#pragma once

#include "../dreo_hpf007s.h"
#include "esphome/components/number/number.h"
#include "esphome/core/component.h"

namespace esphome::dreo_hpf007s {

enum class NumberRole : uint8_t { POSITION, CURVE_BLOCK };

// A head-position target for one axis, or one block of the Custom curve.
class DreoHpf007sNumber final : public number::Number, public Component {
 public:
  DreoHpf007sNumber(DreoHpf007s *parent, Axis axis) : parent_(parent), role_(NumberRole::POSITION), axis_(axis) {
    parent->set_position_number(axis, this);
  }
  DreoHpf007sNumber(DreoHpf007s *parent, uint8_t block)
      : parent_(parent), role_(NumberRole::CURVE_BLOCK), axis_(Axis::HORIZONTAL), block_(block) {
    parent->set_curve_number(block, this);
  }
  void dump_config() override;
  void publish_from_coordinator(float value) { this->publish_state(value); }

 protected:
  void control(float value) override;
  DreoHpf007s *parent_;
  NumberRole role_;
  Axis axis_;
  uint8_t block_{0};
};

}  // namespace esphome::dreo_hpf007s
