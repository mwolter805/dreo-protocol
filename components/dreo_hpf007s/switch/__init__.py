import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv

from .. import AXES, CONF_AXIS, CONF_DREO_HPF007S_ID, DreoHpf007s, dreo_hpf007s_ns

DEPENDENCIES = ["dreo_hpf007s"]

DreoHpf007sAxisSwitch = dreo_hpf007s_ns.class_(
    "DreoHpf007sAxisSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = switch.switch_schema(DreoHpf007sAxisSwitch).extend(
    {
        cv.GenerateID(CONF_DREO_HPF007S_ID): cv.use_id(DreoHpf007s),
        cv.Required(CONF_AXIS): cv.enum(AXES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_HPF007S_ID])
    var = await switch.new_switch(config, parent, config[CONF_AXIS])
    await cg.register_component(var, config)
