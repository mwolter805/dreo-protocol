#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#define protected public
#include "components/dreo/dreo.h"
#include "components/dreo/binary_sensor/dreo_binary_sensor.h"
#include "components/dreo/fan/dreo_fan.h"
#include "components/dreo/light/dreo_light.h"
#include "components/dreo/lock/dreo_lock.h"
#include "components/dreo/number/dreo_number.h"
#include "components/dreo/select/dreo_select.h"
#include "components/dreo/switch/dreo_switch.h"
#include "components/dreo/text/dreo_text.h"
#include "components/dreo_ceiling_fan/dreo_ceiling_fan.h"
#include "components/dreo_ceiling_fan/fan/dreo_ceiling_fan_fan.h"
#include "components/dreo_ceiling_fan/light/dreo_ceiling_fan_light.h"
#undef protected

using esphome::advance_millis;
using esphome::set_millis;
using esphome::dreo::Dreo;
using esphome::dreo::DreoBinarySensor;
using esphome::dreo::DreoCommand;
using esphome::dreo::DreoCommandType;
using esphome::dreo::DreoDatapoint;
using esphome::dreo::DreoDatapointCommand;
using esphome::dreo::DreoDatapointType;
using esphome::dreo::DreoFan;
using esphome::dreo::DreoLight;
using esphome::dreo::DreoLightEffect;
using esphome::dreo::DreoLock;
using esphome::dreo::DreoNumber;
using esphome::dreo::DreoSelect;
using esphome::dreo::DreoSwitch;
using esphome::dreo::DreoText;
using esphome::dreo_ceiling_fan::AMBIENT_PRESET_COUNT;
using esphome::dreo_ceiling_fan::DreoCeilingFan;
using esphome::dreo_ceiling_fan::DreoCeilingFanFan;
using esphome::dreo_ceiling_fan::DreoCeilingFanLight;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

bool near(float actual, float expected) { return std::fabs(actual - expected) < 0.001f; }

std::vector<uint8_t> from_hex(const std::string &hex) {
  require(hex.size() % 2 == 0, "fixture has an odd number of hex digits");
  std::vector<uint8_t> data;
  data.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2)
    data.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return data;
}

std::vector<uint8_t> load_fixture(const std::string &name) {
  std::ifstream input("tests/host/fixtures/" + name + ".hex");
  require(input.good(), "cannot open fixture " + name);
  std::ostringstream contents;
  contents << input.rdbuf();
  std::string hex;
  for (char c : contents.str()) {
    if (!std::isspace(static_cast<unsigned char>(c)))
      hex.push_back(c);
  }
  return from_hex(hex);
}

std::vector<uint8_t> datapoint(uint8_t id, DreoDatapointType type, const std::vector<uint8_t> &value) {
  std::vector<uint8_t> result{id, 1, static_cast<uint8_t>(type), static_cast<uint8_t>(value.size() >> 8),
                              static_cast<uint8_t>(value.size())};
  result.insert(result.end(), value.begin(), value.end());
  return result;
}

std::vector<uint8_t> integer_dp(uint8_t id, const std::vector<uint8_t> &value) {
  return datapoint(id, DreoDatapointType::INTEGER, value);
}

std::vector<uint8_t> wifi_status_frame(uint8_t sequence, uint8_t status, uint8_t second_byte = 0x00) {
  return {0x55, 0xAA, 0x00, sequence, 0x03, 0x00, 0x00, 0x02, status, second_byte,
          static_cast<uint8_t>(0x55 + 0xAA + sequence + 0x03 + 0x02 + status + second_byte)};
}

std::vector<uint8_t> boolean_dp(uint8_t id, bool value) {
  return datapoint(id, DreoDatapointType::BOOLEAN, {static_cast<uint8_t>(value)});
}

std::vector<uint8_t> enum_dp(uint8_t id, uint8_t value) {
  return datapoint(id, DreoDatapointType::ENUM, {value});
}

std::vector<uint8_t> string_dp(uint8_t id, const std::string &value) {
  return datapoint(id, DreoDatapointType::STRING, std::vector<uint8_t>(value.begin(), value.end()));
}

std::vector<uint8_t> command_payload(uint8_t id, uint8_t marker, DreoDatapointType type,
                                     const std::vector<uint8_t> &value) {
  std::vector<uint8_t> result{id, marker, static_cast<uint8_t>(type), static_cast<uint8_t>(value.size() >> 8),
                              static_cast<uint8_t>(value.size())};
  result.insert(result.end(), value.begin(), value.end());
  return result;
}

void append(std::vector<uint8_t> &target, const std::vector<uint8_t> &value) {
  target.insert(target.end(), value.begin(), value.end());
}

size_t count_serialized_command(const std::vector<uint8_t> &bytes, uint8_t command) {
  size_t count = 0;
  size_t offset = 0;
  while (offset < bytes.size()) {
    require(offset + 9 <= bytes.size(), "serialized frame was truncated");
    require(bytes[offset] == 0x55 && bytes[offset + 1] == 0xAA, "serialized frame lost its header");
    const size_t payload_size = (static_cast<size_t>(bytes[offset + 6]) << 8) | bytes[offset + 7];
    const size_t frame_size = payload_size + 9;
    require(offset + frame_size <= bytes.size(), "serialized frame length exceeded captured bytes");
    if (bytes[offset + 4] == command)
      count++;
    offset += frame_size;
  }
  return count;
}

const DreoDatapoint *find_dp(const Dreo &dreo, uint8_t id) {
  for (const auto &datapoint : dreo.datapoints_) {
    if (datapoint.id == id)
      return &datapoint;
  }
  return nullptr;
}

void configure_hec_guard(Dreo &dreo) {
  dreo.add_transition_datapoint(1);
  dreo.add_transition_datapoint(3);
  auto *parent = &dreo;
  dreo.set_command_authorizer([parent](const DreoDatapointCommand &command) {
    if (command.datapoint_id == 1 || command.datapoint_id == 17)
      return true;
    auto power = parent->get_boolean_datapoint_value(1);
    if (command.datapoint_id == 6 || command.datapoint_id == 7 || command.datapoint_id == 8) {
      auto mist = parent->get_boolean_datapoint_value(3);
      if (!power.has_value() || parent->is_datapoint_pending(1) || !mist.has_value() ||
          parent->is_datapoint_pending(3))
        return false;
      return parent->allow_sub_entity_control_while_off() || (*power && *mist);
    }
    if (!power.has_value() || parent->is_datapoint_pending(1) || !*power)
      return false;
    return true;
  });
}

void test_marker_default() {
  Dreo dreo;
  dreo.send_datapoint_command_({.datapoint_id = 7, .type = DreoDatapointType::INTEGER}, {0});
  require(dreo.command_queue_.size() == 1, "marker test did not queue a command");
  require(dreo.command_queue_[0].payload.size() == 6, "marker test queued an unexpected payload");
  require(dreo.command_queue_[0].payload[1] == 0, "default datapoint marker is not zero");
}

void run_baseline() {
  Dreo dreo;
  auto body = integer_dp(40, {0x7f, 0xff, 0xff, 0xff});
  append(body, enum_dp(41, 9));
  dreo.handle_datapoints_(body.data(), body.size());
  require(find_dp(dreo, 40) != nullptr && find_dp(dreo, 41) != nullptr,
          "four-byte baseline did not parse and continue");
  for (const auto &value : {std::vector<uint8_t>{0x7f}, std::vector<uint8_t>{0x7f, 0xff}}) {
    Dreo narrow;
    auto narrow_body = integer_dp(40, value);
    append(narrow_body, enum_dp(41, 9));
    narrow.handle_datapoints_(narrow_body.data(), narrow_body.size());
    require(narrow.datapoints_.empty(), "narrow integer unexpectedly parsed in baseline");
  }
  test_marker_default();
  std::cout << "PASS: baseline observes narrow-integer rejection and marker 0\n";
}

#ifdef DREO_FIXED_TESTS
void require_integer(uint8_t id, const std::vector<uint8_t> &bytes, int32_t expected) {
  Dreo dreo;
  auto body = integer_dp(id, bytes);
  dreo.handle_datapoints_(body.data(), body.size());
  auto *value = find_dp(dreo, id);
  require(value != nullptr, "supported integer width was rejected");
  require(value->len == bytes.size(), "integer width was not retained");
  require(value->value_int == expected, "integer was not sign-extended correctly");
}

