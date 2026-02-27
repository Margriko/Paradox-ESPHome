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
  # Optional frame boundary idle timeout in microseconds.
  frame_idle_us: 25000
  # Optional sample delay after selected clock edge (40..900 us).
  # Paradox COMBUS at 1 kHz often needs ~300-500 us for stable reads.
  sample_delay_us: 350
  # Sample on rising edge for slave->master packets (default).
  # Set false to sample after falling edge if your wiring inverts bus phases.
  sample_on_rising: true
  # Reject clock transitions that arrive too quickly (noise/glitches).
  # For 1 kHz COMBUS, start around 450-500 us.
  min_edge_interval_us: 450
  # Optional data polarity inversion for read sampling.
  invert_data: false
  # Legacy single data pin (shared read/write):
  # dta_pin: D2
  # 3-channel optocoupler (clock + read + write):
  read_pin: D2
  write_pin: D3
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
    disarm_sequence: "1234"
    arm_home_sequence: "S"
    arm_away_sequence: "A"
    arm_night_sequence: "N"
```

- No write config is required if you only need alarm status/state in Home Assistant.
- `disarm_sequence` and `arm_*_sequence` can be provided either as ASCII strings (e.g. `"1234"`, `"A"`) or hex-byte lists.
- If your panel requires entering a code before arming, Home Assistant `code` is still prepended to each `arm_*_sequence`.
- Sequences are raw bytes transmitted LSB-first on COMBUS and are panel-specific.
- The implementation is experimental and intended as a starting point for tuning against your panel.

## Notes

- The COMBUS parser/decoder remains based on the original implementation and now publishes directly to registered ESPHome entities.
- If logs show repeated `bit length not byte-aligned` or `CRC mismatch` drops, start from `frame_idle_us: 25000`, `sample_on_rising: true`, `min_edge_interval_us: 450`, and `sample_delay_us: 350` (then tune in ~50 us steps).
  Values like `frame_idle_us: 150000` can merge multiple packets into one frame and will usually prevent decoding.
- If diagnostics show very low clock activity (for example tens of edges/sec), decoding will fail regardless of CRC/frame tuning; this usually points to wiring, pull-up, level-shifting, or optocoupler speed limits.
- Pin configuration moved from hardcoded C++ defines to YAML (`clk_pin`, plus either legacy `dta_pin` or split `read_pin`/`write_pin`).
- Existing sample configuration was migrated to this new format in `alarm.yaml`.
