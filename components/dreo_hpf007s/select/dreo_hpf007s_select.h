#pragma once

#include <vector>

#include "../dreo_hpf007s.h"
#include "esphome/components/select/select.h"
#include "esphome/core/component.h"

namespace esphome::dreo_hpf007s {

enum class SelectRole : uint8_t { SWEEP, SENSOR_LIGHT_GRADIENT };

// A sweep-width setting for one axis (options map to degrees), or the
// sensor-light gradient (options are the coordinator's configured pairs).
class DreoHpf007sSelect final : public select::Select, public Component {
 public:
  DreoHpf007sSelect(DreoHpf007s *parent, Axis axis) : parent_(parent), role_(SelectRole::SWEEP), axis_(axis) {
    parent->set_sweep_select(axis, this);
  }
  explicit DreoHpf007sSelect(DreoHpf007s *parent)
      : parent_(parent), role_(SelectRole::SENSOR_LIGHT_GRADIENT), axis_(Axis::HORIZONTAL) {
    parent->set_gradient_select(this);
  }
  void dump_config() override;
  // Sweep selects: option index N carries degree value mappings[N].
  void set_degree_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }
  void publish_value_from_coordinator(uint8_t degrees);
  void publish_index_from_coordinator(size_t index) { this->publish_state(index); }

 protected:
  void control(size_t index) override;
  DreoHpf007s *parent_;
  SelectRole role_;
  Axis axis_;
  std::vector<uint8_t> mappings_{};
};

}  // namespace esphome::dreo_hpf007s