void test_core_parser_and_writer() {
  require_integer(1, {0x80}, std::numeric_limits<int8_t>::min());
  require_integer(2, {0x80, 0x00}, std::numeric_limits<int16_t>::min());
  require_integer(4, {0x80, 0x00, 0x00, 0x00}, std::numeric_limits<int32_t>::min());
  require_integer(11, {0x00}, 0);
  require_integer(12, {0x00, 0x00}, 0);
  require_integer(14, {0x00, 0x00, 0x00, 0x00}, 0);
  require_integer(21, {0x7f}, std::numeric_limits<int8_t>::max());
  require_integer(22, {0x7f, 0xff}, std::numeric_limits<int16_t>::max());
  require_integer(24, {0x7f, 0xff, 0xff, 0xff}, std::numeric_limits<int32_t>::max());

  for (size_t width : {size_t{0}, size_t{3}, size_t{5}}) {
    Dreo dreo;
    auto body = integer_dp(50, std::vector<uint8_t>(width, 0));
    append(body, enum_dp(51, 9));
    dreo.handle_datapoints_(body.data(), body.size());
    require(find_dp(dreo, 50) == nullptr, "invalid integer width was published");
    require(find_dp(dreo, 51) != nullptr, "invalid integer width discarded its sentinel");
  }

  Dreo combined;
  std::vector<uint8_t> body;
  append(body, boolean_dp(31, true));
  append(body, integer_dp(32, {0x80}));
  append(body, integer_dp(33, {0x80, 0x00}));
  append(body, integer_dp(34, {0x80, 0x00, 0x00, 0x00}));
  append(body, enum_dp(35, 7));
  append(body, string_dp(36, "30,70"));
  combined.handle_datapoints_(body.data(), body.size());
  require(find_dp(combined, 31)->value_bool, "combined report lost boolean");
  require(find_dp(combined, 32)->value_int == -128, "combined report lost int8");
  require(find_dp(combined, 33)->value_int == -32768, "combined report lost int16");
  require(find_dp(combined, 34)->value_int == std::numeric_limits<int32_t>::min(), "combined report lost int32");
  require(find_dp(combined, 35)->value_enum == 7, "combined report lost enum");
  require(find_dp(combined, 36)->value_string == "30,70", "combined report lost string bytes");

  for (const auto &bad : {datapoint(70, static_cast<DreoDatapointType>(0x99), {0xAA, 0xBB}),
                          datapoint(70, DreoDatapointType::BOOLEAN, {0, 1}),
                          datapoint(70, DreoDatapointType::ENUM, {0, 1})}) {
    Dreo dreo;
    auto malformed = bad;
    append(malformed, enum_dp(71, 9));
    dreo.handle_datapoints_(malformed.data(), malformed.size());
    require(find_dp(dreo, 70) == nullptr, "malformed or unknown datapoint was published");
    require(find_dp(dreo, 71) != nullptr, "malformed or unknown datapoint discarded its sentinel");
  }
  {
    Dreo dreo;
    auto oversized = datapoint(72, DreoDatapointType::STRING, std::vector<uint8_t>(256, 'x'));
    append(oversized, enum_dp(73, 9));
    dreo.handle_datapoints_(oversized.data(), oversized.size());
    require(find_dp(dreo, 72) == nullptr, "oversized string was allocated/published");
    require(find_dp(dreo, 73) != nullptr, "oversized string discarded its sentinel");
  }

  for (const std::string &fixture : {"full_report", "string_first"}) {
    Dreo dreo;
    auto report = load_fixture(fixture);
    dreo.handle_datapoints_(report.data(), report.size());
    require(find_dp(dreo, 11) != nullptr && find_dp(dreo, 11)->value_string == "30,70",
            fixture + " did not retain dp11 string");
    require(find_dp(dreo, 28) != nullptr, fixture + " did not reach dp28");
  }

  {
    Dreo dreo;
    dreo.set_command_datapoint_marker(1);
    require(dreo.set_string_datapoint_value(11, "22,84"), "bounded string write was rejected");
    const auto expected = command_payload(11, 1, DreoDatapointType::STRING, {'2', '2', ',', '8', '4'});
    require(dreo.command_queue_.back().payload == expected, "string write payload was not exact");
    require(!dreo.set_string_datapoint_value(11, std::string(256, 'x')), "oversized string write was accepted");
    require(dreo.command_queue_.size() == 1, "oversized string write reached the queue");
    dreo.send_raw_command_(dreo.command_queue_.back());
    std::vector<uint8_t> frame{0x55, 0xAA, 0, 0, 0x06, 0, 0, static_cast<uint8_t>(expected.size())};
    append(frame, expected);
    uint8_t checksum = 0;
    for (uint8_t byte : frame)
      checksum += byte;
    frame.push_back(checksum);
    require(dreo.tx_bytes == frame, "string UART frame marker, length, or checksum was wrong");
  }

  {
    Dreo dreo;
    dreo.set_integer_datapoint_value(60, 0x01020304);
    require(dreo.command_queue_.back().payload == command_payload(60, 0, DreoDatapointType::INTEGER, {1, 2, 3, 4}),
            "integer write did not default to four bytes");
  }
  const std::vector<uint8_t> encoded_value{1, 2, 3, 4};
  for (uint8_t width : {uint8_t{1}, uint8_t{2}, uint8_t{4}}) {
    Dreo before_report;
    before_report.set_integer_command_width(62, width);
    before_report.set_integer_datapoint_value(62, 0x01020304);
    const std::vector<uint8_t> expected(encoded_value.end() - width, encoded_value.end());
    require(before_report.command_queue_.back().payload ==
                command_payload(62, 0, DreoDatapointType::INTEGER, expected),
            "configured integer width was not applied before a report");

    Dreo after_report;
    after_report.set_integer_command_width(63, width);
    auto observed = integer_dp(63, {0, 0, 0, 0});
    after_report.handle_datapoints_(observed.data(), observed.size());
    after_report.set_integer_datapoint_value(63, 0x01020304);
    require(after_report.command_queue_.back().payload ==
                command_payload(63, 0, DreoDatapointType::INTEGER, expected),
            "configured integer width did not override a conflicting report width");
  }
  for (uint8_t width : {uint8_t{1}, uint8_t{2}, uint8_t{4}}) {
    Dreo dreo;
    auto observed = integer_dp(61, std::vector<uint8_t>(width, 0));
    dreo.handle_datapoints_(observed.data(), observed.size());
    dreo.set_integer_datapoint_value(61, 0x01020304);
    const std::vector<uint8_t> expected(encoded_value.end() - width, encoded_value.end());
    const auto &normal = dreo.command_queue_.back().payload;
    require(std::vector<uint8_t>(normal.end() - width, normal.end()) == expected,
            "normal integer setter did not retain observed width");
    dreo.force_set_integer_datapoint_value(61, 0x01020304);
    const auto &forced = dreo.command_queue_.back().payload;
    require(std::vector<uint8_t>(forced.end() - width, forced.end()) == expected,
            "forced integer setter did not retain observed width");
  }
  test_marker_default();
  Dreo marker;
  marker.set_command_datapoint_marker(1);
  marker.send_datapoint_command_({.datapoint_id = 7, .type = DreoDatapointType::INTEGER}, {0});
  require(marker.command_queue_.back().payload[1] == 1, "configured datapoint marker was not emitted");
}

void configure_light(DreoLight &light) {
  light.set_switch_id(9);
  light.set_effect_id(10);
  light.set_brightness_id(12);
  light.set_rgb_id(13);
  light.set_constant_effect(1);
  light.add_effect_mapping(0, "Indicator");
  light.add_effect_mapping(1, "Constant");
  light.add_effect_mapping(2, "Breath");
  light.add_effect_mapping(3, "Cycle");
  light.add_brightness_mapping(1, 0.33f);
  light.add_brightness_mapping(2, 0.67f);
  light.add_brightness_mapping(3, 1.0f);
}

void test_light() {
  Dreo dreo;
  dreo.set_command_datapoint_marker(1);
  DreoLight light(&dreo);
  configure_light(light);
  esphome::light::LightState state(&light);
  DreoLightEffect indicator("Indicator", 0);
  DreoLightEffect constant("Constant", 1);
  DreoLightEffect breath("Breath", 2);
  DreoLightEffect cycle("Cycle", 3);
  state.add_effects({&indicator, &constant, &breath, &cycle});
  require(state.get_effect_count() == 4, "configured light modes were not registered together");
  light.setup();
  std::vector<uint8_t> report;
  append(report, boolean_dp(9, true));
  append(report, integer_dp(10, {0}));
  append(report, integer_dp(12, {3}));
  append(report, integer_dp(13, {0x00, 0x11, 0x22, 0x33}));
  dreo.handle_datapoints_(report.data(), report.size());
  state.flush();
  require(dreo.command_queue_.empty() && dreo.tx_bytes.empty(), "MCU light report echoed a command");
  require(state.current_values.is_on(), "MCU light power did not publish");
  require(near(state.current_values.get_brightness(), 1.0f), "MCU brightness did not publish canonical high");
  require(state.current_effect == "Indicator", "MCU effect did not publish");
  require(near(state.current_values.get_red() * state.current_values.get_color_brightness(), 0x11 / 255.0f) &&
              near(state.current_values.get_green() * state.current_values.get_color_brightness(), 0x22 / 255.0f) &&
              near(state.current_values.get_blue() * state.current_values.get_color_brightness(), 0x33 / 255.0f),
          "MCU RGB did not publish exact color");

  for (const auto &[brightness, value] : std::vector<std::pair<float, uint8_t>>{{0.33f, 1}, {0.67f, 2}, {1.0f, 3}}) {
    dreo.command_queue_.clear();
    auto call = state.make_call();
    call.set_brightness(brightness).perform();
    state.flush();
    require(dreo.command_queue_.size() == 1, "brightness call did not emit exactly one command");
    require(dreo.command_queue_[0].payload == command_payload(12, 1, DreoDatapointType::INTEGER, {value}),
            "brightness call emitted the wrong discrete value");
  }

  auto cycle_before_rgb = state.make_call();
  cycle_before_rgb.set_effect("Cycle").perform();
  state.flush();
  require(state.current_effect == "Cycle", "RGB convergence precondition did not select Cycle");

  dreo.command_queue_.clear();
  auto rgb = state.make_call();
  rgb.set_rgb(0x11 / 255.0f, 0x44 / 255.0f, 0x88 / 255.0f).perform();
  state.flush();
  require(dreo.command_queue_.size() == 2, "RGB call did not emit mode then color");
  require(dreo.command_queue_[0].payload == command_payload(10, 1, DreoDatapointType::INTEGER, {1}),
          "RGB call did not select Constant first");
  require(dreo.command_queue_[1].payload == command_payload(13, 1, DreoDatapointType::INTEGER, {0, 0x11, 0x44, 0x88}),
          "RGB call emitted the wrong color frame");
  require(state.current_effect == "Constant", "RGB call did not publish the implicit Constant effect");
  state.flush();
  require(dreo.command_queue_.size() == 2, "implicit Constant publication echoed an extra command");
  require(state.invalid_effect_transition_count == 0,
          "internal effect publication combined an effect with a transition");

  for (const auto &[name, value] : std::vector<std::pair<std::string, uint8_t>>{{"Indicator", 0}, {"Breath", 2},
                                                                                {"Cycle", 3}}) {
    dreo.command_queue_.clear();
    auto effect = state.make_call();
    effect.set_effect(name.c_str()).perform();
    state.flush();
    require(dreo.command_queue_.size() == 1, "effect-only call emitted extra commands");
    require(dreo.command_queue_[0].payload == command_payload(10, 1, DreoDatapointType::INTEGER, {value}),
            "effect-only call emitted the wrong mode");
  }

  dreo.command_queue_.clear();
  auto off = state.make_call();
  off.set_state(false).perform();
  state.flush();
  require(dreo.command_queue_.size() == 1 &&
              dreo.command_queue_[0].payload == command_payload(9, 1, DreoDatapointType::BOOLEAN, {0}),
          "turning off emitted anything other than dp9");
  dreo.command_queue_.clear();
  auto autonomous = integer_dp(13, {0x00, 0xAA, 0x55, 0x11});
  dreo.handle_datapoints_(autonomous.data(), autonomous.size());
  state.flush();
  require(dreo.command_queue_.empty(), "later autonomous RGB report echoed a command");

  Dreo guarded;
  guarded.set_command_datapoint_marker(1);
  configure_hec_guard(guarded);
  DreoLight guarded_light(&guarded);
  configure_light(guarded_light);
  esphome::light::LightState guarded_state(&guarded_light);
  guarded_light.setup();
  std::vector<uint8_t> guarded_report;
  append(guarded_report, boolean_dp(1, false));
  append(guarded_report, boolean_dp(9, false));
  append(guarded_report, integer_dp(10, {0}));
  append(guarded_report, integer_dp(12, {3}));
  append(guarded_report, integer_dp(13, {0, 0x11, 0x22, 0x33}));
  guarded.handle_datapoints_(guarded_report.data(), guarded_report.size());
  guarded_state.flush();
  auto rejected = guarded_state.make_call();
  rejected.set_state(true).set_effect("Breath").perform();
  guarded_state.flush();
  require(guarded.command_queue_.empty(), "rejected light state reached the queue");
  require(!guarded_state.current_values.is_on(), "rejected light state replaced the last report");
  require(!guarded_light.requested_effect_.has_value(), "rejected composite light effect was retained for replay");
  guarded_state.flush();
}

