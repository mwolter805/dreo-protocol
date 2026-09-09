#include "dreo_hpf007s.h"

#include <algorithm>
#include <cstdlib>

#include "binary_sensor/dreo_hpf007s_presence.h"
#include "fan/dreo_hpf007s_fan.h"
#include "number/dreo_hpf007s_number.h"
#include "select/dreo_hpf007s_select.h"
#include "sensor/dreo_hpf007s_sensor.h"
#include "switch/dreo_hpf007s_switch.h"
#include "text_sensor/dreo_hpf007s_zone.h"
#include "esphome/core/log.h"

namespace esphome::dreo_hpf007s {

static const char *const TAG = "dreo_hpf007s";
static const char *const CURVE_PREFIX = "temp:";
static constexpr size_t CURVE_PREFIX_LENGTH = 5;
static constexpr int16_t VERTICAL_LOWER_DEFAULT = -30;

static size_t axis_index_(Axis axis) { return axis == Axis::HORIZONTAL ? 0 : 1; }

void DreoHpf007s::setup() {
  // Wi-Fi counts as lost from setup until the wrapper reports a connection,
  // so a boot that never joins a network flashes after the same debounce.
  this->wifi_lost_at_ = millis();
  for (uint8_t id : {DP_POWER, DP_MODE, DP_SPEED, DP_AXES, DP_CUSTOM_CURVE, DP_SWEEP, DP_POSITION, DP_PRESENCE,
                     DP_PRESENCE_ZONE, DP_LIGHT_CENTRE, DP_LIGHT_EDGE}) {
    this->parent_->register_listener(id, [this](const dreo::DreoDatapoint &datapoint) {
      this->handle_datapoint_(datapoint);
    });
  }
  this->parent_->add_on_report_callback([this](const std::vector<uint8_t> &ids) { this->handle_report_(ids); });
  // The indicator policy is re-derived after every hub initialization, so a
  // module reset that restarts the session repeats the current state once.
  this->parent_->add_on_initialized_callback([this]() {
    this->indication_sent_ = ConnectionIndication::UNKNOWN;
    this->evaluate_connection_indication_();
  });
  this->parent_->add_on_module_reset_request_callback([this]() {
    this->indication_sent_ = ConnectionIndication::UNKNOWN;
  });
}

void DreoHpf007s::handle_report_(const std::vector<uint8_t> &ids) {
  const auto contains = [&ids](uint8_t id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
  if (!contains(DP_POWER) || !contains(DP_MODE) || contains(DP_POSITION))
    return;
  // Full calibration reports omit the position while the head is homing.
  // Targets remain valid requests; only measurements become unknown.
  for (size_t i = 0; i < 2; i++) {
    this->position_measured_[i].reset();
    if (this->position_sensors_[i] != nullptr)
      this->position_sensors_[i]->invalidate_from_coordinator();
  }
}

void DreoHpf007s::loop() { this->evaluate_connection_indication_(); }

void DreoHpf007s::dump_config() {
  ESP_LOGCONFIG(TAG, "Dreo DR-HPF007S coordinator");
  ESP_LOGCONFIG(TAG, "  Sensor light gradients: %u", static_cast<unsigned>(this->gradients_.size()));
}

void DreoHpf007s::set_axis_switch(Axis axis, DreoHpf007sAxisSwitch *entity) {
  this->axis_switches_[axis_index_(axis)] = entity;
}
void DreoHpf007s::set_position_number(Axis axis, DreoHpf007sNumber *entity) {
  this->position_numbers_[axis_index_(axis)] = entity;
}
void DreoHpf007s::set_position_sensor(Axis axis, DreoHpf007sSensor *entity) {
  this->position_sensors_[axis_index_(axis)] = entity;
}
void DreoHpf007s::set_curve_number(uint8_t block, DreoHpf007sNumber *entity) {
  if (block < CURVE_BLOCKS)
    this->curve_numbers_[block] = entity;
}
void DreoHpf007s::set_sweep_select(Axis axis, DreoHpf007sSelect *entity) {
  this->sweep_selects_[axis_index_(axis)] = entity;
}

// ---------------------------------------------------------------------------
// Parsing

bool DreoHpf007s::parse_int_(const std::string &text, size_t begin, size_t end, int16_t &value) {
  if (begin >= end || end > text.size())
    return false;
  size_t i = begin;
  bool negative = false;
  if (text[i] == '-') {
    negative = true;
    i++;
  }
  if (i >= end)
    return false;
  int32_t magnitude = 0;
  for (; i < end; i++) {
    const char c = text[i];
    if (c < '0' || c > '9')
      return false;
    magnitude = magnitude * 10 + (c - '0');
    if (magnitude > 999)
      return false;
  }
  value = static_cast<int16_t>(negative ? -magnitude : magnitude);
  return true;
}

bool DreoHpf007s::parse_curve(const std::string &value, std::array<uint8_t, CURVE_ENTRIES> &digits) {
  if (value.size() != CURVE_PREFIX_LENGTH + CURVE_ENTRIES || value.compare(0, CURVE_PREFIX_LENGTH, CURVE_PREFIX) != 0)
    return false;
  std::array<uint8_t, CURVE_ENTRIES> parsed{};
  for (size_t i = 0; i < CURVE_ENTRIES; i++) {
    const char c = value[CURVE_PREFIX_LENGTH + i];
    if (c < '0' + SPEED_MIN || c > '0' + SPEED_MAX)
      return false;
    parsed[i] = static_cast<uint8_t>(c - '0');
  }
  digits = parsed;
  return true;
}

bool DreoHpf007s::parse_sweep(const std::string &value, std::array<int16_t, 4> &fields) {
  std::array<int16_t, 4> parsed{};
  size_t begin = 0;
  for (size_t field = 0; field < 4; field++) {
    const size_t comma = value.find(',', begin);
    const size_t end = comma == std::string::npos ? value.size() : comma;
    if (field < 3 && comma == std::string::npos)
      return false;
    if (field == 3 && comma != std::string::npos)
      return false;
    if (!parse_int_(value, begin, end, parsed[field]))
      return false;
    begin = end + 1;
  }
  fields = parsed;
  return true;
}

bool DreoHpf007s::parse_position(const std::string &value, optional<int16_t> &vertical,
                                 optional<int16_t> &horizontal) {
  const size_t comma = value.find(',');
  if (comma == std::string::npos || value.find(',', comma + 1) != std::string::npos)
    return false;
  optional<int16_t> parsed_vertical{};
  optional<int16_t> parsed_horizontal{};
  if (comma > 0) {
    int16_t degrees;
    if (!parse_int_(value, 0, comma, degrees))
      return false;
    parsed_vertical = degrees;
  }
  if (comma + 1 < value.size()) {
    int16_t degrees;
    if (!parse_int_(value, comma + 1, value.size(), degrees))
      return false;
    parsed_horizontal = degrees;
  }
  vertical = parsed_vertical;
  horizontal = parsed_horizontal;
  return true;
}

// ---------------------------------------------------------------------------
// Reports

void DreoHpf007s::handle_datapoint_(const dreo::DreoDatapoint &datapoint) {
  using dreo::DreoDatapointType;
  switch (datapoint.id) {
    case DP_POWER:
      if (datapoint.type == DreoDatapointType::BOOLEAN)
        this->handle_power_(datapoint.value_bool);
      break;
    case DP_MODE:
      if (datapoint.type == DreoDatapointType::ENUM) {
        this->mode_ = datapoint.value_enum;
        this->publish_fan_();
      }
      break;
    case DP_SPEED:
      if (datapoint.type == DreoDatapointType::ENUM) {
        this->speed_ = datapoint.value_enum;
        this->publish_fan_();
      }
      break;
    case DP_AXES:
      if (datapoint.type == DreoDatapointType::ENUM) {
        this->axes_ = datapoint.value_enum;
        if (!this->parent_->is_datapoint_pending(DP_AXES))
          this->axes_target_.reset();
        // A sweeping axis makes any outstanding position target moot.
        for (Axis axis : {Axis::HORIZONTAL, Axis::VERTICAL}) {
          if (datapoint.value_enum & axis_bit_(axis))
            this->position_pending_[axis_index_(axis)] = false;
        }
        this->publish_axes_();
        this->publish_sweeps_();
      }
      break;
    case DP_CUSTOM_CURVE:
      if (datapoint.type == DreoDatapointType::STRING)
        this->handle_curve_(datapoint.value_string);
      break;
    case DP_SWEEP:
      if (datapoint.type == DreoDatapointType::STRING)
        this->handle_sweep_(datapoint.value_string);
      break;
    case DP_POSITION:
      if (datapoint.type == DreoDatapointType::STRING)
        this->handle_position_(datapoint.value_string);
      break;
    case DP_PRESENCE:
      if (datapoint.type == DreoDatapointType::ENUM) {
        this->presence_value_ = datapoint.value_enum;
        this->publish_presence_();
      }
      break;
    case DP_PRESENCE_ZONE:
      if (datapoint.type == DreoDatapointType::STRING) {
        this->zone_value_ = datapoint.value_string;
        this->publish_presence_();
      }
      break;
    case DP_LIGHT_CENTRE:
      if (datapoint.type == DreoDatapointType::INTEGER) {
        this->light_centre_ = static_cast<uint32_t>(datapoint.value_int);
        this->publish_gradient_();
      }
      break;
    case DP_LIGHT_EDGE:
      if (datapoint.type == DreoDatapointType::INTEGER) {
        this->light_edge_ = static_cast<uint32_t>(datapoint.value_int);
        this->publish_gradient_();
      }
      break;
    default:
      break;
  }
}

void DreoHpf007s::handle_power_(bool value) {
  this->power_ = value;
  if (!value) {
    this->queued_speed_.reset();
    this->queued_mode_.reset();
    // The MCU stops reporting presence while the fan is off and leaves the
    // last value in place, so it must not be presented as current. The
    // retained values are dropped too: only a report received while the fan
    // is on may restore availability.
    this->presence_value_.reset();
    this->zone_value_.reset();
    this->invalidate_presence_();
  }
  this->flush_queued_fan_settings_();
  this->publish_fan_();
}

void DreoHpf007s::handle_curve_(const std::string &value) {
  std::array<uint8_t, CURVE_ENTRIES> digits{};
  if (!parse_curve(value, digits)) {
    ESP_LOGW(TAG, "Ignoring malformed Custom curve report (%u bytes)", static_cast<unsigned>(value.size()));
    return;
  }
  this->curve_ = digits;
  this->curve_valid_ = true;
  this->publish_curve_();
}

void DreoHpf007s::handle_sweep_(const std::string &value) {
  std::array<int16_t, 4> fields{};
  if (!parse_sweep(value, fields)) {
    ESP_LOGW(TAG, "Ignoring malformed sweep report (%u bytes)", static_cast<unsigned>(value.size()));
    return;
  }
  this->sweep_ = fields;
  this->sweep_valid_ = true;
  // An idle axis reverts its field to a resting value, so only an active axis
  // updates the retained setting; an unknown setting adopts whatever is
  // reported so the entity has a state.
  const uint8_t axes = this->axes_.value_or(0);
  const int16_t horizontal_width = fields[1] > 0 && fields[1] <= 127 ? static_cast<int16_t>(fields[1] * 2) : 0;
  const int16_t vertical_upper = fields[0] > 0 && fields[0] <= 255 ? fields[0] : 0;
  if (!this->sweep_setting_[0].has_value() || (axes & AXIS_HORIZONTAL_BIT))
    this->sweep_setting_[0] = static_cast<uint8_t>(horizontal_width);
  if (!this->sweep_setting_[1].has_value() || (axes & AXIS_VERTICAL_BIT))
    this->sweep_setting_[1] = static_cast<uint8_t>(vertical_upper);
  this->publish_sweeps_();
}

void DreoHpf007s::handle_position_(const std::string &value) {
  optional<int16_t> vertical{};
  optional<int16_t> horizontal{};
  if (!parse_position(value, vertical, horizontal)) {
    ESP_LOGW(TAG, "Ignoring malformed head position report (%u bytes)", static_cast<unsigned>(value.size()));
    return;
  }
  const optional<int16_t> reported[2] = {horizontal, vertical};
  for (size_t i = 0; i < 2; i++) {
    if (!reported[i].has_value())
      continue;
    this->position_measured_[i] = reported[i];
    if (this->position_sensors_[i] != nullptr)
      this->position_sensors_[i]->publish_from_coordinator(*reported[i]);
    // A commanded target is echoed at once, the old position is reported
    // next, and the target again once the head arrives. The target entity
    // keeps the request through that sequence: it is released by a matching
    // report that follows a differing one, or by the travel timeout. Afterwards
    // it follows the measured position so a panel or oscillation move shows.
    if (this->position_pending_[i]) {
      const bool matches = this->position_target_[i].has_value() && *this->position_target_[i] == *reported[i];
      if (!matches)
        this->position_saw_mismatch_[i] = true;
      const bool arrived = matches && this->position_saw_mismatch_[i];
      const bool timed_out = millis() - this->position_written_at_[i] >= POSITION_TRAVEL_TIMEOUT_MS;
      if (!arrived && !timed_out)
        continue;
      this->position_pending_[i] = false;
      if (arrived)
        continue;
    }
    this->position_target_[i] = reported[i];
    if (this->position_numbers_[i] != nullptr)
      this->position_numbers_[i]->publish_from_coordinator(*reported[i]);
  }
}

// ---------------------------------------------------------------------------
// Publication

void DreoHpf007s::publish_fan_() {
  if (this->fan_ != nullptr)
    this->fan_->publish_from_coordinator(this->power_.value_or(false), this->speed_, this->mode_);
}

void DreoHpf007s::publish_axes_() {
  if (!this->axes_.has_value())
    return;
  for (Axis axis : {Axis::HORIZONTAL, Axis::VERTICAL}) {
    auto *entity = this->axis_switches_[axis_index_(axis)];
    if (entity != nullptr)
      entity->publish_from_coordinator((*this->axes_ & axis_bit_(axis)) != 0);
  }
}

void DreoHpf007s::publish_sweeps_() {
  for (size_t i = 0; i < 2; i++) {
    if (this->sweep_selects_[i] != nullptr && this->sweep_setting_[i].has_value())
      this->sweep_selects_[i]->publish_value_from_coordinator(*this->sweep_setting_[i]);
  }
}

void DreoHpf007s::publish_curve_() {
  if (!this->curve_valid_)
    return;
  for (size_t block = 0; block < CURVE_BLOCKS; block++) {
    if (this->curve_numbers_[block] != nullptr)
      this->curve_numbers_[block]->publish_from_coordinator(this->curve_[CURVE_BLOCK_START[block]]);
  }
}

void DreoHpf007s::publish_gradient_() {
  if (this->gradient_select_ == nullptr || !this->light_centre_.has_value() || !this->light_edge_.has_value())
    return;
  for (size_t i = 0; i < this->gradients_.size(); i++) {
    if (this->gradients_[i].centre == *this->light_centre_ && this->gradients_[i].edge == *this->light_edge_) {
      this->gradient_select_->publish_index_from_coordinator(i);
      return;
    }
  }
  ESP_LOGD(TAG, "Sensor light colours match no configured gradient");
}

void DreoHpf007s::publish_presence_() {
  if (!this->power_.value_or(false))
    return;
  if (this->presence_ != nullptr && this->presence_value_.has_value())
    this->presence_->publish_from_coordinator(*this->presence_value_ != 0);
  if (this->zone_ != nullptr && this->zone_value_.has_value())
    this->zone_->publish_from_coordinator(*this->zone_value_);
}

void DreoHpf007s::invalidate_presence_() {
  if (this->presence_ != nullptr)
    this->presence_->invalidate_from_coordinator();
  if (this->zone_ != nullptr)
    this->zone_->invalidate_from_coordinator();
}

// ---------------------------------------------------------------------------
// Authorization

bool DreoHpf007s::power_confirmed_on() const {
  return this->power_.has_value() && *this->power_ && !this->parent_->is_datapoint_pending(DP_POWER);
}

bool DreoHpf007s::power_confirmed_off() const {
  return this->power_.has_value() && !*this->power_ && !this->parent_->is_datapoint_pending(DP_POWER);
}

// The stock application allows only the auto-on timer and the child lock while
// the fan is off; everything else waits for confirmed power. Auto Speed is
// additionally offered only in Normal mode.
bool DreoHpf007s::authorize_command(const dreo::DreoDatapointCommand &command) const {
  switch (command.datapoint_id) {
    case DP_POWER:
    case DP_AUTO_ON_TIMER:
    case DP_CHILD_LOCK:
      return true;
    case DP_AUTO_SPEED:
      return this->power_confirmed_on() && this->mode_.value_or(0) == MODE_NORMAL;
    default:
      return this->power_confirmed_on();
  }
}

// ---------------------------------------------------------------------------
// Controls

bool DreoHpf007s::write_mode_(uint8_t mode) {
  if (mode == MODE_CUSTOM && this->curve_valid_) {
    // The stock application activates Custom by sending the mode and the
    // curve in one frame.
    return this->parent_->set_datapoint_values({
        dreo::DreoDatapointCommand{
            .datapoint_id = DP_MODE, .type = dreo::DreoDatapointType::ENUM, .value_uint = MODE_CUSTOM, .length = 1},
        dreo::DreoDatapointCommand{
            .datapoint_id = DP_CUSTOM_CURVE, .type = dreo::DreoDatapointType::STRING, .value_string = this->curve_string()},
    });
  }
  return this->parent_->set_enum_datapoint_value(DP_MODE, mode);
}

void DreoHpf007s::control_fan(bool turn_on, bool turn_off, optional<uint8_t> speed, optional<uint8_t> mode) {
  if (turn_off) {
    this->queued_speed_.reset();
    this->queued_mode_.reset();
    this->parent_->set_boolean_datapoint_value(DP_POWER, false);
    return;
  }
  const bool on_target = this->parent_->get_boolean_datapoint_target(DP_POWER).value_or(false);
  if (!this->power_confirmed_on()) {
    // Settings wait for the MCU to confirm power; the stock application
    // offers none of them while the fan is off.
    if (speed.has_value())
      this->queued_speed_ = speed;
    if (mode.has_value())
      this->queued_mode_ = mode;
    if (turn_on && !on_target)
      this->parent_->set_boolean_datapoint_value(DP_POWER, true);
    return;
  }
  if (mode.has_value())
    this->write_mode_(*mode);
  if (speed.has_value() && *speed >= SPEED_MIN && *speed <= SPEED_MAX)
    this->parent_->set_enum_datapoint_value(DP_SPEED, *speed);
}

void DreoHpf007s::flush_queued_fan_settings_() {
  if (!this->power_confirmed_on())
    return;
  if (this->queued_mode_.has_value()) {
    const uint8_t mode = *this->queued_mode_;
    this->queued_mode_.reset();
    this->write_mode_(mode);
  }
  if (this->queued_speed_.has_value()) {
    const uint8_t speed = *this->queued_speed_;
    this->queued_speed_.reset();
    if (speed >= SPEED_MIN && speed <= SPEED_MAX)
      this->parent_->set_enum_datapoint_value(DP_SPEED, speed);
  }
}

uint8_t DreoHpf007s::axes_base_() const {
  if (this->axes_target_.has_value() && this->parent_->is_datapoint_pending(DP_AXES))
    return *this->axes_target_;
  return this->axes_.value_or(0);
}

bool DreoHpf007s::control_axis(Axis axis, bool state) {
  if (!this->axes_.has_value())
    return false;
  const uint8_t bit = axis_bit_(axis);
  const uint8_t base = this->axes_base_();
  const uint8_t combined = state ? static_cast<uint8_t>(base | bit) : static_cast<uint8_t>(base & ~bit);
  if (!this->parent_->set_enum_datapoint_value(DP_AXES, combined))
    return false;
  this->axes_target_ = combined;
  return true;
}

bool DreoHpf007s::write_position_(const std::string &value) {
  return this->parent_->set_string_datapoint_value(DP_POSITION, value);
}

bool DreoHpf007s::control_position(Axis axis, int16_t degrees) {
  const int16_t low = axis == Axis::VERTICAL ? VERTICAL_POSITION_MIN : HORIZONTAL_POSITION_MIN;
  const int16_t high = axis == Axis::VERTICAL ? VERTICAL_POSITION_MAX : HORIZONTAL_POSITION_MAX;
  if (degrees < low || degrees > high)
    return false;
  const std::string text = std::to_string(degrees);
  if (!this->write_position_(axis == Axis::VERTICAL ? text + "," : "," + text))
    return false;
  const size_t i = axis_index_(axis);
  this->position_target_[i] = degrees;
  this->position_pending_[i] = true;
  this->position_saw_mismatch_[i] = false;
  this->position_written_at_[i] = millis();
  if (this->position_numbers_[i] != nullptr)
    this->position_numbers_[i]->publish_from_coordinator(degrees);
  return true;
}

// The stock application recalibrates an axis by commanding "0" for it and
// leaving the other field empty; an entirely empty "," recalibrates both.
bool DreoHpf007s::calibrate(bool vertical, bool horizontal) {
  if (!vertical && !horizontal)
    return false;
  std::string value = ",";
  if (vertical && !horizontal)
    value = "0,";
  else if (horizontal && !vertical)
    value = ",0";
  return this->write_position_(value);
}

bool DreoHpf007s::control_sweep(Axis axis, uint8_t degrees) {
  if (degrees == 0)
    return false;
  std::array<int16_t, 4> fields = this->sweep_valid_ ? this->sweep_ : std::array<int16_t, 4>{0, 0, VERTICAL_LOWER_DEFAULT, 0};
  optional<uint8_t> other = this->sweep_setting_[axis_index_(axis == Axis::HORIZONTAL ? Axis::VERTICAL : Axis::HORIZONTAL)];
  if (axis == Axis::HORIZONTAL) {
    // Horizontal sweeps are symmetric about centre, so the setting is the
    // total width and the limits are its halves.
    if (degrees % 2 != 0)
      return false;
    fields[1] = static_cast<int16_t>(degrees / 2);
    fields[3] = static_cast<int16_t>(-(degrees / 2));
    if (other.has_value())
      fields[0] = *other;
  } else {
    // Vertical sweeps pin the lower limit and move only the upper limit.
    fields[0] = degrees;
    if (other.has_value()) {
      fields[1] = static_cast<int16_t>(*other / 2);
      fields[3] = static_cast<int16_t>(-(*other / 2));
    }
  }
  const std::string value = std::to_string(fields[0]) + "," + std::to_string(fields[1]) + "," +
                            std::to_string(fields[2]) + "," + std::to_string(fields[3]);
  if (!this->parent_->set_string_datapoint_value(DP_SWEEP, value))
    return false;
  this->sweep_setting_[axis_index_(axis)] = degrees;
  this->publish_sweeps_();
  return true;
}

std::string DreoHpf007s::curve_string() const {
  std::string value = CURVE_PREFIX;
  for (uint8_t digit : this->curve_)
    value.push_back(static_cast<char>('0' + digit));
  return value;
}

optional<uint8_t> DreoHpf007s::curve_block_speed(uint8_t block) const {
  if (!this->curve_valid_ || block >= CURVE_BLOCKS)
    return {};
  return this->curve_[CURVE_BLOCK_START[block]];
}

bool DreoHpf007s::control_curve_block(uint8_t block, uint8_t speed) {
  if (block >= CURVE_BLOCKS || speed < SPEED_MIN || speed > SPEED_MAX)
    return false;
  if (!this->curve_valid_) {
    ESP_LOGW(TAG, "Custom curve cannot be edited before the MCU has reported it");
    return false;
  }
  std::array<uint8_t, CURVE_ENTRIES> next = this->curve_;
  for (size_t i = 0; i < CURVE_BLOCK_LENGTH[block]; i++)
    next[CURVE_BLOCK_START[block] + i] = speed;
  std::string value = CURVE_PREFIX;
  for (uint8_t digit : next)
    value.push_back(static_cast<char>('0' + digit));
  // Saving a curve in the stock application activates Custom mode with the
  // curve in one frame; the MCU's report then publishes both.
  return this->parent_->set_datapoint_values({
      dreo::DreoDatapointCommand{
          .datapoint_id = DP_MODE, .type = dreo::DreoDatapointType::ENUM, .value_uint = MODE_CUSTOM, .length = 1},
      dreo::DreoDatapointCommand{
          .datapoint_id = DP_CUSTOM_CURVE, .type = dreo::DreoDatapointType::STRING, .value_string = value},
  });
}

bool DreoHpf007s::control_gradient(size_t index) {
  if (index >= this->gradients_.size())
    return false;
  const auto &gradient = this->gradients_[index];
  return this->parent_->set_datapoint_values({
      dreo::DreoDatapointCommand{
          .datapoint_id = DP_LIGHT_CENTRE, .type = dreo::DreoDatapointType::INTEGER, .value_uint = gradient.centre, .length = 4},
      dreo::DreoDatapointCommand{
          .datapoint_id = DP_LIGHT_EDGE, .type = dreo::DreoDatapointType::INTEGER, .value_uint = gradient.edge, .length = 4},
  });
}

optional<uint8_t> DreoHpf007s::sweep_upper(Axis axis) const { return this->sweep_setting_[axis_index_(axis)]; }
optional<int16_t> DreoHpf007s::position_target(Axis axis) const { return this->position_target_[axis_index_(axis)]; }
optional<int16_t> DreoHpf007s::position_measured(Axis axis) const {
  return this->position_measured_[axis_index_(axis)];
}

// ---------------------------------------------------------------------------
// Connection indicator

void DreoHpf007s::set_wifi_connected(bool connected) {
  if (!connected && this->wifi_connected_)
    this->wifi_lost_at_ = millis();
  this->wifi_connected_ = connected;
  this->evaluate_connection_indication_();
}

void DreoHpf007s::set_state_subscriber_connected(bool connected) {
  this->state_subscriber_connected_ = connected;
  this->evaluate_connection_indication_();
}

// Flashing after five continuous seconds without Wi-Fi, off while connected
// without a state-subscribing API client, solid while one is connected. Each
// frame is sent once per derived state change, after the hub has initialized.
void DreoHpf007s::evaluate_connection_indication_() {
  if (this->parent_->get_init_state() != dreo::DreoInitState::INIT_DONE)
    return;
  ConnectionIndication next;
  if (!this->wifi_connected_) {
    if (millis() - this->wifi_lost_at_ < WIFI_LOSS_DEBOUNCE_MS)
      return;
    next = ConnectionIndication::FLASHING;
  } else {
    next = this->state_subscriber_connected_ ? ConnectionIndication::SOLID : ConnectionIndication::OFF;
  }
  if (next == this->indication_sent_)
    return;
  this->indication_sent_ = next;
  switch (next) {
    case ConnectionIndication::FLASHING:
      this->parent_->send_wifi_status_flash();
      break;
    case ConnectionIndication::SOLID:
      this->parent_->send_wifi_status_solid();
      break;
    default:
      this->parent_->send_wifi_status_off();
      break;
  }
}

}  // namespace esphome::dreo_hpf007s
