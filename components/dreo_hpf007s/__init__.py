import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..dreo import CONF_DREO_ID, Dreo

DEPENDENCIES = ["dreo"]

CONF_DREO_HPF007S_ID = "dreo_hpf007s_id"
CONF_AXIS = "axis"

dreo_hpf007s_ns = cg.esphome_ns.namespace("dreo_hpf007s")
DreoHpf007s = dreo_hpf007s_ns.class_("DreoHpf007s", cg.Component)
Axis = dreo_hpf007s_ns.enum("Axis", is_class=True)

AXES = {
    "horizontal": Axis.HORIZONTAL,
    "vertical": Axis.VERTICAL,
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DreoHpf007s),
        cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await cg.register_component(var, config)
