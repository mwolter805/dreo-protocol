#pragma once

#include "esphome/components/dreo/dreo.h"
#include "esphome/components/lock/lock.h"
#include "esphome/core/component.h"

namespace esphome::dreo {

class DreoLock final : public lock::Lock, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void set_dreo_parent(Dreo *parent) { this->parent_ = parent; }
  void set_lock_id(uint8_t lock_id) { this->lock_id_ = lock_id; }

 protected:
  void control(const lock::LockCall &call) override;

  Dreo *parent_{nullptr};
  uint8_t lock_id_{0};
};

}  // namespace esphome::dreo