bool threshold_pair_valid(const std::string &value) {
  auto comma = value.find(',');
  if (comma == std::string::npos || comma == 0 || comma + 1 >= value.size() ||
      value.find(',', comma + 1) != std::string::npos)
    return false;
  int low = 0;
  int high = 0;
  for (size_t i = 0; i < value.size(); i++) {
    if (i == comma)
      continue;
    char c = value[i];
    if (c < '0' || c > '9')
      return false;
    if (i < comma)
      low = low * 10 + (c - '0');
    else
      high = high * 10 + (c - '0');
    if (low > 100 || high > 100)
      return false;
  }
  return low < high;
}

void test_text() {
  Dreo reported;
  DreoText reported_text;
  reported_text.set_dreo_parent(&reported);
  reported_text.set_text_id(11);
  reported_text.setup();
  auto report = string_dp(11, "30,70");
  append(report, enum_dp(28, 0));
  reported.handle_datapoints_(report.data(), report.size());
  require(reported_text.publish_count == 1 && reported_text.state == "30,70", "text report lost exact bytes");
  require(find_dp(reported, 28) != nullptr, "text report discarded its later sentinel");

  Dreo writable;
  writable.set_command_datapoint_marker(1);
  DreoText text;
  text.set_dreo_parent(&writable);
  text.set_text_id(11);
  text.set_validator(threshold_pair_valid);
  text.setup();
  for (const std::string &value : {"30,70", "22,84"}) {
    size_t before = writable.command_queue_.size();
    text.control(value);
    require(writable.command_queue_.size() == before + 1, "known-good threshold did not emit a command");
    require(writable.command_queue_.back().payload ==
                command_payload(11, 1, DreoDatapointType::STRING, std::vector<uint8_t>(value.begin(), value.end())),
            "threshold command bytes were wrong");
  }
  for (const std::string &value : {" 30,70", "+30,70", "30,-70", "3070", "30,70,80", "a,70", "30,b",
                                   "101,102", "70,70", "84,22", ",70", "30,"}) {
    size_t before = writable.command_queue_.size();
    text.control(value);
    require(writable.command_queue_.size() == before, "invalid threshold reached the queue: " + value);
  }
}

void test_lock() {
  Dreo dreo;
  dreo.set_command_datapoint_marker(1);
  DreoLock child_lock;
  child_lock.set_dreo_parent(&dreo);
  child_lock.set_lock_id(26);
  child_lock.setup();
  auto unlocked = boolean_dp(26, false);
  dreo.handle_datapoints_(unlocked.data(), unlocked.size());
  require(child_lock.state == esphome::lock::LOCK_STATE_UNLOCKED, "false report did not publish unlocked");
  size_t published = child_lock.publish_count;
  esphome::lock::LockCall lock_call;
  lock_call.set_state(esphome::lock::LOCK_STATE_LOCKED);
  child_lock.control(lock_call);
  require(dreo.command_queue_.back().payload == command_payload(26, 1, DreoDatapointType::BOOLEAN, {1}),
          "lock command frame was wrong");
  require(child_lock.publish_count == published && child_lock.state == esphome::lock::LOCK_STATE_UNLOCKED,
          "lock command published optimistic success");
  auto locked = boolean_dp(26, true);
  dreo.handle_datapoints_(locked.data(), locked.size());
  require(child_lock.state == esphome::lock::LOCK_STATE_LOCKED, "true report did not publish locked");
  published = child_lock.publish_count;
  esphome::lock::LockCall unlock_call;
  unlock_call.set_state(esphome::lock::LOCK_STATE_UNLOCKED);
  child_lock.control(unlock_call);
  require(dreo.command_queue_.back().payload == command_payload(26, 1, DreoDatapointType::BOOLEAN, {0}),
          "unlock command frame was wrong");
  require(child_lock.publish_count == published && child_lock.state == esphome::lock::LOCK_STATE_LOCKED,
          "unlock command published optimistic success");
  auto wrong_type = enum_dp(26, 1);
  dreo.handle_datapoints_(wrong_type.data(), wrong_type.size());
  require(child_lock.publish_count == published, "wrong lock type was published");
  auto wrong_length = datapoint(26, DreoDatapointType::BOOLEAN, {0, 1});
  append(wrong_length, enum_dp(74, 9));
  dreo.handle_datapoints_(wrong_length.data(), wrong_length.size());
  require(child_lock.publish_count == published, "wrong lock length was published");
  require(find_dp(dreo, 74) != nullptr, "wrong lock length discarded its sentinel");
}

void test_guard() {
  const std::vector<uint8_t> blocked_while_off{3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 23, 25, 26, 27, 28};
  {
    Dreo dreo;
    dreo.set_command_datapoint_marker(1);
    configure_hec_guard(dreo);
    auto off = boolean_dp(1, false);
    dreo.handle_datapoints_(off.data(), off.size());
    require(dreo.force_set_boolean_datapoint_value(1, true), "dp1 was blocked while off");
    require(dreo.force_set_integer_datapoint_value(17, 60), "dp17 was blocked while off");
    for (uint8_t id : blocked_while_off)
      require(!dreo.force_set_boolean_datapoint_value(id, true), "off-state guard allowed datapoint " + std::to_string(id));
    require(dreo.command_queue_.size() == 2, "off-state guard emitted an unexpected frame count");
    require(dreo.command_queue_[0].payload == command_payload(1, 1, DreoDatapointType::BOOLEAN, {1}),
            "off-state dp1 allowed control was not exact");
    require(dreo.command_queue_[1].payload == command_payload(17, 1, DreoDatapointType::INTEGER, {0, 0, 0, 60}),
            "off-state dp17 allowed control was not exact");
  }
  {
    Dreo dreo;
    dreo.set_command_datapoint_marker(1);
    configure_hec_guard(dreo);
    std::vector<uint8_t> state;
    append(state, boolean_dp(1, true));
    append(state, boolean_dp(3, false));
    dreo.handle_datapoints_(state.data(), state.size());
    for (uint8_t id : {uint8_t{6}, uint8_t{7}, uint8_t{8}})
      require(!dreo.force_set_integer_datapoint_value(id, 1), "mist-off guard allowed a dependent command");
    require(dreo.force_set_enum_datapoint_value(4, 2), "normal fan control was blocked while on");
    require(dreo.force_set_boolean_datapoint_value(3, true), "mist power was blocked while on");
    require(dreo.command_queue_.size() == 2, "mist-off guard emitted an unexpected frame count");
    require(dreo.command_queue_[0].payload == command_payload(4, 1, DreoDatapointType::ENUM, {2}),
            "allowed normal fan control was not exact");
    require(dreo.command_queue_[1].payload == command_payload(3, 1, DreoDatapointType::BOOLEAN, {1}),
            "allowed mist power control was not exact");
    require(!dreo.force_set_integer_datapoint_value(6, 2), "pending mist transition allowed a dependent command");
    auto stale = boolean_dp(3, false);
    dreo.handle_datapoints_(stale.data(), stale.size());
    require(dreo.is_datapoint_pending(3), "stale mist report cleared the pending transition");
    auto confirmed = boolean_dp(3, true);
    dreo.handle_datapoints_(confirmed.data(), confirmed.size());
    require(!dreo.is_datapoint_pending(3), "matching mist report did not clear the transition");
    require(dreo.force_set_integer_datapoint_value(6, 2), "confirmed mist transition did not release dependents");
  }
  {
    Dreo dreo;
    configure_hec_guard(dreo);
    require(!dreo.force_set_enum_datapoint_value(4, 1), "unknown power state did not fail closed");
    require(dreo.command_queue_.empty(), "unknown-state rejection was replayed or queued");
  }
  {
    Dreo dreo;
    configure_hec_guard(dreo);
    auto off = boolean_dp(1, false);
    dreo.handle_datapoints_(off.data(), off.size());
    require(dreo.force_set_boolean_datapoint_value(1, true), "power transition command was rejected");
    auto stale = boolean_dp(1, false);
    dreo.handle_datapoints_(stale.data(), stale.size());
    require(dreo.is_datapoint_pending(1), "stale power report cleared the pending transition");
    require(!dreo.force_set_enum_datapoint_value(4, 1), "pending power transition allowed a dependent command");
    auto confirmed = boolean_dp(1, true);
    dreo.handle_datapoints_(confirmed.data(), confirmed.size());
    require(!dreo.is_datapoint_pending(1), "matching power report did not clear the transition");
    require(dreo.force_set_enum_datapoint_value(4, 1), "confirmed power transition did not release controls");
    require(dreo.command_queue_.size() == 2, "blocked power-dependent command was replayed");
  }
  {
    Dreo dreo;
    configure_hec_guard(dreo);
    set_millis(100);
    require(!dreo.force_set_enum_datapoint_value(4, 1), "rate-limit fixture unexpectedly allowed command");
    require(dreo.last_rejection_log_timestamp_ == 100, "first rejection was not logged");
    advance_millis(999);
    dreo.force_set_enum_datapoint_value(4, 1);
    require(dreo.last_rejection_log_timestamp_ == 100, "rejection warning was not rate limited");
    advance_millis(1);
    dreo.force_set_enum_datapoint_value(4, 1);
    require(dreo.last_rejection_log_timestamp_ == 1100, "rejection warning did not resume after the interval");
    set_millis(0);
  }
  {
    Dreo dreo;
    dreo.set_command_datapoint_marker(1);
    configure_hec_guard(dreo);
    DreoFan fan(&dreo, 12);
    fan.set_switch_id(1);
    fan.set_speed_id(5);
    fan.set_oscillation_id(27);
    fan.setup();
    std::vector<uint8_t> report;
    append(report, boolean_dp(1, false));
    append(report, integer_dp(5, {7}));
    append(report, boolean_dp(27, false));
    dreo.handle_datapoints_(report.data(), report.size());
    auto call = fan.make_call();
    call.set_state(true).set_speed(9).set_oscillating(true).perform();
    require(dreo.command_queue_.size() == 1, "combined fan call emitted cached fields during power transition");
    require(dreo.command_queue_[0].payload == command_payload(1, 1, DreoDatapointType::BOOLEAN, {1}),
            "combined fan call did not emit exact power-only frame");
  }
}

