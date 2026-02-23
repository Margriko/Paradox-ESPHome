# Native ESPHome `paradox_combus` component

This repository now includes an ESPHome external component under `components/paradox_combus`.

## What changed

Instead of this pattern:
- `custom_component`
- callback glue in lambda
- many `template` entities (`z1...zN`, `alarm_status`)

you can now define entities directly with `platform: paradox_combus`.

## Component layout

```text
components/
  paradox_combus/
    __init__.py          # hub schema (`paradox_combus:`)
    binary_sensor.py     # zone entities
    alarm_control_panel.py # arm/disarm entity + write sequences
    paradox_combus.h     # hub implementation
    paradox_combus.cpp
```

## YAML schema

### Hub

```yaml
paradox_combus:
  id: combus
  clk_pin: D1
  dta_pin: D2
```

### Zone sensor

```yaml
binary_sensor:
  - platform: paradox_combus
    paradox_combus_id: combus
    zone: 1
    name: "Entrance motion"
```

- `zone` supports values `1..32`.

### Alarm control panel (read status, optional write in the same entry)

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

- No write config is required if you only need alarm status/state in Home Assistant.
- `codes` follows the same structure as template alarm and is used to validate both arm and disarm commands.
- If `disarm_sequence` is omitted, the entered code is sent as keypad digits for disarm; arm modes send `code + arm_*_sequence`.
- Sequences are raw bytes transmitted LSB-first on COMBUS and are panel-specific.
- The implementation is experimental and intended as a starting point for tuning against your panel.

## Notes

- The COMBUS parser/decoder remains based on the original implementation and now publishes directly to registered ESPHome entities.
- Pin configuration moved from hardcoded C++ defines to YAML (`clk_pin`, `dta_pin`).
- Existing sample configuration was migrated to this new format in `alarm.yaml`.
