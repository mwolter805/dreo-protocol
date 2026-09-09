import esphome.codegen as cg
from esphome.components import fan
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import CONF_DREO_HPF007S_ID, DreoHpf007s, dreo_hpf007s_ns

DEPENDENCIES = ["dreo_hpf007s"]

DreoHpf007sFan = dreo_hpf007s_ns.class_("DreoHpf007sFan", cg.Component, fan.Fan)

CONFIG_SCHEMA = fan.fan_schema(DreoHpf007sFan).extend(
    {
        cv.GenerateID(CONF_DREO_HPF007S_ID): cv.use_id(DreoHpf007s),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_HPF007S_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
