import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import (
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_OUTPUT_ID,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from .. import DreoCeilingFan, dreo_ceiling_fan_ns

DEPENDENCIES = ["dreo_ceiling_fan"]

CONF_DREO_CEILING_FAN_ID = "dreo_ceiling_fan_id"

DreoCeilingFanLight = dreo_ceiling_fan_ns.class_("DreoCeilingFanLight", light.LightOutput, cg.Component)

CONFIG_SCHEMA = light.light_schema(DreoCeilingFanLight, light.LightType.BRIGHTNESS_ONLY).extend(
    {
        cv.GenerateID(CONF_DREO_CEILING_FAN_ID): cv.use_id(DreoCeilingFan),
        cv.Optional(CONF_WARM_WHITE_COLOR_TEMPERATURE, default="2700 K"): cv.color_temperature,
        cv.Optional(CONF_COLD_WHITE_COLOR_TEMPERATURE, default="6500 K"): cv.color_temperature,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_CEILING_FAN_ID])
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent)
    await cg.register_component(var, config)
    await light.register_light(var, config)
    cg.add(var.set_warm_white_color_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_cold_white_color_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
