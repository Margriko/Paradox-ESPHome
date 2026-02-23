import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

from . import ParadoxCombusComponent, paradox_combus_ns

DEPENDENCIES = ["paradox_combus"]

CONF_PARADOX_COMBUS_ID = "paradox_combus_id"
CONF_ZONE = "zone"

ParadoxZoneBinarySensor = paradox_combus_ns.class_("ParadoxZoneBinarySensor", binary_sensor.BinarySensor)

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(ParadoxZoneBinarySensor).extend(
    {
        cv.GenerateID(CONF_PARADOX_COMBUS_ID): cv.use_id(ParadoxCombusComponent),
        cv.Required(CONF_ZONE): cv.int_range(min=1, max=32),
    }
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    hub = await cg.get_variable(config[CONF_PARADOX_COMBUS_ID])
    cg.add(hub.register_zone_sensor(config[CONF_ZONE], var))
