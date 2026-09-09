#include "dreo_hpf007s_sensor.h"

#include "esphome/core/log.h"
#include "esphome/core/controller_registry.h"

namespace esphome::dreo_hpf007s {

static const char *const TAG = "dreo_hpf007s.sensor";

void DreoHpf007sSensor::invalidate_from_coordinator() {
  this->set_has_state(false);
#if defined(USE_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_sensor_update(this);
#endif
}

void DreoHpf007sSensor::dump_config() {
  LOG_SENSOR("", "Dreo DR-HPF007S head position", this);
  ESP_LOGCONFIG(TAG, "  Axis: %s", this->axis_ == Axis::HORIZONTAL ? "horizontal" : "vertical");
}

}  // namespace esphome::dreo_hpf007s
