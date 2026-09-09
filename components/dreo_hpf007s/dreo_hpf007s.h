#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../dreo/dreo.h"
#include "esphome/core/component.h"
#include "esphome/core/optional.h"

namespace esphome::dreo_hpf007s {

// DR-HPF007S stock-protocol datapoints the coordinator composes. Independent
// settings (mute, timers, lock, display, calibration and the like) stay on the
// generic `dreo` platforms in the product package.
static constexpr uint8_t DP_POWER = 1;
static constexpr uint8_t DP_MODE = 2;
static constexpr uint8_t DP_SPEED = 4;
static constexpr uint8_t DP_AXES = 5;           // bit 0 horizontal, bit 1 vertical
static constexpr uint8_t DP_CUSTOM_CURVE = 6;   // "temp:" + 26 speed digits, 67-92 F
static constexpr uint8_t DP_SWEEP = 7;          // "v_upper,h_upper,v_lower,h_lower"
static constexpr uint8_t DP_POSITION = 8;       // "vertical,horizontal"; empty field = leave axis
static constexpr uint8_t DP_AUTO_ON_TIMER = 12;
static constexpr uint8_t DP_CHILD_LOCK = 17;
static constexpr uint8_t DP_PRESENCE = 23;
static constexpr uint8_t DP_PRESENCE_ZONE = 24;
static constexpr uint8_t DP_LIGHT_CENTRE = 25;
static constexpr uint8_t DP_LIGHT_EDGE = 26;
static constexpr uint8_t DP_AUTO_SPEED = 28;

static constexpr uint8_t MODE_NORMAL = 1;
static constexpr uint8_t MODE_NATURAL = 2;
static constexpr uint8_t MODE_SLEEP = 3;
static constexpr uint8_t MODE_AUTO = 4;
static constexpr uint8_t MODE_TURBO = 5;
static constexpr uint8_t MODE_CUSTOM = 6;

static constexpr uint8_t SPEED_MIN = 1;
static constexpr uint8_t SPEED_MAX = 9;

static constexpr uint8_t AXIS_HORIZONTAL_BIT = 0x01;
static constexpr uint8_t AXIS_VERTICAL_BIT = 0x02;

static constexpr int16_t VERTICAL_POSITION_MIN = -30;
static constexpr int16_t VERTICAL_POSITION_MAX = 90;
static constexpr int16_t HORIZONTAL_POSITION_MIN = -75;
static constexpr int16_t HORIZONTAL_POSITION_MAX = 75;
// A commanded head move was observed to complete in about 12 seconds; a
// target that no report has confirmed by then follows the measured position.
static constexpr uint32_t POSITION_TRAVEL_TIMEOUT_MS = 30000;

// The Custom curve holds one speed digit per degree Fahrenheit from 67 to 92.
// The stock application edits it in six blocks: <=67, 68-73, 74-79, 80-85,
// 86-91 and >=92.
static constexpr size_t CURVE_ENTRIES = 26;
static constexpr size_t CURVE_BLOCKS = 6;
static constexpr std::array<uint8_t, CURVE_BLOCKS> CURVE_BLOCK_START{0, 1, 7, 13, 19, 25};
static constexpr std::array<uint8_t, CURVE_BLOCKS> CURVE_BLOCK_LENGTH{1, 6, 6, 6, 6, 1};

// Connection-indicator states derived from Wi-Fi and API-subscriber inputs.
enum class ConnectionIndication : uint8_t { UNKNOWN, FLASHING, OFF, SOLID };
static constexpr uint32_t WIFI_LOSS_DEBOUNCE_MS = 5000;

enum class Axis : uint8_t { HORIZONTAL, VERTICAL };

struct SensorLightGradient {
  std::string name;
  uint32_t centre;
  uint32_t edge;
};

class DreoHpf007sFan;
class DreoHpf007sAxisSwitch;
class DreoHpf007sNumber;
class DreoHpf007sSensor;
class DreoHpf007sSelect;
class DreoHpf007sPresence;
class DreoHpf007sZone;

// Coordinates a Dreo DR-HPF007S over the shared `dreo` hub: the fan's power,
// mode and speed; the two oscillation axes that share one datapoint; the
// structured sweep, head-position and Custom-curve strings; the sensor-light
// gradient pairs; presence availability; and the Wi-Fi indicator policy. Every
// published state follows an MCU report; nothing is invented from a command.
class DreoHpf007s final : public Component {
 public:
  explicit DreoHpf007s(dreo::Dreo *parent) : parent_(parent) {}

  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_fan(DreoHpf007sFan *fan) { this->fan_ = fan; }
  void set_axis_switch(Axis axis, DreoHpf007sAxisSwitch *entity);
  void set_position_number(Axis axis, DreoHpf007sNumber *entity);
  void set_position_sensor(Axis axis, DreoHpf007sSensor *entity);
  void set_curve_number(uint8_t block, DreoHpf007sNumber *entity);
  void set_sweep_select(Axis axis, DreoHpf007sSelect *entity);
  void set_gradient_select(DreoHpf007sSelect *entity) { this->gradient_select_ = entity; }
  void set_presence(DreoHpf007sPresence *entity) { this->presence_ = entity; }
  void set_zone(DreoHpf007sZone *entity) { this->zone_ = entity; }
  void add_gradient(const std::string &name, uint32_t centre, uint32_t edge) {
    this->gradients_.push_back(SensorLightGradient{name, centre, edge});
  }
  const std::vector<SensorLightGradient> &gradients() const { return this->gradients_; }

