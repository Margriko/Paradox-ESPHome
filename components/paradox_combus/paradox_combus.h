#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <array>

namespace esphome {
namespace paradox_combus {


class ParadoxZoneBinarySensor : public binary_sensor::BinarySensor {};

class ParadoxCombusComponent : public Component {
 public:
  void set_clk_pin(InternalGPIOPin *pin) { this->clk_pin_ = pin; }
  void set_dta_pin(InternalGPIOPin *pin) { this->dta_pin_ = pin; }

  void register_zone_sensor(uint8_t zone, binary_sensor::BinarySensor *sensor);
  void set_alarm_status_sensor(text_sensor::TextSensor *sensor) { this->alarm_status_sensor_ = sensor; }

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

  static void IRAM_ATTR interrupt_clock_falling_();
  static void IRAM_ATTR read_data_pin_();

  InternalGPIOPin *clk_pin_{nullptr};
  InternalGPIOPin *dta_pin_{nullptr};

  std::array<binary_sensor::BinarySensor *, 32> zone_sensors_{};
  text_sensor::TextSensor *alarm_status_sensor_{nullptr};

  String bus_message_;
  volatile unsigned long last_clk_signal_{0};
  volatile bool clk_pin_triggered_{false};
  bool combus_connection_status_{false};

  const std::string STATUS_UNAVAILABLE = "unavailable";
  const std::string STATUS_ARM = "armed_away";
  const std::string STATUS_SLEEP = "armed_night";
  const std::string STATUS_STAY = "armed_home";
  const std::string STATUS_OFF = "disarmed";

  static ParadoxCombusComponent *instance_;
};

}  // namespace paradox_combus
}  // namespace esphome
