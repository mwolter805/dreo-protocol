#include "dreo_text.h"

#include "esphome/core/log.h"

namespace esphome::dreo {

static const char *const TAG = "dreo.text";

void DreoText::setup() {
  this->parent_->register_listener(this->text_id_, [this](const DreoDatapoint &datapoint) {
    if (datapoint.type != DreoDatapointType::STRING) {
      ESP_LOGW(TAG, "Datapoint %u reported type %u instead of string", this->text_id_,
               static_cast<uint8_t>(datapoint.type));
      return;
    }
    ESP_LOGV(TAG, "MCU reported text %u (len: %zu)", this->text_id_, datapoint.value_string.size());
    this->publish_state(datapoint.value_string);
  });
}

void DreoText::control(const std::string &value) {
  if (this->validator_ && !this->validator_(value)) {
    ESP_LOGW(TAG, "Rejected invalid value for text datapoint %u", this->text_id_);
    return;
  }
  this->parent_->set_string_datapoint_value(this->text_id_, value);
}

void DreoText::dump_config() {
  LOG_TEXT("", "Dreo Text", this);
  ESP_LOGCONFIG(TAG, "  Text has datapoint ID %u", this->text_id_);
}

}  // namespace esphome::dreo
