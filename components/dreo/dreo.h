#pragma once

#include <cinttypes>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"


namespace esphome::dreo {

// Minimum gap between consecutive transmissions, in milliseconds, when a
// configuration does not choose its own. Products whose stock bridge paced
// frames more slowly raise it through the hub's `command_spacing` option.
static constexpr uint32_t DEFAULT_COMMAND_SPACING_MS = 10;

// Second payload byte of the module status frame when a configuration does not
// choose its own. Products whose stock bridge sent a different value set it
// through the hub's `wifi_status_second_byte` option.
static constexpr uint8_t DEFAULT_WIFI_STATUS_SECOND_BYTE = 0x00;

enum class DreoDatapointType : uint8_t {
  // RAW = 0x00,      // variable length
  BOOLEAN = 0x01,  // 1 byte (0/1)
  INTEGER = 0x02,  // 1/2/4 bytes
  STRING = 0x03,   // variable length
  ENUM = 0x04,     // 1 byte
  // BITMASK = 0x05,  // 1/2/4 bytes
};

struct DreoDatapoint {
  uint8_t id;
  DreoDatapointType type;
  size_t len;
  union {
    bool value_bool;
    int32_t value_int;
    uint32_t value_uint;
    uint8_t value_enum;
  };
  std::string value_string;
};

struct DreoDatapointCommand {
  uint8_t datapoint_id;
  DreoDatapointType type;
  uint32_t value_uint{0};
  std::string value_string;
};

struct DreoPendingTransition {
  DreoDatapointCommand command;
};

struct DreoDatapointListener {
  uint8_t datapoint_id;
  std::function<void(DreoDatapoint)> on_datapoint;
};

enum class DreoCommandType : uint8_t {
  HEARTBEAT = 0x00,
  PRODUCT_QUERY = 0x01,
  WIFI_STATE = 0x03,
  WIFI_RESET = 0x04,
  DATAPOINT_DELIVER = 0x06,
  DATAPOINT_REPORT = 0x07,
  DATAPOINT_QUERY = 0x08,
  DATAPOINT_CHANGE_NOTIFICATION = 0x0E, // Can't decipher this so we'll ignore it
};


// INIT_HEARTBEAT->INIT_PRODUCT->INIT_DATAPOINT->INIT_DONE

enum class DreoInitState : uint8_t {
  INIT_HEARTBEAT = 0x00,
  INIT_PRODUCT,
  INIT_DATAPOINT,
  INIT_DONE,
};

enum class DreoReconciliationRoute : uint8_t {
  NONE = 0,
  NOTIFICATION,
  TRANSITION,
};

struct DreoCommand {
  DreoCommandType cmd;
  std::vector<uint8_t> payload;
  DreoReconciliationRoute reconciliation_route{DreoReconciliationRoute::NONE};
  bool diagnostic_full_report{false};
};

class Dreo final : public Component, public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::LATE; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void register_listener(uint8_t datapoint_id, const std::function<void(DreoDatapoint)> &func);
  bool set_boolean_datapoint_value(uint8_t datapoint_id, bool value);
  bool set_integer_datapoint_value(uint8_t datapoint_id, uint32_t value);
  bool set_enum_datapoint_value(uint8_t datapoint_id, uint8_t value);
  bool set_string_datapoint_value(uint8_t datapoint_id, const std::string &value);
  bool force_set_boolean_datapoint_value(uint8_t datapoint_id, bool value);
  bool force_set_integer_datapoint_value(uint8_t datapoint_id, uint32_t value);
  bool force_set_enum_datapoint_value(uint8_t datapoint_id, uint8_t value);
  bool force_set_string_datapoint_value(uint8_t datapoint_id, const std::string &value);
  void send_wifi_status_off();
  void send_wifi_status_flash();
  void send_wifi_status_solid();
  bool request_full_datapoint_report_once();
  optional<bool> get_boolean_datapoint_value(uint8_t datapoint_id);
  bool is_datapoint_pending(uint8_t datapoint_id) const;
  void set_integer_command_width(uint8_t datapoint_id, uint8_t width);
  void set_allow_sub_entity_control_while_off(bool allow) {
    this->allow_sub_entity_control_while_off_ = allow;
  }
  bool allow_sub_entity_control_while_off() const { return this->allow_sub_entity_control_while_off_; }
  void set_command_authorizer(const std::function<bool(const DreoDatapointCommand &)> &authorizer) {
    this->command_authorizer_ = authorizer;
  }
  void add_transition_datapoint(uint8_t datapoint_id) { this->transition_datapoints_.push_back(datapoint_id); }
  DreoInitState get_init_state();
  void add_ignore_mcu_update_on_datapoints(uint8_t ignore_mcu_update_on_datapoints) {
    this->ignore_mcu_update_on_datapoints_.push_back(ignore_mcu_update_on_datapoints);
  }
  void set_command_datapoint_marker(uint8_t marker) { this->command_datapoint_marker_ = marker; }
  void set_command_spacing(uint32_t milliseconds) { this->command_spacing_ = milliseconds; }
  uint32_t get_command_spacing() const { return this->command_spacing_; }
  void set_wifi_status_second_byte(uint8_t value) { this->wifi_status_second_byte_ = value; }
  uint8_t get_wifi_status_second_byte() const { return this->wifi_status_second_byte_; }
  template<typename F> void add_on_initialized_callback(F &&callback) {
    this->initialized_callback_.add(std::forward<F>(callback));
  }