void test_subordinate_control_policy() {
  for (bool allow_while_off : {false, true}) {
    for (bool power_on : {false, true}) {
      for (bool mist_on : {false, true}) {
        Dreo dreo;
        configure_hec_guard(dreo);
        dreo.set_allow_sub_entity_control_while_off(allow_while_off);
        std::vector<uint8_t> state;
        append(state, boolean_dp(1, power_on));
        append(state, boolean_dp(3, mist_on));
        dreo.handle_datapoints_(state.data(), state.size());
        const bool settings_allowed = allow_while_off || (power_on && mist_on);
        for (uint8_t id : {uint8_t{6}, uint8_t{7}, uint8_t{8}})
          require(dreo.force_set_integer_datapoint_value(id, 1) == settings_allowed,
                  "subordinate policy returned the wrong confirmed-state result");
        require(dreo.force_set_enum_datapoint_value(4, 1) == power_on,
                "subordinate policy changed an unrelated datapoint result");
      }
    }
  }

  {
    Dreo unknown;
    configure_hec_guard(unknown);
    unknown.set_allow_sub_entity_control_while_off(true);
    require(!unknown.force_set_integer_datapoint_value(6, 1), "unknown parents did not fail closed");
    auto power_only = boolean_dp(1, true);
    unknown.handle_datapoints_(power_only.data(), power_only.size());
    require(!unknown.force_set_integer_datapoint_value(6, 1), "unknown mist state did not fail closed");
  }
  {
    Dreo pending_power;
    configure_hec_guard(pending_power);
    pending_power.set_allow_sub_entity_control_while_off(true);
    std::vector<uint8_t> state;
    append(state, boolean_dp(1, false));
    append(state, boolean_dp(3, false));
    pending_power.handle_datapoints_(state.data(), state.size());
    require(pending_power.force_set_boolean_datapoint_value(1, true), "power transition setup was rejected");
    require(!pending_power.force_set_integer_datapoint_value(6, 1), "pending power did not fail closed");
  }
  {
    Dreo pending_mist;
    configure_hec_guard(pending_mist);
    pending_mist.set_allow_sub_entity_control_while_off(true);
    std::vector<uint8_t> state;
    append(state, boolean_dp(1, true));
    append(state, boolean_dp(3, false));
    pending_mist.handle_datapoints_(state.data(), state.size());
    require(pending_mist.force_set_boolean_datapoint_value(3, true), "mist transition setup was rejected");
    require(!pending_mist.force_set_integer_datapoint_value(6, 1), "pending mist did not fail closed");
  }
  {
    Dreo toggled;
    configure_hec_guard(toggled);
    toggled.set_allow_sub_entity_control_while_off(true);
    std::vector<uint8_t> state;
    append(state, boolean_dp(1, false));
    append(state, boolean_dp(3, false));
    toggled.handle_datapoints_(state.data(), state.size());
    require(toggled.force_set_integer_datapoint_value(6, 2), "enabled policy rejected confirmed-off setting");
    const size_t queued = toggled.command_queue_.size();
    toggled.set_allow_sub_entity_control_while_off(false);
    require(!toggled.force_set_integer_datapoint_value(6, 3), "disabled policy did not restore rejection");
    require(toggled.command_queue_.size() == queued, "disabled policy changed or queued prior state");
  }
}

class AmbientTestOutput : public esphome::light::LightOutput {
 public:
  esphome::light::LightTraits get_traits() override {
    esphome::light::LightTraits traits;
    traits.set_supported_color_modes({esphome::light::ColorMode::RGB});
    return traits;
  }
  void write_state(esphome::light::LightState *) override { writes++; }
  size_t writes{0};
};

void set_hcf_state(Dreo &dreo, bool master, bool fan, bool main_light, bool ambient, uint8_t mode = 1,
                   uint8_t speed = 6, uint8_t brightness = 50, uint8_t color_temperature = 50) {
  std::vector<uint8_t> report;
  append(report, boolean_dp(1, master));
  append(report, boolean_dp(3, fan));
  append(report, boolean_dp(4, main_light));
  append(report, boolean_dp(5, ambient));
  append(report, integer_dp(6, {0, 0, 0, mode}));
  append(report, integer_dp(7, {0, 0, 0, speed}));
  append(report, integer_dp(8, {0, 0, 0, brightness}));
  append(report, integer_dp(9, {0, 0, 0, color_temperature}));
  dreo.handle_datapoints_(report.data(), report.size());
}

std::vector<uint8_t> queued_ids(const Dreo &dreo) {
  std::vector<uint8_t> ids;
  for (const auto &command : dreo.command_queue_) {
    if (command.cmd == DreoCommandType::DATAPOINT_DELIVER)
      ids.push_back(command.payload[0]);
  }
  return ids;
}

void configure_hcf(Dreo &dreo, DreoCeilingFan &coordinator) {
  for (uint8_t id : {uint8_t{1}, uint8_t{3}, uint8_t{4}, uint8_t{5}})
    dreo.add_transition_datapoint(id);
  for (uint8_t id : {uint8_t{6}, uint8_t{7}, uint8_t{8}, uint8_t{9}})
    dreo.set_integer_command_width(id, 1);
  dreo.set_command_authorizer([&coordinator](const DreoDatapointCommand &command) {
    return coordinator.authorize_command(command);
  });
  coordinator.setup();
}

