#include "paradox_combus.h"

#include "esphome/core/log.h"

namespace esphome {
namespace paradox_combus {

static const char *const TAG = "paradox_combus";
ParadoxCombusComponent *ParadoxCombusComponent::instance_ = nullptr;

void ParadoxCombusComponent::register_zone_sensor(uint8_t zone, binary_sensor::BinarySensor *sensor) {
  if (zone < 1 || zone > 32) {
    ESP_LOGW(TAG, "Ignoring zone %u (valid range is 1..32)", zone);
    return;
  }
  this->zone_sensors_[zone - 1] = sensor;
}

void IRAM_ATTR ParadoxCombusComponent::interrupt_clock_falling_() {
  if (instance_ == nullptr) {
    return;
  }

  instance_->last_clk_signal_ = micros();
  instance_->clk_pin_triggered_ = true;
  timer1_write(750);
}

void IRAM_ATTR ParadoxCombusComponent::read_data_pin_() {
  if (instance_ == nullptr || instance_->dta_pin_ == nullptr) {
    return;
  }

  if (!instance_->clk_pin_triggered_) {
    return;
  }

  while (micros() - instance_->last_clk_signal_ < 150) {
  }

  if (!instance_->dta_pin_->digital_read())
    instance_->bus_message_ += "1";
  else
    instance_->bus_message_ += "0";

  instance_->clk_pin_triggered_ = false;

  if (instance_->bus_message_.length() > 200) {
    instance_->bus_message_ = "";
    return;
  }
}

void ParadoxCombusComponent::connect_combus_() {
  if (this->clk_pin_ == nullptr || this->dta_pin_ == nullptr) {
    ESP_LOGE(TAG, "clk_pin and dta_pin are required");
    return;
  }

  this->clk_pin_->pin_mode(gpio::FLAG_INPUT);
  this->dta_pin_->pin_mode(gpio::FLAG_INPUT);

  attachInterrupt(this->clk_pin_->get_pin(), &ParadoxCombusComponent::interrupt_clock_falling_, FALLING);
  timer1_attachInterrupt(&ParadoxCombusComponent::read_data_pin_);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);

  this->combus_connection_status_ = true;
  ESP_LOGI(TAG, "COMBUS initialized");
}

void ParadoxCombusComponent::disconnect_combus_() {
  timer1_disable();
  timer1_detachInterrupt();

  if (this->clk_pin_ != nullptr) {
    detachInterrupt(this->clk_pin_->get_pin());
  }

  this->combus_connection_status_ = false;
}

void ParadoxCombusComponent::setup() {
  instance_ = this;
  this->connect_combus_();
}

void ParadoxCombusComponent::publish_alarm_state_(const std::string &value) {
  if (this->alarm_status_sensor_ != nullptr) {
    this->alarm_status_sensor_->publish_state(value);
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
    this->publish_alarm_state_(STATUS_UNAVAILABLE);
    for (int i = 0; i < 32; i++) {
      this->publish_zone_state_(i + 1, false);
    }
    return;
  }

  if (!this->check_clock_idle_() || this->bus_message_.length() < 2) {
    return;
  }

  String message = this->bus_message_;
  this->bus_message_ = "";

  this->decode_message_(message);
}

void ParadoxCombusComponent::process_zone_status_(String &msg) {
  for (int i = 0; i < 32; i++) {
    bool open = msg[17 + (i * 2)] == '1';
    this->publish_zone_state_(i + 1, open);
  }
}

void ParadoxCombusComponent::process_alarm_status_(String &msg) {
  if (msg[((8 * 7) + 1)] == '0') {
    if (msg[((8 * 2) + 5)] == '1') {
      this->publish_alarm_state_(STATUS_STAY);
    }
    if (msg[((8 * 6) + 5)] == '1') {
      this->publish_alarm_state_(STATUS_SLEEP);
    }
    if (msg[((8 * 2) + 1)] == '1') {
      if (msg[((8 * 2) + 0)] == '1') {
        this->publish_alarm_state_("exit");
      } else if (msg[((8 * 2) + 0)] == '0') {
        this->publish_alarm_state_("fullalarm");
      } else {
        this->publish_alarm_state_(STATUS_ARM);
      }
    }
  } else {
    this->publish_alarm_state_(STATUS_OFF);
  }
}

void ParadoxCombusComponent::decode_message_(String &msg) {
  int cmd = get_int_from_string_(msg.substring(0, 8));

  if (cmd == 0xD0 || cmd == 0xD1) {
    msg = msg.substring(0, msg.length() - (4 * 8) - 1);
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

uint8_t ParadoxCombusComponent::crc8_(uint8_t *addr, uint8_t len) {
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

uint8_t ParadoxCombusComponent::check_crc_(String &st) {
  int bytes = (st.length()) / 8;
  uint8_t calc_crc_byte;

  uint8_t *binary_str = str_to_bin_array_(st);

  uint8_t crc = binary_str[bytes - 1];
  calc_crc_byte = crc8_(binary_str, (int) bytes - 1);
  bool valid = calc_crc_byte == crc;

  delete[] binary_str;
  return valid;
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

unsigned int ParadoxCombusComponent::get_int_from_string_(String str) {
  int r = 0;
  int length = str.length();

  for (int j = 0; j < length; j++) {
    if (str[length - j - 1] == '1') {
      r |= 1 << j;
    }
  }

  return r;
}

uint8_t *ParadoxCombusComponent::str_to_bin_array_(String &st) {
  int bytes = (st.length()) / 8;
  auto *data = new uint8_t[bytes];

  String val = "";

  for (int i = 0; i < bytes; i++) {
    val = st.substring((i * 8), ((i * 8)) + 8);
    data[i] = get_int_from_string_(val);
  }

  return data;
}

}  // namespace paradox_combus
}  // namespace esphome
