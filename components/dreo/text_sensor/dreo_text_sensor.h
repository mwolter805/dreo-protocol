#pragma once

#include "esphome/components/dreo/dreo.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome::dreo {

class DreoTextSensor final : public text_sensor::TextSensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void set_dreo_parent(Dreo *parent) { this->parent_ = parent; }
  void set_text_id(uint8_t text_id) { this->text_id_ = text_id; }
  void invalidate_state();

 protected:
  Dreo *parent_{nullptr};
  uint8_t text_id_{0};
};

}  // namespace esphome::dreo