void test_ceiling_fan_coordinator() {
  for (bool master : {false, true}) {
    for (bool fan_child : {false, true}) {
      for (bool light_child : {false, true}) {
        Dreo dreo;
        DreoCeilingFan coordinator(&dreo);
        DreoCeilingFanFan fan(&coordinator);
        DreoCeilingFanLight main_light(&coordinator);
        esphome::light::LightState main_light_state(&main_light);
        fan.setup();
        configure_hcf(dreo, coordinator);
        set_hcf_state(dreo, master, fan_child, light_child, false);
        require(fan.state == (master && fan_child), "fan composition state was not master && child");
        require(main_light_state.remote_values.is_on() == (master && light_child),
                "main-light composition state was not master && child");
      }
    }
  }

  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    DreoCeilingFanLight main_light(&coordinator);
    esphome::light::LightState main_light_state(&main_light);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, true, false, true, false, 1, 6, 1, 0);
    require(near(main_light_state.remote_values.get_brightness(), 0.01f),
            "main-light dp08=1 did not publish as one-percent brightness");
    require(near(main_light.wire_to_color_temperature_(0), 1000000.0f / 2700.0f) &&
                near(main_light.wire_to_color_temperature_(100), 1000000.0f / 6500.0f),
            "main-light color-temperature endpoints did not map to 2700-6500 K");
    require(main_light.color_temperature_to_wire_(1000000.0f / 2700.0f) == 0 &&
                main_light.color_temperature_to_wire_(1000000.0f / 6500.0f) == 100,
            "main-light color-temperature endpoints did not map back to dp09 0-100");
  }

  for (bool allow : {false, true}) {
    for (bool master : {false, true}) {
      for (bool child : {false, true}) {
        Dreo dreo;
        DreoCeilingFan coordinator(&dreo);
        configure_hcf(dreo, coordinator);
        dreo.set_allow_sub_entity_control_while_off(allow);
        set_hcf_state(dreo, master, child, child, false);
        const bool expected = allow || (master && child);
        require(dreo.force_set_integer_datapoint_value(6, 2) == expected,
                "fan subordinate authorization matrix mismatch");
        require(dreo.force_set_integer_datapoint_value(8, 60) == expected,
                "main-light subordinate authorization matrix mismatch");
      }
    }
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    dreo.set_allow_sub_entity_control_while_off(true);
    require(!dreo.force_set_integer_datapoint_value(6, 2), "unknown HCF parents did not fail closed");
    set_hcf_state(dreo, false, false, false, false);
    require(dreo.force_set_boolean_datapoint_value(1, true), "pending-parent fixture did not queue master");
    require(!dreo.force_set_integer_datapoint_value(6, 2), "pending HCF parent did not fail closed");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    dreo.set_allow_sub_entity_control_while_off(true);
    set_hcf_state(dreo, true, false, false, false);
    require(dreo.force_set_boolean_datapoint_value(3, true), "pending-fan-child fixture did not queue child");
    require(!dreo.force_set_integer_datapoint_value(7, 8), "pending fan child did not fail closed");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    dreo.set_allow_sub_entity_control_while_off(true);
    set_hcf_state(dreo, true, false, false, false);
    require(dreo.force_set_boolean_datapoint_value(4, true), "pending-light-child fixture did not queue child");
    require(!dreo.force_set_integer_datapoint_value(9, 75), "pending main-light child did not fail closed");
  }
  for (bool allow : {false, true}) {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    dreo.set_allow_sub_entity_control_while_off(allow);
    set_hcf_state(dreo, false, false, false, false);
    require(!dreo.force_set_boolean_datapoint_value(19, true),
            "off-state option granted an unrelated datapoint while master was off");
    set_hcf_state(dreo, true, false, false, false);
    require(dreo.force_set_boolean_datapoint_value(19, true),
            "off-state option changed the existing unrelated-datapoint result");
  }

  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, false, false, true, true);
    coordinator.control_fan(true, false, {}, {});
    require(queued_ids(dreo) == std::vector<uint8_t>({4, 5, 3, 1}),
            "children-first fan power sequence was not exact");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, false, true, false, true);
    coordinator.control_main_light(true, false, {}, {});
    require(queued_ids(dreo) == std::vector<uint8_t>({3, 5, 4, 1}),
            "children-first main-light power sequence was not exact");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, false, true, true, false);
    coordinator.control_ambient(true);
    require(queued_ids(dreo) == std::vector<uint8_t>({3, 4, 5, 1}),
            "children-first ambient power sequence was not exact");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, true, true, true, false);
    coordinator.control_fan(false, true, {}, {});
    require(queued_ids(dreo) == std::vector<uint8_t>({3}), "multi-feature fan-off did not write only its child");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, true, true, false, false);
    coordinator.control_fan(false, true, {}, {});
    require(queued_ids(dreo) == std::vector<uint8_t>({1}), "sole-feature fan-off did not write only master");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, true, true, true, false);
    coordinator.control_main_light(false, true, {}, {});
    require(queued_ids(dreo) == std::vector<uint8_t>({4}),
            "multi-feature main-light-off did not write only its child");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, true, false, true, false);
    coordinator.control_main_light(false, true, {}, {});
    require(queued_ids(dreo) == std::vector<uint8_t>({1}),
            "sole-feature main-light-off did not write only master");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, true, true, false, true);
    coordinator.control_ambient(false);
    require(queued_ids(dreo) == std::vector<uint8_t>({5}),
            "multi-feature ambient-off did not write only its child");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, true, false, false, true);
    coordinator.control_ambient(false);
    require(queued_ids(dreo) == std::vector<uint8_t>({1}),
            "sole-feature ambient-off did not write only master");
  }

  for (bool master_first : {false, true}) {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    DreoCeilingFanFan fan(&coordinator);
    fan.setup();
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, false, false, false, false, 1, 6);
    auto first = fan.make_call();
    first.set_state(true).set_speed(4).perform();
    auto replacement = fan.make_call();
    replacement.set_state(true).set_speed(9).perform();
    require(queued_ids(dreo) == std::vector<uint8_t>({3, 1}), "queued replacement duplicated power commands");
    auto report_first = boolean_dp(master_first ? 1 : 3, true);
    dreo.handle_datapoints_(report_first.data(), report_first.size());
    require(queued_ids(dreo) == std::vector<uint8_t>({3, 1}), "setting flushed before both parent reports");
    auto report_second = boolean_dp(master_first ? 3 : 1, true);
    dreo.handle_datapoints_(report_second.data(), report_second.size());
    require(queued_ids(dreo) == std::vector<uint8_t>({3, 1, 7}), "queued setting did not flush exactly once");
    require(dreo.command_queue_.back().payload == command_payload(7, 0, DreoDatapointType::INTEGER, {9}),
            "last-write-wins fan setting used the wrong value or width");
    dreo.handle_datapoints_(report_second.data(), report_second.size());
    const auto ids = queued_ids(dreo);
    require(std::count(ids.begin(), ids.end(), uint8_t{7}) == 1, "queued fan setting flushed more than once");
  }

  // A turn-on call that carries a preset mode while the fan is off: the mode
  // must reach dp06 exactly once, after the power writes, and the fan must
  // publish that preset. Observed on the wire 2026-09-03/04: only the power
  // datapoints were written and the fan came on in the MCU's retained mode.
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    DreoCeilingFanFan fan(&coordinator);
    fan.setup();
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, false, false, false, false, 2, 6);  // retained mode Natural
    auto call = fan.make_call();
    call.set_state(true).set_preset_mode("Normal").perform();
    require(queued_ids(dreo) == std::vector<uint8_t>({3, 1}),
            "turn-on with preset did not queue exactly the power writes first");
    auto ack = std::vector<uint8_t>{};
    dreo.handle_datapoints_(ack.data(), ack.size());
    set_hcf_state(dreo, false, true, false, false, 2, 6);   // MCU: dp03 on, dp06 still 2
    require(queued_ids(dreo) == std::vector<uint8_t>({3, 1}),
            "turn-on preset flushed before the master report");
    set_hcf_state(dreo, true, true, false, false, 2, 6);    // MCU: dp01 on
    require(queued_ids(dreo) == std::vector<uint8_t>({3, 1, 6}),
            "turn-on with preset did not write dp06 exactly once after the power writes");
    require(dreo.command_queue_.back().payload == command_payload(6, 0, DreoDatapointType::INTEGER, {1}),
            "turn-on preset wrote the wrong dp06 value or width");
    set_hcf_state(dreo, true, true, false, false, 1, 6);    // MCU echoes dp06=1
    require(fan.get_preset_mode() == "Normal", "turn-on preset did not publish Normal");
    const auto ids = queued_ids(dreo);
    require(std::count(ids.begin(), ids.end(), uint8_t{6}) == 1, "turn-on preset wrote dp06 more than once");
  }
  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    DreoCeilingFanFan fan(&coordinator);
    fan.setup();
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, false, false, false, false, 2, 6);
    auto call = fan.make_call();
    call.set_state(true).perform();
    set_hcf_state(dreo, false, true, false, false, 2, 6);
    set_hcf_state(dreo, true, true, false, false, 2, 6);
    const auto ids = queued_ids(dreo);
    require(std::count(ids.begin(), ids.end(), uint8_t{6}) == 0,
            "plain turn-on wrote dp06 although no preset was requested");
    require(fan.get_preset_mode() == "Natural", "plain turn-on did not keep the MCU's retained mode");
  }

  for (bool master_first : {false, true}) {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    DreoCeilingFanLight main_light(&coordinator);
    esphome::light::LightState state(&main_light);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, false, false, false, false, 1, 6, 50, 50);
    state.flush();
    auto first = state.make_call();
    first.set_state(true).set_brightness(0.25f).set_color_temperature(1000000.0f / 2700.0f).perform();
    state.flush();
    auto replacement = state.make_call();
    replacement.set_state(true).set_brightness(0.65f).set_color_temperature(1000000.0f / 6500.0f).perform();
    state.flush();
    require(queued_ids(dreo) == std::vector<uint8_t>({4, 1}),
            "queued main-light replacement duplicated power commands");
    auto report_first = boolean_dp(master_first ? 1 : 4, true);
    dreo.handle_datapoints_(report_first.data(), report_first.size());
    require(queued_ids(dreo) == std::vector<uint8_t>({4, 1}),
            "main-light settings flushed before both parent reports");
    auto report_second = boolean_dp(master_first ? 4 : 1, true);
    dreo.handle_datapoints_(report_second.data(), report_second.size());
    require(queued_ids(dreo) == std::vector<uint8_t>({4, 1, 8, 9}),
            "queued main-light settings did not flush exactly once");
    require(dreo.command_queue_[2].payload == command_payload(8, 0, DreoDatapointType::INTEGER, {65}) &&
                dreo.command_queue_[3].payload == command_payload(9, 0, DreoDatapointType::INTEGER, {100}),
            "last-write-wins main-light settings used the wrong values or widths");
    dreo.handle_datapoints_(report_second.data(), report_second.size());
    const auto ids = queued_ids(dreo);
    require(std::count(ids.begin(), ids.end(), uint8_t{8}) == 1 &&
                std::count(ids.begin(), ids.end(), uint8_t{9}) == 1,
            "queued main-light settings flushed more than once");
  }

  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    DreoCeilingFanFan fan(&coordinator);
    fan.setup();
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, false, false, false, false);
    auto on = fan.make_call();
    on.set_state(true).set_speed(10).perform();
    auto off = fan.make_call();
    off.set_state(false).perform();
    auto master = boolean_dp(1, true);
    auto child = boolean_dp(3, true);
    dreo.handle_datapoints_(master.data(), master.size());
    dreo.handle_datapoints_(child.data(), child.size());
    const auto ids = queued_ids(dreo);
    require(std::count(ids.begin(), ids.end(), uint8_t{7}) == 0,
            "off request did not cancel queued fan setting");
  }

  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    DreoCeilingFanLight main_light(&coordinator);
    esphome::light::LightState state(&main_light);
    configure_hcf(dreo, coordinator);
    set_hcf_state(dreo, false, false, false, false);
    state.flush();
    auto on = state.make_call();
    on.set_state(true).set_brightness(0.8f).perform();
    state.flush();
    auto off = state.make_call();
    off.set_state(false).perform();
    state.flush();
    auto master = boolean_dp(1, true);
    auto child = boolean_dp(4, true);
    dreo.handle_datapoints_(master.data(), master.size());
    dreo.handle_datapoints_(child.data(), child.size());
    const auto ids = queued_ids(dreo);
    require(std::count(ids.begin(), ids.end(), uint8_t{8}) == 0 &&
                std::count(ids.begin(), ids.end(), uint8_t{9}) == 0,
            "off request did not cancel queued main-light settings");
  }

  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    DreoCeilingFanFan fan(&coordinator);
    fan.setup();
    configure_hcf(dreo, coordinator);
    // What Home Assistant is told. The modes must reach the traits object the
    // platform returns, not just the Fan's own storage; the installed image
    // registered them in setup() and still advertised preset_modes: [].
    {
      const auto traits = fan.get_traits();
      require(traits.supports_speed() && traits.supported_speed_count() == 12 && traits.supports_direction() &&
                  !traits.supports_oscillation(),
              "fan traits changed speed count, direction or oscillation");
      require(traits.supports_preset_modes(), "fan traits advertise no preset modes");
      const auto &modes = traits.supported_preset_modes();
      require(modes.size() == 3 && std::strcmp(modes[0], "Normal") == 0 && std::strcmp(modes[1], "Natural") == 0 &&
                  std::strcmp(modes[2], "Sleep") == 0,
              "fan traits do not advertise exactly Normal, Natural, Sleep");
    }
    for (uint8_t mode : {uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{4}}) {
      set_hcf_state(dreo, true, true, false, false, mode);
      require(fan.direction == (mode == 4 ? esphome::fan::FanDirection::REVERSE
                                         : esphome::fan::FanDirection::FORWARD),
              "fan mode published the wrong direction");
      if (mode == 1)
        require(fan.get_preset_mode() == "Normal", "dp06=1 did not publish Normal");
      else if (mode == 2)
        require(fan.get_preset_mode() == "Natural", "dp06=2 did not publish Natural");
      else if (mode == 3)
        require(fan.get_preset_mode() == "Sleep", "dp06=3 did not publish Sleep");
      else
        require(fan.get_preset_mode().empty(), "reverse report retained a forward preset");
    }
    dreo.command_queue_.clear();
    auto forward = fan.make_call();
    forward.set_direction(esphome::fan::FanDirection::FORWARD).perform();
    require(dreo.command_queue_.back().payload == command_payload(6, 0, DreoDatapointType::INTEGER, {1}),
            "direct forward request did not select Normal/dp06=1");
  }

  {
    Dreo dreo;
    DreoCeilingFan coordinator(&dreo);
    AmbientTestOutput output;
    esphome::light::LightState ambient(&output);
    coordinator.set_ambient_light(&ambient);
    configure_hcf(dreo, coordinator);
    auto startup = ambient.make_call();
    startup.set_state(true).perform();
    require(dreo.command_queue_.empty(), "ambient startup callback wrote before authoritative state");
    set_hcf_state(dreo, true, false, false, false);
    dreo.command_queue_.clear();

    auto unsuppressed = ambient.make_call();
    unsuppressed.set_state(true).perform();
    require(queued_ids(dreo) == std::vector<uint8_t>({5}),
            "ambient negative control did not observe one callback and attempted dp05 write");

    dreo.command_queue_.clear();
    coordinator.publish_ambient_();
    require(dreo.command_queue_.empty(), "suppressed ambient publication looped back to UART");
    require(coordinator.ambient_power_.has_value() && !*coordinator.ambient_power_,
            "suppressed ambient publication changed retained dp05 state");
  }
}

class NamedEffect : public esphome::light::LightEffect {
 public:
  using esphome::light::LightEffect::LightEffect;
  void apply() override {}
};

const char *const PRESET_NAMES[] = {"Rainbow Marquee", "Rainbow Breath", "Rainbow Cycle", "Solid Yellow"};

// Counts string commands for one datapoint in everything the hub has queued or
// already written, so a sentinel cannot be missed just because the queue drained.
size_t count_string_commands(const Dreo &dreo, uint8_t datapoint_id, const std::string &value) {
  size_t count = 0;
  for (const auto &command : dreo.command_queue_) {
    if (command.cmd != DreoCommandType::DATAPOINT_DELIVER || command.payload.empty())
      continue;
    if (command.payload[0] != datapoint_id)
      continue;
    if (command.payload[2] != static_cast<uint8_t>(DreoDatapointType::STRING))
      continue;
    const std::string body(command.payload.begin() + 5, command.payload.end());
    if (body == value)
      count++;
  }
  return count;
}

size_t count_queued(const Dreo &dreo, uint8_t datapoint_id) {
  size_t count = 0;
  for (uint8_t id : queued_ids(dreo)) {
    if (id == datapoint_id)
      count++;
  }
  return count;
}

