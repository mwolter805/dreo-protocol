#include "dreo_hpf007s_presence.h"

#include "esphome/core/log.h"

namespace esphome::dreo_hpf007s {

static const char *const TAG = "dreo_hpf007s.binary_sensor";

void DreoHpf007sPresence::dump_config() { LOG_BINARY_SENSOR("", "Dreo DR-HPF007S presence", this); }

}  // namespace esphome::dreo_hpf007s
