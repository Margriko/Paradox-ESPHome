# Paradox-ESPHome

Connect Paradox COMBUS (green-yellow wires which connect alarm system to the keypad) alarm interface to Home Assistant using esp8266 device and ESPHome library.

Currently the implementation is read-only, it shows motion, window/door, smoke sensor and alarm state status in Home Assistant (with a slight delay).

## Example in Home Assistant
![Image of HASS example](https://github.com/Margriko/Paradox-ESPHome/blob/master/images/hass-example.png)

## Wiring
Because Combus operates at ~12v, we need to step down voltage to levels suitable for esp2866.

Wiring example:

      Alarm Aux(+) --- Voltage regulator (5v for Wemos, NodeMCU, 3.3V for generic esp8266) --- VIN pin on esp8266
    
      Alarm Aux(-) --- esp8266 Ground
    
                                           +--- clock pin (Wemos, NodeMCU: D1, D2, D8)
      Alarm Yellow --- 15k ohm resistor ---|
                                           +--- 10k ohm resistor --- Ground

                                           +--- data read pin (Wemos, NodeMCU: D1, D2, D8)
      Alarm Green ---- 15k ohm resistor ---|
                                           +--- 10k ohm resistor --- Ground
 
When using different pins, be sure to modify sources to match your configuration.

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
