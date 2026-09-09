#include "dreo_hpf007s_zone.h"

#include "esphome/core/controller_registry.h"
#include "esphome/core/log.h"

namespace esphome::dreo_hpf007s {

static const char *const TAG = "dreo_hpf007s.text_sensor";

void DreoHpf007sZone::invalidate_from_coordinator() {
  this->set_has_state(false);
#if defined(USE_TEXT_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_text_sensor_update(this);
#endif
}

void DreoHpf007sZone::dump_config() { LOG_TEXT_SENSOR("", "Dreo DR-HPF007S presence zone", this); }

}  // namespace esphome::dreo_hpf007s
