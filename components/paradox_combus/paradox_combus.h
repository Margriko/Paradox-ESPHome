#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/alarm_control_panel/alarm_control_panel.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace esphome {
namespace paradox_combus {

class ParadoxCombusComponent;

class ParadoxAlarmControlPanel : public alarm_control_panel::AlarmControlPanel {
 public:
  void set_parent(ParadoxCombusComponent *parent) { this->parent_ = parent; }

 protected:
  uint32_t get_supported_features() const override;
  bool get_requires_code() const override;
  bool get_requires_code_to_arm() const override;
  void control(const alarm_control_panel::AlarmControlPanelCall &call) override;
  ParadoxCombusComponent *parent_{nullptr};
};

class ParadoxZoneBinarySensor : public binary_sensor::BinarySensor {};

class ParadoxCombusComponent : public Component {
 public:
  void set_clk_pin(InternalGPIOPin *pin) { this->clk_pin_ = pin; }
  void set_dta_pin(InternalGPIOPin *pin) { this->dta_pin_ = pin; }

  void register_zone_sensor(uint8_t zone, binary_sensor::BinarySensor *sensor);
  void set_alarm_status_sensor(text_sensor::TextSensor *sensor) { this->alarm_status_sensor_ = sensor; }
  void set_alarm_control_panel(ParadoxAlarmControlPanel *panel) { this->alarm_control_panel_ = panel; }

  void set_disarm_sequence(const std::vector<uint8_t> &sequence) { this->disarm_sequence_ = sequence; }
  void set_arm_home_sequence(const std::vector<uint8_t> &sequence) { this->arm_home_sequence_ = sequence; }
  void set_arm_away_sequence(const std::vector<uint8_t> &sequence) { this->arm_away_sequence_ = sequence; }
  void set_arm_night_sequence(const std::vector<uint8_t> &sequence) { this->arm_night_sequence_ = sequence; }

  void request_disarm();
  void request_arm_home();
  void request_arm_away();
  void request_arm_night();

  bool supports_disarm() const { return !this->disarm_sequence_.empty(); }
  bool supports_arm_home() const { return !this->arm_home_sequence_.empty(); }
  bool supports_arm_away() const { return !this->arm_away_sequence_.empty(); }
  bool supports_arm_night() const { return !this->arm_night_sequence_.empty(); }

  void setup() override;
  void loop() override;

 protected:
  void connect_combus_();
  void disconnect_combus_();
  bool get_combus_connection_status_() const { return this->combus_connection_status_; }

  void process_zone_status_(String &msg);
  void process_alarm_status_(String &msg);
  void decode_message_(String &msg);

  uint8_t crc8_(uint8_t *addr, uint8_t len);
  uint8_t check_crc_(String &st);
  bool check_clock_idle_();
  unsigned int get_int_from_string_(String str);
  uint8_t *str_to_bin_array_(String &st);

  void publish_alarm_state_(const std::string &value);
  void publish_zone_state_(uint8_t zone, bool open);
  void publish_alarm_control_panel_state_(alarm_control_panel::AlarmControlPanelState state);

  void capture_combus_bits_();
  void process_pending_bus_writes_();
  void queue_write_sequence_(const std::vector<uint8_t> &sequence);

  InternalGPIOPin *clk_pin_{nullptr};
  InternalGPIOPin *dta_pin_{nullptr};

  std::array<binary_sensor::BinarySensor *, 32> zone_sensors_{};
  text_sensor::TextSensor *alarm_status_sensor_{nullptr};
  ParadoxAlarmControlPanel *alarm_control_panel_{nullptr};

  std::vector<uint8_t> disarm_sequence_{};
  std::vector<uint8_t> arm_home_sequence_{};
  std::vector<uint8_t> arm_away_sequence_{};
  std::vector<uint8_t> arm_night_sequence_{};

  std::deque<bool> tx_bits_{};
  bool tx_drive_low_{false};

  std::string bus_message_;
  bool last_clk_state_{true};
  bool sample_pending_{false};
  unsigned long pending_sample_at_{0};
  unsigned long last_clk_signal_{0};
  bool combus_connection_status_{false};

  const std::string STATUS_UNAVAILABLE = "unavailable";
  const std::string STATUS_ARM = "armed_away";
  const std::string STATUS_SLEEP = "armed_night";
  const std::string STATUS_STAY = "armed_home";
  const std::string STATUS_OFF = "disarmed";
};

}  // namespace paradox_combus
}  // namespace esphome