 private:
  void send_wifi_status_(uint8_t status);

 protected:
  void handle_char_(uint8_t c);
  void handle_datapoints_(const uint8_t *buffer, size_t len, bool authoritative_transition_report = false);
  optional<DreoDatapoint> get_datapoint_(uint8_t datapoint_id);
  optional<uint8_t> integer_command_width_(uint8_t datapoint_id) const;
  bool validate_message_();

  void handle_command_(uint8_t command, uint8_t version, uint8_t sequence, const uint8_t *buffer, size_t len);
  void send_raw_command_(DreoCommand command);
  void process_command_queue_();
  void send_command_(const DreoCommand &command);
  void send_empty_command_(DreoCommandType command);
  bool set_numeric_datapoint_value_(uint8_t datapoint_id, DreoDatapointType datapoint_type, uint32_t value,
                                    uint8_t length, bool forced);
  bool set_string_datapoint_value_(uint8_t datapoint_id, const std::string &value, bool forced);
  bool send_datapoint_command_(const DreoDatapointCommand &command, std::vector<uint8_t> data);
  bool authorize_command_(const DreoDatapointCommand &command);
  void record_pending_transition_(const DreoDatapointCommand &command);
  void clear_confirmed_transition_(const DreoDatapoint &datapoint, bool authoritative);
  bool datapoint_confirms_command_(const DreoDatapoint &datapoint, const DreoDatapointCommand &command) const;
  void schedule_notification_reconciliation_();
  void schedule_transition_reconciliation_();
  void queue_reconciliation_request_(DreoReconciliationRoute route);
  void cancel_reconciliation_(DreoReconciliationRoute route);
  bool has_pending_transitions_() const { return !this->pending_transitions_.empty(); }
  bool response_contains_complete_table_(const uint8_t *buffer, size_t len) const;

  DreoInitState init_state_ = DreoInitState::INIT_HEARTBEAT;
  bool init_failed_{false};
  int init_retries_{0};
  uint8_t protocol_version_ = -1;
  uint32_t last_command_timestamp_ = 0;
  uint32_t last_rx_char_timestamp_ = 0;
  std::string product_;
  std::vector<DreoDatapointListener> listeners_;
  std::vector<DreoDatapoint> datapoints_;
  std::vector<uint8_t> rx_message_;
  std::vector<uint8_t> ignore_mcu_update_on_datapoints_{};
  std::vector<DreoCommand> command_queue_;
  optional<DreoCommandType> expected_response_{};
  CallbackManager<void()> initialized_callback_{};
  uint8_t sequence_ = 0;
  uint8_t command_datapoint_marker_ = 0;
  uint32_t command_spacing_{DEFAULT_COMMAND_SPACING_MS};
  uint8_t wifi_status_second_byte_{DEFAULT_WIFI_STATUS_SECOND_BYTE};
  std::vector<std::pair<uint8_t, uint8_t>> integer_command_widths_{};
  bool allow_sub_entity_control_while_off_{false};
  std::function<bool(const DreoDatapointCommand &)> command_authorizer_{};
  std::vector<uint8_t> transition_datapoints_{};
  std::vector<DreoPendingTransition> pending_transitions_{};
  uint8_t last_rejected_datapoint_{0xFF};
  uint32_t last_rejection_log_timestamp_{0};
  DreoReconciliationRoute reconciliation_route_{DreoReconciliationRoute::NONE};
  uint32_t notification_reconciliation_due_{0};
  uint8_t reconciliation_attempts_{0};
};

}  // namespace esphome::dreo
