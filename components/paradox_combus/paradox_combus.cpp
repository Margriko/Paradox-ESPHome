#include "paradox_combus.h"

#include "esphome/core/log.h"

namespace esphome {
namespace paradox_combus {

static const char *const TAG = "paradox_combus";


uint32_t ParadoxAlarmControlPanel::get_supported_features() const {
  uint32_t features = 0;

  if (this->parent_ == nullptr) {
    return features;
  }

  if (this->parent_->supports_arm_home()) {
    features |= 1U;
  }
  if (this->parent_->supports_arm_away()) {
    features |= 1U << 1;
  }
  if (this->parent_->supports_arm_night()) {
    features |= 1U << 2;
  }

  return features;
}

bool ParadoxAlarmControlPanel::get_requires_code() const {
  return this->parent_ != nullptr && this->parent_->requires_code();
}

bool ParadoxAlarmControlPanel::get_requires_code_to_arm() const {
  return this->parent_ != nullptr && this->parent_->requires_code();
}

void ParadoxAlarmControlPanel::control(const alarm_control_panel::AlarmControlPanelCall &call) {
  if (this->parent_ == nullptr || !call.get_state().has_value()) {
    return;
  }


  switch (*call.get_state()) {
    case alarm_control_panel::ACP_STATE_DISARMED:
      this->parent_->request_disarm(call.get_code());
      break;
    case alarm_control_panel::ACP_STATE_ARMED_HOME:
      this->parent_->request_arm_home(call.get_code());
      break;
    case alarm_control_panel::ACP_STATE_ARMED_AWAY:
      this->parent_->request_arm_away(call.get_code());
      break;
    case alarm_control_panel::ACP_STATE_ARMED_NIGHT:
      this->parent_->request_arm_night(call.get_code());
      break;
    default:
      ESP_LOGW(TAG, "Requested alarm state is not currently mapped to COMBUS write sequence");
      break;
  }
}

void ParadoxCombusComponent::register_zone_sensor(uint8_t zone, binary_sensor::BinarySensor *sensor) {
  if (zone < 1 || zone > 32) {
    ESP_LOGW(TAG, "Ignoring zone %u (valid range is 1..32)", zone);
    return;
  }
  this->zone_sensors_[zone - 1] = sensor;
}

void ParadoxCombusComponent::capture_combus_bits_() {
  if (this->clk_pin_ == nullptr || this->read_pin_ == nullptr) {
    return;
  }

  const unsigned long now = micros();
  const bool clk_state = this->clk_pin_->digital_read();

  if (this->last_clk_state_ && !clk_state) {
    this->last_clk_signal_ = now;
    this->pending_sample_at_ = now + 150;
    this->sample_pending_ = true;
  }
  this->last_clk_state_ = clk_state;

  if (!this->sample_pending_ || static_cast<long>(now - this->pending_sample_at_) < 0) {
    return;
  }

  this->sample_pending_ = false;

  this->bus_message_.push_back(this->read_pin_->digital_read() ? '0' : '1');

  if (this->bus_message_.length() > 200) {
    this->bus_message_.clear();
    ESP_LOGW(TAG, "Dropped COMBUS frame buffer due to overflow");
    return;
  }
}

void ParadoxCombusComponent::queue_write_sequence_(const std::vector<uint8_t> &sequence) {
  if (sequence.empty()) {
    ESP_LOGW(TAG, "Cannot queue empty COMBUS write sequence");
    return;
  }

  for (uint8_t value : sequence) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      this->tx_bits_.push_back((value >> bit) & 0x01);
    }
  }

  ESP_LOGD(TAG, "Queued COMBUS write sequence (%u bytes, %u bits pending)", static_cast<unsigned>(sequence.size()),
           static_cast<unsigned>(this->tx_bits_.size()));
}

std::vector<uint8_t> ParadoxCombusComponent::code_to_sequence_(const optional<std::string> &code) const {
  std::vector<uint8_t> sequence;
  if (!code.has_value()) {
    return sequence;
  }

  for (char value : *code) {
    if (value >= '0' && value <= '9') {
      sequence.push_back(static_cast<uint8_t>(value));
    }
  }

  return sequence;
}

void ParadoxCombusComponent::request_disarm(const optional<std::string> &code) {
  if (!this->disarm_sequence_.empty()) {
    this->queue_write_sequence_(this->disarm_sequence_);
    return;
  }

  this->queue_write_sequence_(this->code_to_sequence_(code));
}