struct AmbientFixture {
  Dreo dreo;
  DreoCeilingFan coordinator{&dreo};
  AmbientTestOutput output;
  esphome::light::LightState ambient{&output};
  NamedEffect marquee{PRESET_NAMES[0]};
  NamedEffect breath{PRESET_NAMES[1]};
  NamedEffect cycle{PRESET_NAMES[2]};
  NamedEffect yellow{PRESET_NAMES[3]};

  AmbientFixture() {
    ambient.add_effects({&marquee, &breath, &cycle, &yellow});
    for (const char *name : PRESET_NAMES)
      coordinator.add_ambient_preset_effect(name);
    // Mirrors the product package: marker 1 and a one-byte dp26 width.
    dreo.set_command_datapoint_marker(1);
    dreo.set_integer_command_width(26, 1);
    configure_hcf(dreo, coordinator);
    coordinator.set_ambient_light(&ambient);
  }

  void report_preset_cursor(uint8_t value) {
    auto body = integer_dp(25, {0, 0, 0, value});
    dreo.handle_datapoints_(body.data(), body.size());
  }

  // The MCU reports dp28 = "0" in every status report from the first one
  // after boot, so on hardware the sentinel's last known value always equals
  // the constant the coordinator wants to send. A fixture without this seed
  // never reaches the transport's unchanged-value branch, which is how the
  // original test passed an image that sent nothing.
  void report_predefine_sentinel() {
    auto body = string_dp(28, "0");
    dreo.handle_datapoints_(body.data(), body.size());
  }
};

// The recovered dp25-dp28 behaviour. Each assertion below is written so the
// opposite implementation would fail it: a suppressed remote change that still
// emitted the sentinel, an out-of-range cursor that indexed the map, or a
// preset that applied while the ambient output was off.
void test_ceiling_fan_ambient_presets() {
  // dp25 selects a preset only while dp01 and dp05 are both on.
  {
    AmbientFixture fixture;
    set_hcf_state(fixture.dreo, true, false, false, false);
    fixture.report_preset_cursor(2);
    require(fixture.ambient.current_effect.empty(), "preset applied while the ambient output was off");

    set_hcf_state(fixture.dreo, true, false, false, true);
    fixture.report_preset_cursor(2);
    require(fixture.ambient.current_effect == PRESET_NAMES[1], "dp25=2 did not select the second preset");

    // 0 means "keep the last effect", so it must change nothing.
    fixture.report_preset_cursor(0);
    require(fixture.ambient.current_effect == PRESET_NAMES[1], "dp25=0 replaced the running effect");

    // Anything past the configured presets is ignored rather than clamped.
    for (uint8_t invalid : {uint8_t{5}, uint8_t{11}, uint8_t{200}}) {
      fixture.report_preset_cursor(invalid);
      require(fixture.ambient.current_effect == PRESET_NAMES[1],
              "an out-of-range dp25 value changed the running effect");
    }

    fixture.report_preset_cursor(4);
    require(fixture.ambient.current_effect == PRESET_NAMES[3], "dp25=4 did not select the fourth preset");
  }

  // A remote-originated preset must not look like a local change: no sentinel
  // and no ambient power command follow it.
  {
    AmbientFixture fixture;
    set_hcf_state(fixture.dreo, true, false, false, true);
    fixture.report_predefine_sentinel();
    const size_t power_before = count_queued(fixture.dreo, 5) + count_queued(fixture.dreo, 1);
    fixture.report_preset_cursor(3);
    require(fixture.ambient.current_effect == PRESET_NAMES[2], "remote preset precondition did not apply");
    require(count_string_commands(fixture.dreo, 28, "0") == 0, "a remote preset emitted the dp28 sentinel");
    require(count_queued(fixture.dreo, 5) + count_queued(fixture.dreo, 1) == power_before,
            "a remote preset emitted an ambient power command");
  }

  // A user change emits exactly one sentinel, with the outputs on and with the
  // master gate off — and with dp28 already reported as "0", as it always is
  // on hardware, so a deduplicating send would be dropped here too.
  for (bool master_on : {true, false}) {
    AmbientFixture fixture;
    set_hcf_state(fixture.dreo, master_on, false, false, master_on);
    fixture.report_predefine_sentinel();
    require(count_string_commands(fixture.dreo, 28, "0") == 0, "state reports alone emitted the sentinel");

    fixture.ambient.make_call().set_state(true).set_rgb(1.0f, 0.0f, 0.0f).perform();
    require(count_string_commands(fixture.dreo, 28, "0") == 1,
            "a user colour change did not emit exactly one dp28 sentinel");

    fixture.ambient.make_call().set_state(true).set_brightness(0.25f).perform();
    require(count_string_commands(fixture.dreo, 28, "0") == 2,
            "a user brightness change did not emit its own dp28 sentinel");

    fixture.ambient.make_call().set_state(true).set_effect(PRESET_NAMES[0]).perform();
    require(count_string_commands(fixture.dreo, 28, "0") == 3,
            "a user effect change did not emit its own dp28 sentinel");
  }

  // The MCU does not reset dp25 on dp28 (measured on the wire 2026-09-03): its
  // reply to the sentinel write repeats the cursor it already reported. That
  // repeat must not re-select the preset the user just left, while a remote
  // press — which changes dp25 — must still apply its preset.
  {
    AmbientFixture fixture;
    set_hcf_state(fixture.dreo, true, false, false, true);
    fixture.report_predefine_sentinel();
    fixture.report_preset_cursor(1);
    require(fixture.ambient.current_effect == PRESET_NAMES[0], "cursor-reply precondition did not apply preset 1");

    fixture.ambient.make_call().set_state(true).set_effect(PRESET_NAMES[3]).perform();
    require(fixture.ambient.current_effect == PRESET_NAMES[3], "the user's effect change did not take");
    require(count_string_commands(fixture.dreo, 28, "0") == 1, "the user change did not emit exactly one dp28 sentinel");
    fixture.report_preset_cursor(1);
    require(fixture.ambient.current_effect == PRESET_NAMES[3], "the MCU's dp25 re-report re-applied the preset");
    require(count_string_commands(fixture.dreo, 28, "0") == 1, "the MCU's dp25 re-report emitted a dp28 sentinel");

    fixture.report_preset_cursor(2);
    require(fixture.ambient.current_effect == PRESET_NAMES[1], "a remote press to dp25=2 did not apply its preset");

    // The remote can revisit a cursor value only by cycling through its off
    // position, which the stock capture shows resets dp25 to 0.
    fixture.ambient.make_call().set_state(true).set_effect(PRESET_NAMES[3]).perform();
    require(count_string_commands(fixture.dreo, 28, "0") == 2, "the second user change did not emit its sentinel");
    fixture.report_preset_cursor(2);
    require(fixture.ambient.current_effect == PRESET_NAMES[3], "a repeated dp25=2 re-applied the preset");
    set_hcf_state(fixture.dreo, true, false, false, false);
    fixture.report_preset_cursor(0);
    set_hcf_state(fixture.dreo, true, false, false, true);
    fixture.report_preset_cursor(1);
    require(fixture.ambient.current_effect == PRESET_NAMES[0], "dp25=1 after the off cycle did not apply preset 1");
    require(count_string_commands(fixture.dreo, 28, "0") == 2, "the remote's return to dp25=1 emitted a sentinel");
  }

  // Power-only calls carry no sentinel, including an explicit turn-off while
  // an effect runs: ESPHome stops the effect on that call, which must not be
  // counted as a presentation change or forget the remembered effect. The MCU's
  // reports after each write are what resume the effect, as on hardware.
  {
    AmbientFixture fixture;
    set_hcf_state(fixture.dreo, true, false, false, true);
    fixture.report_predefine_sentinel();
    fixture.ambient.make_call().set_state(false).perform();
    fixture.ambient.make_call().set_state(true).perform();
    require(count_string_commands(fixture.dreo, 28, "0") == 0, "a power-only call emitted the dp28 sentinel");

    fixture.report_preset_cursor(1);
    require(fixture.ambient.current_effect == PRESET_NAMES[0], "power-off precondition did not select a preset");
    fixture.ambient.make_call().set_state(false).perform();
    require(fixture.ambient.current_effect.empty(), "stub did not model ESPHome stopping the effect on turn-off");
    require(count_string_commands(fixture.dreo, 28, "0") == 0, "turning off with an effect running emitted the dp28 sentinel");
    set_hcf_state(fixture.dreo, true, false, false, false);
    fixture.ambient.make_call().set_state(true).perform();
    set_hcf_state(fixture.dreo, true, false, false, true);
    require(fixture.ambient.current_effect == PRESET_NAMES[0], "the effect did not resume after a user off/on cycle");
    require(count_string_commands(fixture.dreo, 28, "0") == 0, "the user off/on cycle emitted the dp28 sentinel");
  }

  // The remembered effect survives an MCU-originated off/on cycle. The stub
  // reproduces ESPHome's rule that an explicit turn-off stops the effect, so
  // this would fail without the restore.
  {
    AmbientFixture fixture;
    set_hcf_state(fixture.dreo, true, false, false, true);
    fixture.report_preset_cursor(3);
    require(fixture.ambient.current_effect == PRESET_NAMES[2], "restore precondition did not select a preset");
    set_hcf_state(fixture.dreo, true, false, false, false);
    require(fixture.ambient.current_effect.empty(), "stub did not model ESPHome stopping the effect on turn-off");
    set_hcf_state(fixture.dreo, true, false, false, true);
    require(fixture.ambient.current_effect == PRESET_NAMES[2], "the remembered effect did not resume on the on report");
  }

  // dp26: one one-byte correction after initialization, and none once the MCU
  // agrees.
  {
    AmbientFixture fixture;
    fixture.dreo.init_state_ = esphome::dreo::DreoInitState::INIT_DATAPOINT;
    auto body = boolean_dp(1, true);
    fixture.dreo.handle_command_(static_cast<uint8_t>(DreoCommandType::DATAPOINT_REPORT), 0, 0, body.data(),
                                 body.size());
    require(count_queued(fixture.dreo, 26) == 1, "initialization did not write the preset count exactly once");
    for (const auto &command : fixture.dreo.command_queue_) {
      if (command.cmd == DreoCommandType::DATAPOINT_DELIVER && command.payload[0] == 26) {
        require(command.payload == command_payload(26, 1, DreoDatapointType::INTEGER, {AMBIENT_PRESET_COUNT}),
                "the preset-count command was not a one-byte dp26=4");
      }
    }
    auto agreed = integer_dp(26, {0, 0, 0, AMBIENT_PRESET_COUNT});
    fixture.dreo.handle_datapoints_(agreed.data(), agreed.size());
    require(count_queued(fixture.dreo, 26) == 1, "an agreeing dp26 report queued another correction");
  }
}

