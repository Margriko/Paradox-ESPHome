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

This repository now includes a native ESPHome **external component** (`components/paradox_combus`) that exposes:

- ESP8266/ESP32-compatible COMBUS reader implemented as a native polling loop (no timer/interrupt dependency)

- `paradox_combus:` hub configuration
- `binary_sensor` platform `paradox_combus` with per-zone sensors
- `alarm_control_panel` platform `paradox_combus` for integrated alarm status + arm/disarm commands (experimental write support)

That means there is no need for `custom_component` + `template` sensor callback wiring.

Minimal YAML:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/Margriko/Paradox-ESPHome
      ref: v2

paradox_combus:
  id: combus
  clk_pin: D1
  dta_pin: D2

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
```

Alarm control panel YAML (read-only status by default, optional write path):

```yaml
alarm_control_panel:
  - platform: paradox_combus
    paradox_combus_id: combus
    name: "Alarm"

    disarm_sequence: [0x31, 0x32, 0x33, 0x34]
    codes:
      - "1234"
    arm_home_sequence: [0x53]
    arm_away_sequence: [0x41]
    arm_night_sequence: [0x4E]
```

`codes` uses the same structure as the ESPHome template alarm control panel and is used to validate arm/disarm actions.
If `disarm_sequence` is omitted, the entered code is sent as keypad digits for disarm.

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
