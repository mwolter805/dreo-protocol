#include "dreo_hpf007s_number.h"

#include <cmath>

#include "esphome/core/log.h"

namespace esphome::dreo_hpf007s {

static const char *const TAG = "dreo_hpf007s.number";

void DreoHpf007sNumber::control(float value) {
  const long rounded = std::lround(value);
  if (this->role_ == NumberRole::POSITION) {
    this->parent_->control_position(this->axis_, static_cast<int16_t>(rounded));
  } else if (rounded >= SPEED_MIN && rounded <= SPEED_MAX) {
    // The curve is only ever confirmed by the MCU's report; no optimistic
    // publication here.
    this->parent_->control_curve_block(this->block_, static_cast<uint8_t>(rounded));
  }
}

void DreoHpf007sNumber::dump_config() {
  LOG_NUMBER("", "Dreo DR-HPF007S number", this);
  if (this->role_ == NumberRole::POSITION) {
    ESP_LOGCONFIG(TAG, "  Head position target: %s", this->axis_ == Axis::HORIZONTAL ? "horizontal" : "vertical");
  } else {
    ESP_LOGCONFIG(TAG, "  Custom curve block: %u", this->block_);
  }
}

}  // namespace esphome::dreo_hpf007s