void ParadoxCombusComponent::request_arm_home(const optional<std::string> &code) {
  std::vector<uint8_t> sequence = this->code_to_sequence_(code);
  sequence.insert(sequence.end(), this->arm_home_sequence_.begin(), this->arm_home_sequence_.end());
  this->queue_write_sequence_(sequence);
}

void ParadoxCombusComponent::request_arm_away(const optional<std::string> &code) {
  std::vector<uint8_t> sequence = this->code_to_sequence_(code);
  sequence.insert(sequence.end(), this->arm_away_sequence_.begin(), this->arm_away_sequence_.end());
  this->queue_write_sequence_(sequence);
}

void ParadoxCombusComponent::request_arm_night(const optional<std::string> &code) {
  std::vector<uint8_t> sequence = this->code_to_sequence_(code);
  sequence.insert(sequence.end(), this->arm_night_sequence_.begin(), this->arm_night_sequence_.end());
  this->queue_write_sequence_(sequence);
}

void ParadoxCombusComponent::process_pending_bus_writes_() {
  if (this->clk_pin_ == nullptr || this->write_pin_ == nullptr) {
    return;
  }

  if (this->tx_bits_.empty()) {
    if (this->tx_drive_low_) {
      this->write_pin_->pin_mode(gpio::FLAG_INPUT);
      this->tx_drive_low_ = false;
    }
    return;
  }

  const bool clk_state = this->clk_pin_->digital_read();
  if (this->last_clk_state_ && !clk_state) {
    const bool write_bit = this->tx_bits_.front();
    this->tx_bits_.pop_front();

    // COMBUS uses open collector signalling: logical '1' is driven low, logical '0' is release.
    if (write_bit) {
      this->write_pin_->pin_mode(gpio::FLAG_OUTPUT);
      this->write_pin_->digital_write(false);
      this->tx_drive_low_ = true;
    } else {
      this->write_pin_->pin_mode(gpio::FLAG_INPUT);
      this->tx_drive_low_ = false;
    }
  }
}

void ParadoxCombusComponent::connect_combus_() {
  if (this->clk_pin_ == nullptr || this->read_pin_ == nullptr) {
    ESP_LOGE(TAG, "clk_pin and read_pin are required");
    return;
  }

  this->clk_pin_->pin_mode(gpio::FLAG_INPUT);
  this->read_pin_->pin_mode(gpio::FLAG_INPUT);
  if (this->write_pin_ == nullptr) {
    this->write_pin_ = this->read_pin_;
  }
  this->write_pin_->pin_mode(gpio::FLAG_INPUT);

  this->last_clk_state_ = this->clk_pin_->digital_read();
  this->sample_pending_ = false;
  this->last_clk_signal_ = micros();
  this->bus_message_.clear();

  this->combus_connection_status_ = true;
  ESP_LOGI(TAG, "COMBUS initialized in polling mode");
}

void ParadoxCombusComponent::disconnect_combus_() {
  this->sample_pending_ = false;
  this->tx_bits_.clear();
  if (this->write_pin_ != nullptr) {
    this->write_pin_->pin_mode(gpio::FLAG_INPUT);
  }
  this->tx_drive_low_ = false;
  this->bus_message_.clear();
  this->combus_connection_status_ = false;
}

void ParadoxCombusComponent::setup() { this->connect_combus_(); }


void ParadoxCombusComponent::publish_alarm_control_panel_state_(alarm_control_panel::AlarmControlPanelState state) {
  if (this->alarm_control_panel_ != nullptr) {
    this->alarm_control_panel_->publish_state(state);
  }
}

void ParadoxCombusComponent::publish_zone_state_(uint8_t zone, bool open) {
  if (zone < 1 || zone > 32) {
    return;
  }
  auto *sensor = this->zone_sensors_[zone - 1];
  if (sensor != nullptr) {
    sensor->publish_state(open);
  }
}

void ParadoxCombusComponent::loop() {
  if (!this->get_combus_connection_status_()) {
    this->publish_alarm_control_panel_state_(alarm_control_panel::ACP_STATE_DISARMED);
    for (int i = 0; i < 32; i++) {
      this->publish_zone_state_(i + 1, false);
    }
    return;
  }

  this->capture_combus_bits_();
  this->process_pending_bus_writes_();

  if (!this->check_clock_idle_() || this->bus_message_.length() < 2) {
    return;
  }

  std::string message = this->bus_message_;
  this->bus_message_.clear();

  this->decode_message_(message);
}

