import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID

CODEOWNERS = ["@Margriko"]
AUTO_LOAD = ["alarm_control_panel", "binary_sensor"]

paradox_combus_ns = cg.esphome_ns.namespace("paradox_combus")
ParadoxCombusComponent = paradox_combus_ns.class_("ParadoxCombusComponent", cg.Component)
ParadoxAlarmControlPanel = paradox_combus_ns.class_("ParadoxAlarmControlPanel")

CONF_CLK_PIN = "clk_pin"
CONF_DTA_PIN = "dta_pin"
CONF_READ_PIN = "read_pin"
CONF_WRITE_PIN = "write_pin"
CONF_FRAME_IDLE_US = "frame_idle_us"
CONF_SAMPLE_DELAY_US = "sample_delay_us"
CONF_INVERT_DATA = "invert_data"

BASE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ParadoxCombusComponent),
        cv.Required(CONF_CLK_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_DTA_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_READ_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_WRITE_PIN): pins.internal_gpio_output_pin_schema,
        cv.Optional(CONF_FRAME_IDLE_US, default=25000): cv.int_range(min=2000, max=200000),
        cv.Optional(CONF_SAMPLE_DELAY_US, default=150): cv.int_range(min=40, max=250),
        cv.Optional(CONF_INVERT_DATA, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


def validate_config(config):
    has_legacy_dta = CONF_DTA_PIN in config
    has_read_pin = CONF_READ_PIN in config
    has_write_pin = CONF_WRITE_PIN in config

    if has_legacy_dta and (has_read_pin or has_write_pin):
        raise cv.Invalid("Use either dta_pin or read_pin/write_pin, not both")

    if not has_legacy_dta and not has_read_pin:
        raise cv.Invalid("Either dta_pin or read_pin must be configured")

    return config


CONFIG_SCHEMA = cv.All(BASE_SCHEMA, validate_config)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    clk = await cg.gpio_pin_expression(config[CONF_CLK_PIN])
    cg.add(var.set_clk_pin(clk))
    cg.add(var.set_frame_idle_us(config[CONF_FRAME_IDLE_US]))
    cg.add(var.set_sample_delay_us(config[CONF_SAMPLE_DELAY_US]))
    cg.add(var.set_invert_data(config[CONF_INVERT_DATA]))

    if CONF_DTA_PIN in config:
        dta = await cg.gpio_pin_expression(config[CONF_DTA_PIN])
        cg.add(var.set_read_pin(dta))
        cg.add(var.set_write_pin(dta))
    else:
        read_pin = await cg.gpio_pin_expression(config[CONF_READ_PIN])
        cg.add(var.set_read_pin(read_pin))

        if CONF_WRITE_PIN in config:
            write_pin = await cg.gpio_pin_expression(config[CONF_WRITE_PIN])
            cg.add(var.set_write_pin(write_pin))
