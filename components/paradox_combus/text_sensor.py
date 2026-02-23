import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import ParadoxCombusComponent

DEPENDENCIES = ["paradox_combus"]

CONF_PARADOX_COMBUS_ID = "paradox_combus_id"
CONF_TYPE = "type"

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(
    {
        cv.GenerateID(CONF_PARADOX_COMBUS_ID): cv.use_id(ParadoxCombusComponent),
        cv.Required(CONF_TYPE): cv.one_of("alarm_status", lower=True),
    }
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    hub = await cg.get_variable(config[CONF_PARADOX_COMBUS_ID])
    cg.add(hub.set_alarm_status_sensor(var))
