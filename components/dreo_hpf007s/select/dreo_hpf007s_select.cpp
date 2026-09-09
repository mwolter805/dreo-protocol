#include "dreo_hpf007s_select.h"

#include <algorithm>

#include "esphome/core/log.h"

namespace esphome::dreo_hpf007s {

static const char *const TAG = "dreo_hpf007s.select";

void DreoHpf007sSelect::control(size_t index) {
  if (this->role_ == SelectRole::SENSOR_LIGHT_GRADIENT) {
    this->parent_->control_gradient(index);
    return;
  }
  if (index < this->mappings_.size())
    this->parent_->control_sweep(this->axis_, this->mappings_[index]);
}

void DreoHpf007sSelect::publish_value_from_coordinator(uint8_t degrees) {
  auto it = std::find(this->mappings_.begin(), this->mappings_.end(), degrees);
  if (it == this->mappings_.end()) {
    ESP_LOGD(TAG, "Sweep value %u matches no configured option", degrees);
    return;
  }
  this->publish_state(static_cast<size_t>(std::distance(this->mappings_.begin(), it)));
}

void DreoHpf007sSelect::dump_config() {
  LOG_SELECT("", "Dreo DR-HPF007S select", this);
  if (this->role_ == SelectRole::SENSOR_LIGHT_GRADIENT) {
    ESP_LOGCONFIG(TAG, "  Sensor light gradient");
  } else {
    ESP_LOGCONFIG(TAG, "  Sweep axis: %s", this->axis_ == Axis::HORIZONTAL ? "horizontal" : "vertical");
  }
}

}  // namespace esphome::dreo_hpf007s
