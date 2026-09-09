#include "dreo_hpf007s_switch.h"

#include "esphome/core/log.h"

namespace esphome::dreo_hpf007s {

static const char *const TAG = "dreo_hpf007s.switch";

void DreoHpf007sAxisSwitch::dump_config() {
  LOG_SWITCH("", "Dreo DR-HPF007S oscillation axis", this);
  ESP_LOGCONFIG(TAG, "  Axis: %s", this->axis_ == Axis::HORIZONTAL ? "horizontal" : "vertical");
}

}  // namespace esphome::dreo_hpf007s
