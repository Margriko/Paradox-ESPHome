import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID

CODEOWNERS = ["@Margriko"]
AUTO_LOAD = ["binary_sensor", "text_sensor"]

paradox_combus_ns = cg.esphome_ns.namespace("paradox_combus")
ParadoxCombusComponent = paradox_combus_ns.class_("ParadoxCombusComponent", cg.Component)

CONF_CLK_PIN = "clk_pin"
CONF_DTA_PIN = "dta_pin"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ParadoxCombusComponent),
        cv.Required(CONF_CLK_PIN): pins.internal_gpio_input_pin_schema,
        cv.Required(CONF_DTA_PIN): pins.internal_gpio_input_pin_schema,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    clk = await cg.gpio_pin_expression(config[CONF_CLK_PIN])
    cg.add(var.set_clk_pin(clk))

    dta = await cg.gpio_pin_expression(config[CONF_DTA_PIN])
    cg.add(var.set_dta_pin(dta))
