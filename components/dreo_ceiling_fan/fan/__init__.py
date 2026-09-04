import esphome.codegen as cg
from esphome.components import fan
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import DreoCeilingFan, dreo_ceiling_fan_ns

DEPENDENCIES = ["dreo_ceiling_fan"]

CONF_DREO_CEILING_FAN_ID = "dreo_ceiling_fan_id"

DreoCeilingFanFan = dreo_ceiling_fan_ns.class_("DreoCeilingFanFan", cg.Component, fan.Fan)

CONFIG_SCHEMA = fan.fan_schema(DreoCeilingFanFan).extend(
    {
        cv.GenerateID(CONF_DREO_CEILING_FAN_ID): cv.use_id(DreoCeilingFan),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_CEILING_FAN_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
