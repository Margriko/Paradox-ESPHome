import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import alarm_control_panel
from esphome.const import CONF_ID

from . import ParadoxAlarmControlPanel, ParadoxCombusComponent

DEPENDENCIES = ["paradox_combus"]

CONF_PARADOX_COMBUS_ID = "paradox_combus_id"
CONF_DISARM_SEQUENCE = "disarm_sequence"
CONF_ARM_HOME_SEQUENCE = "arm_home_sequence"
CONF_ARM_AWAY_SEQUENCE = "arm_away_sequence"
CONF_ARM_NIGHT_SEQUENCE = "arm_night_sequence"


def _validate_ascii_sequence(value):
    if isinstance(value, str):
        if any(ord(char) > 0x7F for char in value):
            raise cv.Invalid("Sequence string can only contain ASCII characters")
        return [ord(char) for char in value]

    return cv.ensure_list(cv.hex_uint8_t)(value)


CONFIG_SCHEMA = alarm_control_panel.alarm_control_panel_schema(ParadoxAlarmControlPanel).extend(
    {
        cv.GenerateID(CONF_PARADOX_COMBUS_ID): cv.use_id(ParadoxCombusComponent),
        cv.Optional(CONF_DISARM_SEQUENCE): _validate_ascii_sequence,
        cv.Optional(CONF_ARM_HOME_SEQUENCE): _validate_ascii_sequence,
        cv.Optional(CONF_ARM_AWAY_SEQUENCE): _validate_ascii_sequence,
        cv.Optional(CONF_ARM_NIGHT_SEQUENCE): _validate_ascii_sequence,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await alarm_control_panel.register_alarm_control_panel(var, config)

    hub = await cg.get_variable(config[CONF_PARADOX_COMBUS_ID])
    cg.add(var.set_parent(hub))
    cg.add(hub.set_alarm_control_panel(var))

    if CONF_DISARM_SEQUENCE in config:
        cg.add(hub.set_disarm_sequence(config[CONF_DISARM_SEQUENCE]))
    if CONF_ARM_HOME_SEQUENCE in config:
        cg.add(hub.set_arm_home_sequence(config[CONF_ARM_HOME_SEQUENCE]))
    if CONF_ARM_AWAY_SEQUENCE in config:
        cg.add(hub.set_arm_away_sequence(config[CONF_ARM_AWAY_SEQUENCE]))
    if CONF_ARM_NIGHT_SEQUENCE in config:
        cg.add(hub.set_arm_night_sequence(config[CONF_ARM_NIGHT_SEQUENCE]))
