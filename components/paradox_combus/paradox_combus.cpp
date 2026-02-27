#include "paradox_combus.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <sstream>

namespace esphome {
namespace paradox_combus {

static const char *const TAG = "paradox_combus";

namespace {

std::string format_sequence_bytes(const std::vector<uint8_t> &sequence) {
  std::ostringstream stream;
  stream << "[";
  for (size_t i = 0; i < sequence.size(); i++) {
    if (i > 0) {
      stream << " ";
    }
    stream << str_sprintf("0x%02X", sequence[i]);
  }
  stream << "]";
  return stream.str();
}

std::string format_bits_preview(const std::string &bits, size_t max_bits = 96) {
  if (bits.length() <= max_bits) {
    return bits;
  }

  return str_sprintf("%s...(+%u bits)", bits.substr(0, max_bits).c_str(),
                     static_cast<unsigned>(bits.length() - max_bits));
}

}  // namespace


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
    this->pending_sample_at_ = now + this->sample_delay_us_;
    this->sample_pending_ = true;
    this->clock_falling_edges_++;
  }
  this->last_clk_state_ = clk_state;

  if (!this->sample_pending_ || static_cast<long>(now - this->pending_sample_at_) < 0) {
    return;
  }

  this->sample_pending_ = false;
  this->sampled_bits_++;

  const bool raw_level = this->read_pin_->digital_read();
  const bool bit_is_one = this->invert_data_ ? raw_level : !raw_level;
  this->bus_message_.push_back(bit_is_one ? '1' : '0');

