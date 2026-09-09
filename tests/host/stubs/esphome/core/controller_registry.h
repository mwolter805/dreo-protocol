#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {

class ControllerRegistry {
 public:
  static void notify_text_sensor_update(text_sensor::TextSensor *sensor) {
    notify_count++;
    last_missing_state = !sensor->has_state();
  }

  static void notify_sensor_update(sensor::Sensor *sensor) {
    sensor_notify_count++;
    sensor_missing_state = !sensor->has_state();
  }
  inline static size_t sensor_notify_count{0};
  inline static bool sensor_missing_state{false};
  inline static size_t notify_count{0};
  inline static bool last_missing_state{false};
};

}  // namespace esphome
