# Paradox-ESPHome

Connect Paradox COMBUS (green-yellow wires which connect alarm system to the keypad) alarm interface to Home Assistant using ESP8266/ESP32 device and ESPHome library.

The implementation can read COMBUS zone/alarm status and now includes an **experimental** COMBUS writer that can be mapped to Home Assistant alarm actions via `alarm_control_panel` (arming/disarming sequences still depend on panel model/programming).

## Example in Home Assistant
![Image of HASS example](https://github.com/Margriko/Paradox-ESPHome/blob/master/images/hass-example.png)

## Wiring
Because Combus operates at ~12v, we need to step down voltage to levels suitable for esp2866.

Wiring example:

      Alarm Aux(+) --- Voltage regulator (5v for Wemos, NodeMCU, 3.3V for generic ESP8266/ESP32) --- VIN pin on esp8266

      Alarm Aux(-) --- esp8266 Ground

                                           +--- clock pin (Wemos, NodeMCU: D1, D2, D8)
      Alarm Yellow --- 15k ohm resistor ---|
                                           +--- 10k ohm resistor --- Ground

                                           +--- data read pin (Wemos, NodeMCU: D1, D2, D8)
      Alarm Green ---- 15k ohm resistor ---|
                                           +--- 10k ohm resistor --- Ground

## Native ESPHome component usage

This repository now includes a native ESPHome **external component** (`components/paradox_combus`) that exposes (including optional split read/write data pins for 3-channel optocoupler modules):

- ESP8266/ESP32-compatible COMBUS reader implemented as a native polling loop (no timer/interrupt dependency)

- `paradox_combus:` hub configuration
- `binary_sensor` platform `paradox_combus` with per-zone sensors
- `alarm_control_panel` platform `paradox_combus` for integrated alarm status + arm/disarm commands (experimental write support)

That means there is no need for `custom_component` + `template` sensor callback wiring.

YAML example (single `alarm_control_panel` entry; write settings are optional):

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/Margriko/Paradox-ESPHome
      ref: v2

paradox_combus:
  id: combus
  clk_pin: D1
  # Optional: frame split idle timeout (microseconds).
  # Increase if logs show mostly very short frames (<=8 bits).
  frame_idle_us: 10000
  # Optional: sampling delay after selected clock edge (40..900 us).
  # `0` enables adaptive delay based on observed clock half-cycle (recommended on ESP32 polling).
  # Set a fixed value (e.g. 120-250) only for manual tuning.
  sample_delay_us: 0
  # Sample on falling edge by default (matches legacy branch behaviour).
  # Set true if your wiring/level shifting captures cleaner data on rising edges.
  sample_on_rising: false
  # Reject clock transitions that arrive too quickly (noise/glitches).
  # For 1 kHz COMBUS, start around 80-150 us.
  min_edge_interval_us: 100
  # Optional: invert read polarity for troubleshooting optocoupler/wiring polarity.
  invert_data: false
  # Legacy/shared data line (single pin for read+write):
  # dta_pin: D2
  # 3-channel optocoupler wiring (recommended for PC817 modules):
  read_pin: D2
  write_pin: D3

binary_sensor:
  - platform: paradox_combus
    paradox_combus_id: combus
    zone: 1
    name: "Entrance motion"
    device_class: motion

alarm_control_panel:
  - platform: paradox_combus
    paradox_combus_id: combus
    name: "Alarm"
    # Optional write-path configuration:
    disarm_sequence: "1234"
    arm_home_sequence: "S"
    arm_away_sequence: "A"
    arm_night_sequence: "N"
```

`disarm_sequence` now accepts an ASCII string (for example `"1234"`) in addition to raw hex bytes.
If `disarm_sequence` is omitted, disarm is not exposed as a writable feature.

### Legacy branch timing baseline (why `master` used to work)

The old interrupt-driven implementation sampled on **falling** edges with a fixed ~**150 us** timer offset.
`v2` is polling-based on ESP32/ESP-IDF, so exact ISR timing no longer exists; it now uses adaptive delay by default (`sample_delay_us: 0`) to derive sampling point from the measured clock period.

If decoding is still unstable on ESP32 + `esp-idf`, keep `invert_data: false`, start with `min_edge_interval_us: 100`, and temporarily remove `write_pin` while validating receive-only traffic.



- If diagnostics stay below ~100 falling edges/sec (e.g. 10-30/sec), decoding cannot work yet. This is usually a GPIO level issue, not CRC tuning.
  Try explicit pin modes:
  ```yaml
  clk_pin:
    number: GPIO18
    mode: INPUT_PULLUP
  read_pin:
    number: GPIO16
    mode: INPUT_PULLUP
  ```
  and validate receive-only first by removing `write_pin` temporarily.

For a full multi-zone example, see `alarm-example.yaml`.

## OTA updates
In order to make OTA updates, connection switch in frontend must be switched to OFF.

## Compatibility
Tested with "Trikdis SP231" alarm system which uses Paradox-compatible green-yellow data bus (COMBUS).
Should work with other Paradox alarm systems.

## References
* Most of the code is taken from https://github.com/liaan/paradox_esp8266
* Wiring and some ideas taken from https://github.com/taligentx/dscKeybusInterface
* General knowledge about decoding COMBUS and a source for future improvements https://github.com/0ki/paradox
* ESPHome library https://esphome.io

## Suggestions
This is a rough implementation, stability is not guaranteed. If you want a stable solution with read/write capability and your alarm system is compatible, take a look at [Paradox Alarm Interface](https://github.com/ParadoxAlarmInterface/pai), which connects to alarm system by using serial port. In my case my alarm system was not compatible and had to use my own solution.