  if (this->bus_message_.length() > 200) {
    this->bus_message_.clear();
    this->overflow_drop_frames_++;
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

  ESP_LOGD(TAG, "TX queued COMBUS packet (%u bytes): %s", static_cast<unsigned>(sequence.size()),
           format_sequence_bytes(sequence).c_str());
  ESP_LOGD(TAG, "TX queue depth is now %u bits", static_cast<unsigned>(this->tx_bits_.size()));
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
  this->last_diag_log_at_ = this->last_clk_signal_;
  this->clock_falling_edges_ = 0;
  this->sampled_bits_ = 0;
  this->decoded_frames_ = 0;
  this->crc_drop_frames_ = 0;
  this->overflow_drop_frames_ = 0;
  this->frame_len_hist_.fill(0);
  this->short_frame_drops_ = 0;
  this->malformed_d1_d0_frames_ = 0;
  this->misaligned_frame_drops_ = 0;
  this->last_crc_fail_preview_.clear();
  this->last_crc_fail_cmd_ = 0;
  this->bus_message_.clear();

  this->combus_connection_status_ = true;
  ESP_LOGI(TAG, "COMBUS initialized in polling mode (frame_idle_us=%u, sample_delay_us=%u, invert_data=%s)",
           static_cast<unsigned>(this->frame_idle_us_), static_cast<unsigned>(this->sample_delay_us_),
           YESNO(this->invert_data_));
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
  this->log_bus_diagnostics_();

  if (!this->check_clock_idle_() || this->bus_message_.length() < 2) {
    return;
  }

  std::string message = this->bus_message_;
  this->bus_message_.clear();

  this->decode_message_(message);
}

void ParadoxCombusComponent::log_bus_diagnostics_() {
  const unsigned long now = micros();
  if (now - this->last_diag_log_at_ < 5000000UL) {
    return;
  }

  this->last_diag_log_at_ = now;
  if (this->clock_falling_edges_ == 0) {
    ESP_LOGW(TAG,
             "No COMBUS clock edges seen in last 5s (clk=%d read=%d). Check wiring/level-shifting/opto speed.",
             this->clk_pin_->digital_read(), this->read_pin_->digital_read());
  } else {
    ESP_LOGD(TAG,
             "COMBUS diag (5s): clk_edges=%u sampled_bits=%u decoded=%u crc_drop=%u overflow_drop=%u tx_pending_bits=%u",
             static_cast<unsigned>(this->clock_falling_edges_), static_cast<unsigned>(this->sampled_bits_),
             static_cast<unsigned>(this->decoded_frames_), static_cast<unsigned>(this->crc_drop_frames_),
             static_cast<unsigned>(this->overflow_drop_frames_), static_cast<unsigned>(this->tx_bits_.size()));

    ESP_LOGD(TAG,
             "COMBUS frame lens (5s): <=8=%u 9-16=%u 17-32=%u 33-64=%u 65-96=%u 97-128=%u 129-160=%u >160=%u "
             "short_drop=%u malformed_d0d1=%u misaligned=%u",
             static_cast<unsigned>(this->frame_len_hist_[0]), static_cast<unsigned>(this->frame_len_hist_[1]),
             static_cast<unsigned>(this->frame_len_hist_[2]), static_cast<unsigned>(this->frame_len_hist_[3]),
             static_cast<unsigned>(this->frame_len_hist_[4]), static_cast<unsigned>(this->frame_len_hist_[5]),
             static_cast<unsigned>(this->frame_len_hist_[6]), static_cast<unsigned>(this->frame_len_hist_[7]),
             static_cast<unsigned>(this->short_frame_drops_), static_cast<unsigned>(this->malformed_d1_d0_frames_),
             static_cast<unsigned>(this->misaligned_frame_drops_));

    if (!this->last_crc_fail_preview_.empty()) {
      ESP_LOGD(TAG, "Last CRC fail cmd=0x%02X bits=%s", this->last_crc_fail_cmd_, this->last_crc_fail_preview_.c_str());
    }
  }

  this->clock_falling_edges_ = 0;
  this->sampled_bits_ = 0;
  this->decoded_frames_ = 0;
  this->crc_drop_frames_ = 0;
  this->overflow_drop_frames_ = 0;
  this->frame_len_hist_.fill(0);
  this->short_frame_drops_ = 0;
  this->malformed_d1_d0_frames_ = 0;
  this->misaligned_frame_drops_ = 0;
}

void ParadoxCombusComponent::track_frame_length_(size_t frame_bits) {
  if (frame_bits <= 8) {
    this->frame_len_hist_[0]++;
  } else if (frame_bits <= 16) {
    this->frame_len_hist_[1]++;
  } else if (frame_bits <= 32) {
    this->frame_len_hist_[2]++;
  } else if (frame_bits <= 64) {
    this->frame_len_hist_[3]++;
  } else if (frame_bits <= 96) {
    this->frame_len_hist_[4]++;
  } else if (frame_bits <= 128) {
    this->frame_len_hist_[5]++;
  } else if (frame_bits <= 160) {
    this->frame_len_hist_[6]++;
  } else {
    this->frame_len_hist_[7]++;
  }
}

bool ParadoxCombusComponent::recover_byte_alignment_(std::string &msg, int cmd, bool trim_postamble) {
  const size_t original_length = msg.length();
  for (size_t trim_bits = 1; trim_bits <= 7; trim_bits++) {
    if (original_length <= trim_bits) {
      break;
    }

    std::string candidate = msg.substr(0, original_length - trim_bits);
    if (trim_postamble) {
      if (candidate.length() <= ((4 * 8) + 1)) {
        continue;
      }
      candidate = candidate.substr(0, candidate.length() - (4 * 8) - 1);
    }

    if ((candidate.length() % 8) != 0 || !this->check_crc_(candidate)) {
      continue;
    }

    ESP_LOGD(TAG,
             "RX frame recovered: trimmed %u trailing bit(s) to restore byte alignment (cmd=0x%02X, %u -> %u bits)",
             static_cast<unsigned>(trim_bits), cmd, static_cast<unsigned>(original_length),
             static_cast<unsigned>(msg.length() - trim_bits));
    msg.resize(original_length - trim_bits);
    return true;
  }

  return false;
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
  this->track_frame_length_(msg.length());

  if (msg.length() < 8) {
    this->short_frame_drops_++;
    return;
  }

  int cmd = get_int_from_string_(msg.substr(0, 8));

  ESP_LOGD(TAG, "RX raw COMBUS frame (%u bits, cmd=0x%02X): %s", static_cast<unsigned>(msg.length()), cmd,
           format_bits_preview(msg).c_str());

  if (cmd == 0xD0 || cmd == 0xD1) {
    if ((msg.length() % 8) != 0 && !this->recover_byte_alignment_(msg, cmd, true)) {
      this->misaligned_frame_drops_++;
      ESP_LOGD(TAG, "RX frame dropped: bit length not byte-aligned (cmd=0x%02X, %u bits)", cmd,
               static_cast<unsigned>(msg.length()));
      return;
    }

    if (msg.length() <= ((4 * 8) + 1)) {
      this->malformed_d1_d0_frames_++;
      ESP_LOGD(TAG, "RX frame dropped: cmd 0x%02X too short for postamble trim (%u bits)", cmd,
               static_cast<unsigned>(msg.length()));
      return;
    }
    msg = msg.substr(0, msg.length() - (4 * 8) - 1);
    if ((msg.length() % 8) != 0) {
      this->misaligned_frame_drops_++;
      ESP_LOGD(TAG, "RX frame dropped: bit length not byte-aligned after postamble trim (%u bits, cmd=0x%02X)",
               static_cast<unsigned>(msg.length()), cmd);
      return;
    }
    if (!check_crc_(msg)) {
      this->crc_drop_frames_++;
      this->last_crc_fail_cmd_ = cmd;
      this->last_crc_fail_preview_ = format_bits_preview(msg);
      ESP_LOGD(TAG, "RX frame dropped: CRC mismatch for cmd 0x%02X", cmd);
      return;
    }
  } else {
    if ((msg.length() % 8) != 0 && !this->recover_byte_alignment_(msg, cmd, false)) {
      this->misaligned_frame_drops_++;
      ESP_LOGD(TAG, "RX frame dropped: bit length not byte-aligned (%u bits, cmd=0x%02X)",
               static_cast<unsigned>(msg.length()), cmd);
      return;
    }
    if (!check_crc_(msg)) {
      this->crc_drop_frames_++;
      this->last_crc_fail_cmd_ = cmd;
      this->last_crc_fail_preview_ = format_bits_preview(msg);
      ESP_LOGD(TAG, "RX frame dropped: CRC mismatch for cmd 0x%02X", cmd);
      return;
    }
  }

  ESP_LOGD(TAG, "RX parsed COMBUS packet cmd=0x%02X (%u bits): %s", cmd, static_cast<unsigned>(msg.length()),
           format_bits_preview(msg).c_str());
  this->decoded_frames_++;

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

  if (idletime > static_cast<long>(this->frame_idle_us_)) {
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