  // Command policy applied by the hub at its common write boundary.
  bool authorize_command(const dreo::DreoDatapointCommand &command) const;

  // Product controls.
  void control_fan(bool turn_on, bool turn_off, optional<uint8_t> speed, optional<uint8_t> mode);
  bool control_axis(Axis axis, bool state);
  bool control_position(Axis axis, int16_t degrees);
  bool calibrate(bool vertical, bool horizontal);
  bool control_sweep(Axis axis, uint8_t upper_degrees);
  bool control_curve_block(uint8_t block, uint8_t speed);
  bool control_gradient(size_t index);

  // Wi-Fi indicator inputs, fed by the wrapper configuration.
  void set_wifi_connected(bool connected);
  void set_state_subscriber_connected(bool connected);

  // Observed state, report-authoritative.
  optional<bool> power() const { return this->power_; }
  optional<uint8_t> axes() const { return this->axes_; }
  bool power_confirmed_on() const;
  bool power_confirmed_off() const;
  optional<uint8_t> curve_block_speed(uint8_t block) const;
  std::string curve_string() const;
  optional<uint8_t> sweep_upper(Axis axis) const;
  optional<int16_t> position_target(Axis axis) const;
  optional<int16_t> position_measured(Axis axis) const;
  ConnectionIndication connection_indication() const { return this->indication_sent_; }

  // Parsing helpers, exposed for tests. Each returns false and leaves its
  // output untouched on malformed input.
  static bool parse_curve(const std::string &value, std::array<uint8_t, CURVE_ENTRIES> &digits);
  static bool parse_sweep(const std::string &value, std::array<int16_t, 4> &fields);
  static bool parse_position(const std::string &value, optional<int16_t> &vertical, optional<int16_t> &horizontal);

 protected:
  void handle_datapoint_(const dreo::DreoDatapoint &datapoint);
  void handle_power_(bool value);
  void handle_curve_(const std::string &value);
  void handle_sweep_(const std::string &value);
  void handle_report_(const std::vector<uint8_t> &ids);
  void handle_position_(const std::string &value);
  void flush_queued_fan_settings_();
  void publish_fan_();
  void publish_axes_();
  void publish_sweeps_();
  void publish_curve_();
  void publish_gradient_();
  void publish_presence_();
  void invalidate_presence_();
  bool write_mode_(uint8_t mode);
  bool write_position_(const std::string &value);
  uint8_t axes_base_() const;
  void evaluate_connection_indication_();
  static bool parse_int_(const std::string &text, size_t begin, size_t end, int16_t &value);
  static uint8_t axis_bit_(Axis axis) { return axis == Axis::HORIZONTAL ? AXIS_HORIZONTAL_BIT : AXIS_VERTICAL_BIT; }

  dreo::Dreo *parent_;
  DreoHpf007sFan *fan_{nullptr};
  DreoHpf007sAxisSwitch *axis_switches_[2]{nullptr, nullptr};
  DreoHpf007sNumber *position_numbers_[2]{nullptr, nullptr};
  DreoHpf007sSensor *position_sensors_[2]{nullptr, nullptr};
  DreoHpf007sNumber *curve_numbers_[CURVE_BLOCKS]{};
  DreoHpf007sSelect *sweep_selects_[2]{nullptr, nullptr};
  DreoHpf007sSelect *gradient_select_{nullptr};
  DreoHpf007sPresence *presence_{nullptr};
  DreoHpf007sZone *zone_{nullptr};
  std::vector<SensorLightGradient> gradients_{};

  optional<bool> power_{};
  optional<uint8_t> mode_{};
  optional<uint8_t> speed_{};
  optional<uint8_t> axes_{};
  optional<uint8_t> axes_target_{};
  optional<uint8_t> queued_speed_{};
  optional<uint8_t> queued_mode_{};

  bool curve_valid_{false};
  std::array<uint8_t, CURVE_ENTRIES> curve_{};

  bool sweep_valid_{false};
  std::array<int16_t, 4> sweep_{};  // v_upper, h_upper, v_lower, h_lower as reported
  optional<uint8_t> sweep_setting_[2]{};  // retained per axis: HORIZONTAL, VERTICAL

  optional<int16_t> position_measured_[2]{};
  optional<int16_t> position_target_[2]{};
  bool position_pending_[2]{false, false};
  bool position_saw_mismatch_[2]{false, false};
  uint32_t position_written_at_[2]{0, 0};

  optional<uint8_t> presence_value_{};
  optional<std::string> zone_value_{};
  optional<uint32_t> light_centre_{};
  optional<uint32_t> light_edge_{};

  bool wifi_connected_{false};
  bool state_subscriber_connected_{false};
  uint32_t wifi_lost_at_{0};
  ConnectionIndication indication_sent_{ConnectionIndication::UNKNOWN};
};

}  // namespace esphome::dreo_hpf007s