void test_existing_platform_type_safety() {
  Dreo dreo;
  DreoSelect select;
  select.set_dreo_parent(&dreo);
  select.set_select_id(70, true);
  select.set_select_mappings({4, 7});
  select.traits.options_ = {"four", "seven"};
  select.setup();
  auto body = integer_dp(70, {7});
  dreo.handle_datapoints_(body.data(), body.size());
  require(select.publish_count == 1 && select.last_state == 1, "integer select mapping was not type-safe");
  DreoBinarySensor sensor;
  sensor.set_dreo_parent(&dreo);
  sensor.set_sensor_id(71);
  sensor.setup();
  for (uint8_t value : {uint8_t{0}, uint8_t{1}, uint8_t{4}}) {
    auto sensor_body = integer_dp(71, {value});
    dreo.handle_datapoints_(sensor_body.data(), sensor_body.size());
    require(sensor.state == (value != 0), "integer binary-sensor mapping was not type-safe");
  }
}

void test_masked_binary_sensor_and_number_clamp() {
  Dreo dreo;
  DreoBinarySensor unmasked;
  unmasked.set_dreo_parent(&dreo);
  unmasked.set_sensor_id(71);
  unmasked.setup();
  DreoBinarySensor masked;
  masked.set_dreo_parent(&dreo);
  masked.set_sensor_id(72);
  masked.set_bitmask(0x01);
  masked.setup();
  for (uint8_t value : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{4}}) {
    auto first = integer_dp(71, {value});
    dreo.handle_datapoints_(first.data(), first.size());
    require(unmasked.state == (value != 0), "unmasked binary sensor compatibility changed");
    auto second = integer_dp(72, {value});
    dreo.handle_datapoints_(second.data(), second.size());
    require(masked.state == ((value & 0x01) != 0), "masked binary sensor did not isolate bit zero");
  }

  DreoNumber raw_number;
  raw_number.set_dreo_parent(&dreo);
  raw_number.set_number_id(73);
  raw_number.traits.min_value_ = 1;
  raw_number.traits.max_value_ = 4;
  raw_number.setup();
  DreoNumber clamped_number;
  clamped_number.set_dreo_parent(&dreo);
  clamped_number.set_number_id(74);
  clamped_number.set_clamp_reported_value(true);
  clamped_number.traits.min_value_ = 1;
  clamped_number.traits.max_value_ = 4;
  clamped_number.setup();
  for (int value : {0, 1, 3, 4, 5}) {
    auto raw = integer_dp(73, {static_cast<uint8_t>(value)});
    dreo.handle_datapoints_(raw.data(), raw.size());
    require(near(raw_number.state, value), "default-disabled report clamp changed a number");
    auto clamped = integer_dp(74, {static_cast<uint8_t>(value)});
    dreo.handle_datapoints_(clamped.data(), clamped.size());
    require(near(clamped_number.state, std::max(1, std::min(value, 4))), "report clamp published wrong value");
    require(find_dp(dreo, 74)->value_int == value, "report clamp changed raw core state");
  }
  require(dreo.command_queue_.empty(), "inbound number reports emitted a corrective command");
}

void test_reconciliation_scheduler() {
  {
    Dreo dreo;
    dreo.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
    set_millis(1000);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_CHANGE_NOTIFICATION), 0, 0,
                         nullptr, 0);
    auto report = boolean_dp(1, false);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                         report.data(), report.size());
    advance_millis(101);
    dreo.process_command_queue_();
    require(dreo.command_queue_.empty() && dreo.tx_bytes.empty(),
            "unsolicited full report did not cancel notification readback");
  }
  {
    Dreo dreo;
    dreo.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
    set_millis(2000);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_CHANGE_NOTIFICATION), 0, 0,
                         nullptr, 0);
    advance_millis(50);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_CHANGE_NOTIFICATION), 0, 0,
                         nullptr, 0);
    advance_millis(99);
    dreo.process_command_queue_();
    require(dreo.tx_bytes.empty(), "notification burst did not restart the grace interval");
    advance_millis(2);
    dreo.process_command_queue_();
    require(dreo.command_queue_.size() == 1 &&
                dreo.command_queue_.front().cmd == esphome::dreo::DreoCommandType::DATAPOINT_REPORT &&
                dreo.command_queue_.front().payload.empty(),
            "unfulfilled notification did not serialize an empty 0x07");
    require(count_serialized_command(dreo.tx_bytes, 0x07) == 1,
            "notification reconciliation did not send exactly one first attempt");
    advance_millis(301);
    dreo.process_command_queue_();
    require(count_serialized_command(dreo.tx_bytes, 0x07) == 2,
            "notification reconciliation did not make its one bounded retry");
    advance_millis(301);
    dreo.process_command_queue_();
    require(dreo.command_queue_.empty() && !dreo.expected_response_.has_value(),
            "notification reconciliation remained queued after two attempts");
    require(count_serialized_command(dreo.tx_bytes, 0x07) == 2,
            "notification reconciliation exceeded two attempts");
  }
  {
    Dreo dreo;
    dreo.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
    dreo.add_transition_datapoint(1);
    auto off = boolean_dp(1, false);
    dreo.handle_datapoints_(off.data(), off.size());
    set_millis(3000);
    require(dreo.force_set_boolean_datapoint_value(1, true), "transition command was rejected");
    require(dreo.is_datapoint_pending(1), "transition was not tracked");
    auto matching = boolean_dp(1, true);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                         matching.data(), matching.size());
    require(!dreo.is_datapoint_pending(1), "immediate matching report did not release the transition");
    require(count_serialized_command(dreo.tx_bytes, 0x08) == 0,
            "immediate matching report emitted an unnecessary transition query");
  }
  {
    Dreo dreo;
    dreo.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
    dreo.add_transition_datapoint(1);
    auto off = boolean_dp(1, false);
    auto mode = enum_dp(4, 0);
    dreo.handle_datapoints_(off.data(), off.size());
    dreo.handle_datapoints_(mode.data(), mode.size());
    set_millis(4000);
    require(dreo.force_set_boolean_datapoint_value(1, true), "transition command was rejected");
    require(dreo.force_set_enum_datapoint_value(4, 1), "later ordinary command was rejected");
    auto mismatch = boolean_dp(1, false);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                         mismatch.data(), mismatch.size());
    require(dreo.is_datapoint_pending(1), "immediate mismatching report cleared the transition");
    require(dreo.command_queue_.size() == 2 &&
                dreo.command_queue_[0].cmd == esphome::dreo::DreoCommandType::DATAPOINT_QUERY &&
                dreo.command_queue_[1].cmd == esphome::dreo::DreoCommandType::DATAPOINT_DELIVER,
            "transition query was not prioritized ahead of a later command");
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                         mismatch.data(), mismatch.size());
    require(dreo.is_datapoint_pending(1), "report before the actual query gained transition authority");
    advance_millis(51);
    dreo.process_command_queue_();
    require(dreo.expected_response_ == esphome::dreo::DreoCommandType::DATAPOINT_REPORT &&
                count_serialized_command(dreo.tx_bytes, 0x08) == 1,
            "prioritized transition query was not serialized");
    auto authoritative = boolean_dp(1, true);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                         authoritative.data(), authoritative.size());
    require(!dreo.is_datapoint_pending(1), "matching authoritative readback did not release pending state");
    require(find_dp(dreo, 1)->value_bool, "matching authoritative readback did not publish actual state");
  }
  {
    Dreo dreo;
    dreo.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
    dreo.add_transition_datapoint(1);
    auto off = boolean_dp(1, false);
    dreo.handle_datapoints_(off.data(), off.size());
    set_millis(5000);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_CHANGE_NOTIFICATION), 0, 0,
                         nullptr, 0);
    require(dreo.force_set_boolean_datapoint_value(1, true), "transition command was rejected");
    advance_millis(301);
    dreo.process_command_queue_();
    require(dreo.command_queue_.front().cmd == esphome::dreo::DreoCommandType::DATAPOINT_QUERY,
            "missing transition report did not serialize 0x08");
    require(count_serialized_command(dreo.tx_bytes, 0x07) == 0,
            "transition work did not supersede notification reconciliation");
    auto contrary = boolean_dp(1, false);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                         contrary.data(), contrary.size());
    require(!dreo.is_datapoint_pending(1), "authoritative contrary readback did not release pending state");
    require(!find_dp(dreo, 1)->value_bool, "contrary readback did not publish actual state");
  }
  {
    Dreo dreo;
    dreo.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
    dreo.add_transition_datapoint(1);
    auto off = boolean_dp(1, false);
    dreo.handle_datapoints_(off.data(), off.size());
    set_millis(6000);
    require(dreo.force_set_boolean_datapoint_value(1, true), "transition command was rejected");
    advance_millis(301);
    dreo.process_command_queue_();
    auto unrelated = enum_dp(4, 2);
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                         unrelated.data(), unrelated.size());
    require(dreo.is_datapoint_pending(1), "readback omitting the transition datapoint cleared pending state");
    require(dreo.command_queue_.size() == 1 &&
                dreo.command_queue_.front().cmd == esphome::dreo::DreoCommandType::DATAPOINT_QUERY,
            "incomplete transition readback did not schedule its bounded retry");
    advance_millis(51);
    dreo.process_command_queue_();
    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                         unrelated.data(), unrelated.size());
    require(dreo.is_datapoint_pending(1), "exhausted incomplete readback invented transition state");
    require(dreo.command_queue_.empty() && !dreo.expected_response_.has_value(),
            "transition reconciliation remained queued after two complete but insufficient readbacks");
    require(count_serialized_command(dreo.tx_bytes, 0x08) == 2,
            "transition reconciliation did not stop after exactly two attempts");
  }
}

void test_transition_switch_and_off_effect() {
  Dreo dreo;
  dreo.add_transition_datapoint(3);
  auto off = boolean_dp(3, false);
  dreo.handle_datapoints_(off.data(), off.size());
  DreoSwitch transition;
  transition.set_dreo_parent(&dreo);
  transition.set_switch_id(3);
  transition.setup();
  const size_t transition_reports = transition.publish_count;
  transition.write_state(true);
  require(dreo.is_datapoint_pending(3), "transition switch command was not tracked");
  require(transition.publish_count == transition_reports,
          "transition switch published optimistically (count=" + std::to_string(transition.publish_count) + ")");

  Dreo ordinary_parent;
  DreoSwitch ordinary;
  ordinary.set_dreo_parent(&ordinary_parent);
  ordinary.set_switch_id(23);
  ordinary.setup();
  ordinary.write_state(true);
  require(ordinary.publish_count == 1 && ordinary.state, "ordinary switch lost optimistic compatibility");

  Dreo light_parent;
  DreoLight light(&light_parent);
  light.set_switch_id(9);
  light.set_effect_id(10);
  light.add_effect_mapping(0, "Indicator");
  esphome::light::LightState state(&light);
  DreoLightEffect indicator("Indicator", 0);
  state.add_effects({&indicator});
  light.setup();
  auto light_off = boolean_dp(9, false);
  light_parent.handle_datapoints_(light_off.data(), light_off.size());
  auto effect = integer_dp(10, {0});
  light_parent.handle_datapoints_(effect.data(), effect.size());
  state.flush();
  require(state.current_effect.empty(), "off-state effect report started an effect");
  require(state.invalid_effect_transition_count == 0, "off-state effect report created an invalid call");
  require(light_parent.command_queue_.empty(), "off-state effect report echoed a command");
  auto light_on = boolean_dp(9, true);
  light_parent.handle_datapoints_(light_on.data(), light_on.size());
  light_parent.handle_datapoints_(effect.data(), effect.size());
  state.flush();
  require(state.current_effect == "Indicator", "later on-state report did not publish the cached effect normally");
  require(light_parent.command_queue_.empty(), "later on/effect report echoed a command");
}

