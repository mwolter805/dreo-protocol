#include "dreo_text_sensor.h"

#include "esphome/core/controller_registry.h"
#include "esphome/core/log.h"

namespace esphome::dreo {

static const char *const TAG = "dreo.text_sensor";

void DreoTextSensor::setup() {
  this->parent_->register_listener(this->text_id_, [this](const DreoDatapoint &datapoint) {
    if (datapoint.type != DreoDatapointType::STRING) {
      ESP_LOGW(TAG, "Datapoint %u reported type %u instead of string", this->text_id_,
               static_cast<uint8_t>(datapoint.type));
      return;
    }
    this->publish_state(datapoint.value_string);
  });
}

void DreoTextSensor::invalidate_state() {
  this->set_has_state(false);
#if defined(USE_TEXT_SENSOR) && defined(USE_CONTROLLER_REGISTRY)
  ControllerRegistry::notify_text_sensor_update(this);
#endif
}

void DreoTextSensor::dump_config() {
  LOG_TEXT_SENSOR("", "Dreo Text Sensor", this);
  ESP_LOGCONFIG(TAG, "  Text sensor has datapoint ID %u", this->text_id_);
}

}  // namespace esphome::dreo
