#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/alarm_control_panel/alarm_control_panel.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

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
  void set_read_pin(InternalGPIOPin *pin) { this->read_pin_ = pin; }
  void set_write_pin(GPIOPin *pin) { this->write_pin_ = pin; }
  void set_frame_idle_us(uint32_t frame_idle_us) { this->frame_idle_us_ = frame_idle_us; }

  void register_zone_sensor(uint8_t zone, binary_sensor::BinarySensor *sensor);
  void set_alarm_control_panel(ParadoxAlarmControlPanel *panel) { this->alarm_control_panel_ = panel; }

  void set_disarm_sequence(const std::vector<uint8_t> &sequence) { this->disarm_sequence_ = sequence; }
  void set_arm_home_sequence(const std::vector<uint8_t> &sequence) { this->arm_home_sequence_ = sequence; }
  void set_arm_away_sequence(const std::vector<uint8_t> &sequence) { this->arm_away_sequence_ = sequence; }
  void set_arm_night_sequence(const std::vector<uint8_t> &sequence) { this->arm_night_sequence_ = sequence; }

  void request_disarm(const optional<std::string> &code);
  void request_arm_home(const optional<std::string> &code);
  void request_arm_away(const optional<std::string> &code);
  void request_arm_night(const optional<std::string> &code);

  bool supports_disarm() const { return !this->disarm_sequence_.empty(); }
  bool supports_arm_home() const { return !this->arm_home_sequence_.empty(); }
  bool supports_arm_away() const { return !this->arm_away_sequence_.empty(); }
  bool supports_arm_night() const { return !this->arm_night_sequence_.empty(); }
  bool requires_code() const { return false; }
  std::vector<uint8_t> code_to_sequence_(const optional<std::string> &code) const;

  void setup() override;
  void loop() override;

 protected:
  void connect_combus_();
  void disconnect_combus_();
  bool get_combus_connection_status_() const { return this->combus_connection_status_; }

  void process_zone_status_(const std::string &msg);
  void process_alarm_status_(const std::string &msg);
  void decode_message_(std::string &msg);

  uint8_t crc8_(const uint8_t *addr, uint8_t len);
  uint8_t check_crc_(const std::string &st);
  bool check_clock_idle_();
  unsigned int get_int_from_string_(const std::string &str);
  std::vector<uint8_t> str_to_bin_array_(const std::string &st);

  void publish_zone_state_(uint8_t zone, bool open);
  void publish_alarm_control_panel_state_(alarm_control_panel::AlarmControlPanelState state);

  void capture_combus_bits_();
  void process_pending_bus_writes_();
  void queue_write_sequence_(const std::vector<uint8_t> &sequence);
  void log_bus_diagnostics_();
  void track_frame_length_(size_t frame_bits);

  InternalGPIOPin *clk_pin_{nullptr};
  InternalGPIOPin *read_pin_{nullptr};
  GPIOPin *write_pin_{nullptr};

  std::array<binary_sensor::BinarySensor *, 32> zone_sensors_{};
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
  unsigned long last_diag_log_at_{0};
  uint32_t clock_falling_edges_{0};
  uint32_t sampled_bits_{0};
  uint32_t decoded_frames_{0};
  uint32_t crc_drop_frames_{0};
  uint32_t overflow_drop_frames_{0};
  std::array<uint32_t, 8> frame_len_hist_{};
  uint32_t short_frame_drops_{0};
  uint32_t malformed_d1_d0_frames_{0};
  std::string last_crc_fail_preview_;
  uint8_t last_crc_fail_cmd_{0};
  uint32_t frame_idle_us_{25000};
  bool combus_connection_status_{false};

};

}  // namespace paradox_combus
}  // namespace esphome