void ParadoxCombusComponent::process_zone_status_(const std::string &msg) {
  if (msg.length() < 17 + (32 * 2)) {
    ESP_LOGW(TAG, "Zone frame too short: %u bits", msg.length());
    return;
  }

  for (int i = 0; i < 32; i++) {
    bool open = msg[17 + (i * 2)] == '1';
    this->publish_zone_state_(i + 1, open);
  }
}

void ParadoxCombusComponent::process_alarm_status_(const std::string &msg) {
  if (msg.length() <= ((8 * 7) + 1)) {
    ESP_LOGW(TAG, "Alarm frame too short: %u bits", msg.length());
    return;
  }

  if (msg[((8 * 7) + 1)] == '0') {
    if (msg[((8 * 2) + 5)] == '1') {
      this->publish_alarm_control_panel_state_(alarm_control_panel::ACP_STATE_ARMED_HOME);
    }
    if (msg[((8 * 6) + 5)] == '1') {
      this->publish_alarm_control_panel_state_(alarm_control_panel::ACP_STATE_ARMED_NIGHT);
    }
    if (msg[((8 * 2) + 1)] == '1') {
      if (msg[((8 * 2) + 0)] == '1') {
        this->publish_alarm_control_panel_state_(alarm_control_panel::ACP_STATE_ARMING);
      } else if (msg[((8 * 2) + 0)] == '0') {
        this->publish_alarm_control_panel_state_(alarm_control_panel::ACP_STATE_TRIGGERED);
      } else {
        this->publish_alarm_control_panel_state_(alarm_control_panel::ACP_STATE_ARMED_AWAY);
      }
    }
  } else {
    this->publish_alarm_control_panel_state_(alarm_control_panel::ACP_STATE_DISARMED);
  }
}

void ParadoxCombusComponent::decode_message_(std::string &msg) {
  if (msg.length() < 8) {
    return;
  }

  int cmd = get_int_from_string_(msg.substr(0, 8));

  if (cmd == 0xD0 || cmd == 0xD1) {
    msg = msg.substr(0, msg.length() - (4 * 8) - 1);
    if (!check_crc_(msg)) {
      return;
    }
  } else {
    if (!check_crc_(msg)) {
      return;
    }
  }

  switch (cmd) {
    case 0xD0:
      process_zone_status_(msg);
      break;
    case 0xD1:
      process_alarm_status_(msg);
      break;
    default:
      break;
  }
}

uint8_t ParadoxCombusComponent::crc8_(const uint8_t *addr, uint8_t len) {
  uint8_t crc = 0;

  for (uint8_t i = 0; i < len; i++) {
    uint8_t inbyte = addr[i];
    for (uint8_t j = 0; j < 8; j++) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix)
        crc ^= 0x8C;

      inbyte >>= 1;
    }
  }
  return crc;
}

uint8_t ParadoxCombusComponent::check_crc_(const std::string &st) {
  int bytes = (st.length()) / 8;
  if (bytes < 2) {
    return false;
  }
  const std::vector<uint8_t> binary_str = str_to_bin_array_(st);

  const uint8_t crc = binary_str[bytes - 1];
  const uint8_t calc_crc_byte = crc8_(binary_str.data(), bytes - 1);
  return calc_crc_byte == crc;
}

bool ParadoxCombusComponent::check_clock_idle_() {
  unsigned long current_micros = micros();
  long idletime = (current_micros - this->last_clk_signal_);

  if (idletime > 8000) {
    return true;
  } else {
    return false;
  }
}

unsigned int ParadoxCombusComponent::get_int_from_string_(const std::string &str) {
  int r = 0;
  int length = str.length();

  for (int j = 0; j < length; j++) {
    if (str[length - j - 1] == '1') {
      r |= 1 << j;
    }
  }

  return r;
}

std::vector<uint8_t> ParadoxCombusComponent::str_to_bin_array_(const std::string &st) {
  const int bytes = st.length() / 8;
  std::vector<uint8_t> data(bytes);

  for (int i = 0; i < bytes; i++) {
    const std::string val = st.substr(i * 8, 8);
    data[i] = get_int_from_string_(val);
  }

  return data;
}

}  // namespace paradox_combus
}  // namespace esphome
