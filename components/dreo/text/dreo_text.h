#pragma once

#include <functional>
#include <string>

#include "esphome/components/dreo/dreo.h"
#include "esphome/components/text/text.h"
#include "esphome/core/component.h"

namespace esphome::dreo {

class DreoText final : public text::Text, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void set_dreo_parent(Dreo *parent) { this->parent_ = parent; }
  void set_text_id(uint8_t text_id) { this->text_id_ = text_id; }
  void set_validator(const std::function<bool(const std::string &)> &validator) { this->validator_ = validator; }

 protected:
  void control(const std::string &value) override;

  Dreo *parent_{nullptr};
  uint8_t text_id_{0};
  std::function<bool(const std::string &)> validator_{};
};

}  // namespace esphome::dreo