// The two opt-in hub settings must leave every existing configuration on the
// exact frame and pacing it had before they existed, and must take effect only
// where a configuration asks for them. Each half below is written so the other
// setting's value would fail it.
void test_command_spacing_and_status_byte() {
  {
    Dreo defaults;
    require(defaults.get_command_spacing() == 10, "omitted command spacing did not stay at 10 ms");
    require(defaults.get_wifi_status_second_byte() == 0, "omitted Wi-Fi status second byte did not stay at 0");
  }

  // Explicit second byte reaches the wire; the rest of the frame is unchanged.
  for (uint8_t status : {uint8_t{0}, uint8_t{3}, uint8_t{5}}) {
    Dreo configured;
    configured.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
    configured.set_wifi_status_second_byte(1);
    set_millis(10000 + status * 1000);
    if (status == 0)
      configured.send_wifi_status_off();
    else if (status == 3)
      configured.send_wifi_status_flash();
    else
      configured.send_wifi_status_solid();
    require(configured.tx_bytes == wifi_status_frame(0, status, 1),
            "configured Wi-Fi status second byte did not reach the wire");
    require(configured.command_queue_.size() == 1 &&
                configured.command_queue_.front().payload == std::vector<uint8_t>({status, 1}),
            "configured Wi-Fi status payload was not {status, 1}");
  }

  // Queue threshold. The default 10 ms release and the configured 100 ms
  // release are separated by a 51 ms observation: the default must have sent
  // the second frame by then and the configured one must not have.
  for (bool configured : {false, true}) {
    Dreo dreo;
    dreo.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
    if (configured)
      dreo.set_command_spacing(100);
    set_millis(30000);
    dreo.send_wifi_status_off();
    dreo.send_wifi_status_flash();
    const auto first = wifi_status_frame(0, 0);
    require(dreo.tx_bytes == first && dreo.command_queue_.size() == 2,
            "spacing fixture did not queue the second status behind the first");

    dreo.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::WIFI_STATE), 0, 0, nullptr, 0);
    advance_millis(51);
    dreo.process_command_queue_();
    if (configured) {
      require(dreo.tx_bytes == first,
              "100 ms spacing released the second status after only 51 ms");
      advance_millis(60);
      dreo.process_command_queue_();
    }
    auto expected = first;
    const auto second = wifi_status_frame(1, 3);
    expected.insert(expected.end(), second.begin(), second.end());
    require(dreo.tx_bytes == expected, "second status was not released once the spacing elapsed");
  }
}

void test_wifi_status_senders() {
  for (uint8_t status : {uint8_t{0}, uint8_t{3}, uint8_t{5}}) {
    Dreo dreo;
    dreo.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
    set_millis(10000 + status * 1000);
    if (status == 0)
      dreo.send_wifi_status_off();
    else if (status == 3)
      dreo.send_wifi_status_flash();
    else
      dreo.send_wifi_status_solid();
    require(dreo.tx_bytes == wifi_status_frame(0, status), "named status wrapper frame was not exact");
    require(dreo.command_queue_.size() == 1 && dreo.command_queue_.front().payload == std::vector<uint8_t>({status, 0}),
            "named status wrapper did not retain the fixed-zero payload");
  }

  Dreo serialized;
  serialized.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
  set_millis(30000);
  serialized.send_wifi_status_off();
  serialized.send_wifi_status_flash();
  require(serialized.tx_bytes == wifi_status_frame(0, 0) && serialized.command_queue_.size() == 2,
          "queued status bypassed active queue work");
  serialized.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::WIFI_STATE), 0, 0, nullptr, 0);
  advance_millis(51);
  serialized.process_command_queue_();
  auto expected = wifi_status_frame(0, 0);
  const auto second = wifi_status_frame(1, 3);
  expected.insert(expected.end(), second.begin(), second.end());
  require(serialized.tx_bytes == expected, "queued status was not serialized after acknowledgement");

  const uint8_t unexpected{1};
  serialized.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::WIFI_STATE), 0, 1, &unexpected, 1);
  require(serialized.expected_response_.has_value() && serialized.command_queue_.size() == 1,
          "non-empty status acknowledgement was accepted");
  const size_t sent_bytes = serialized.tx_bytes.size();
  advance_millis(301);
  serialized.process_command_queue_();
  require(serialized.command_queue_.empty() && !serialized.expected_response_.has_value(),
          "status acknowledgement timeout did not release the queue");
  require(serialized.tx_bytes.size() == sent_bytes, "status acknowledgement timeout retried the command");
}

void test_diagnostic_full_report_request() {
  const auto full_report = load_fixture("full_report");

  Dreo rejected;
  require(!rejected.request_full_datapoint_report_once(), "request was accepted before initialization");
  rejected.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
  require(!rejected.request_full_datapoint_report_once(), "request was accepted without a known table baseline");
  rejected.handle_datapoints_(full_report.data(), full_report.size());
  rejected.send_wifi_status_off();
  require(!rejected.request_full_datapoint_report_once(), "request bypassed active queue work");

  Dreo pending;
  pending.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
  pending.handle_datapoints_(full_report.data(), full_report.size());
  pending.pending_transitions_.push_back({});
  require(!pending.request_full_datapoint_report_once(), "request bypassed pending transition state");
  pending.pending_transitions_.clear();
  pending.reconciliation_route_ = esphome::dreo::DreoReconciliationRoute::NOTIFICATION;
  require(!pending.request_full_datapoint_report_once(), "request bypassed active reconciliation state");
  pending.reconciliation_route_ = esphome::dreo::DreoReconciliationRoute::NONE;
  pending.notification_reconciliation_due_ = 1;
  require(!pending.request_full_datapoint_report_once(), "request bypassed scheduled notification state");

  Dreo completed;
  completed.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
  completed.handle_datapoints_(full_report.data(), full_report.size());
  set_millis(40000);
  require(completed.request_full_datapoint_report_once(), "idle full-table request was rejected");
  const std::vector<uint8_t> expected{0x55, 0xAA, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x06};
  require(completed.tx_bytes == expected, "full-table request did not emit one exact empty 0x07 frame");
  require(completed.reconciliation_route_ == esphome::dreo::DreoReconciliationRoute::NONE &&
              completed.reconciliation_attempts_ == 0,
          "full-table request changed reconciliation state");

  const auto incomplete = boolean_dp(1, true);
  completed.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                            incomplete.data(), incomplete.size());
  require(completed.expected_response_.has_value() && completed.command_queue_.size() == 1,
          "incomplete table was accepted as the requested response");
  auto wrong_type = full_report;
  wrong_type[2] = 0xFF;
  completed.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                            wrong_type.data(), wrong_type.size());
  require(completed.expected_response_.has_value() && completed.command_queue_.size() == 1,
          "table with a changed cached datapoint contract was accepted");
  completed.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                            full_report.data(), full_report.size());
  require(completed.command_queue_.empty() && !completed.expected_response_.has_value(),
          "complete table did not release the diagnostic request");
  require(completed.reconciliation_route_ == esphome::dreo::DreoReconciliationRoute::NONE &&
              completed.reconciliation_attempts_ == 0,
          "completed full-table request changed reconciliation state");

  Dreo timed_out;
  timed_out.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
  timed_out.handle_datapoints_(full_report.data(), full_report.size());
  set_millis(50000);
  require(timed_out.request_full_datapoint_report_once(), "timeout fixture request was rejected");
  const size_t sent_bytes = timed_out.tx_bytes.size();
  advance_millis(301);
  timed_out.process_command_queue_();
  require(timed_out.command_queue_.empty() && !timed_out.expected_response_.has_value(),
          "full-table timeout did not release the queue");
  require(timed_out.tx_bytes.size() == sent_bytes, "full-table timeout retried the request");
  require(timed_out.reconciliation_route_ == esphome::dreo::DreoReconciliationRoute::NONE &&
              timed_out.reconciliation_attempts_ == 0,
          "full-table timeout changed reconciliation state");

  Dreo notification;
  notification.init_state_ = esphome::dreo::DreoInitState::INIT_DONE;
  notification.handle_datapoints_(full_report.data(), full_report.size());
  set_millis(60000);
  require(notification.request_full_datapoint_report_once(), "notification overlap fixture request was rejected");
  notification.handle_command_(
      static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_CHANGE_NOTIFICATION), 0, 0, nullptr, 0);
  const uint32_t notification_due = notification.notification_reconciliation_due_;
  require(notification.reconciliation_route_ == esphome::dreo::DreoReconciliationRoute::NOTIFICATION &&
              notification_due != 0,
          "notification overlap did not establish normal reconciliation state");
  notification.handle_command_(static_cast<uint8_t>(esphome::dreo::DreoCommandType::DATAPOINT_REPORT), 0, 0,
                               full_report.data(), full_report.size());
  require(notification.reconciliation_route_ == esphome::dreo::DreoReconciliationRoute::NOTIFICATION &&
              notification.notification_reconciliation_due_ == notification_due,
          "diagnostic response cancelled normal notification reconciliation");
}

void run_fixed() {
  test_core_parser_and_writer();
  test_light();
  test_text();
  test_lock();
  test_guard();
  test_subordinate_control_policy();
  test_ceiling_fan_coordinator();
  test_ceiling_fan_ambient_presets();
  test_existing_platform_type_safety();
  test_masked_binary_sensor_and_number_clamp();
  test_reconciliation_scheduler();
  test_transition_switch_and_off_effect();
  test_wifi_status_senders();
  test_command_spacing_and_status_byte();
  test_diagnostic_full_report_request();
  std::cout << "PASS: actual C++ sources satisfy parser, light, text, lock, guard, and legacy regressions\n";
}
#endif

}  // namespace

int main(int argc, char **argv) {
  const std::string mode = argc > 1 ? argv[1] : "fixed";
  if (mode == "marker") {
    test_marker_default();
    std::cout << "PASS: product-source marker assertion\n";
    return 0;
  }
  if (mode == "baseline") {
    run_baseline();
    return 0;
  }
#ifdef DREO_FIXED_TESTS
  if (mode == "fixed") {
    run_fixed();
    return 0;
  }
#endif
  std::cerr << "unknown or unavailable mode: " << mode << '\n';
  return 2;
}
