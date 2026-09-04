#include "dreo_lock.h"

#include "esphome/core/log.h"

namespace esphome::dreo {

static const char *const TAG = "dreo.lock";

void DreoLock::setup() {
  this->parent_->register_listener(this->lock_id_, [this](const DreoDatapoint &datapoint) {
    if (datapoint.type != DreoDatapointType::BOOLEAN) {
      ESP_LOGW(TAG, "Datapoint %u reported type %u instead of boolean", this->lock_id_,
               static_cast<uint8_t>(datapoint.type));
      return;
    }
    this->publish_state(datapoint.value_bool ? lock::LOCK_STATE_LOCKED : lock::LOCK_STATE_UNLOCKED);
  });
}

void DreoLock::control(const lock::LockCall &call) {
  auto state = call.get_state();
  if (!state.has_value())
    return;
  if (*state == lock::LOCK_STATE_LOCKED) {
    this->parent_->set_boolean_datapoint_value(this->lock_id_, true);
  } else if (*state == lock::LOCK_STATE_UNLOCKED) {
    this->parent_->set_boolean_datapoint_value(this->lock_id_, false);
  }
}

void DreoLock::dump_config() {
  LOG_LOCK("", "Dreo Lock", this);
  ESP_LOGCONFIG(TAG, "  Lock has datapoint ID %u", this->lock_id_);
}

}  // namespace esphome::dreo
