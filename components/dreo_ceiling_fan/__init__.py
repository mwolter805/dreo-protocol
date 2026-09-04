import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_ID

from ..dreo import CONF_DREO_ID, Dreo

DEPENDENCIES = ["dreo"]

CONF_AMBIENT_LIGHT_ID = "ambient_light_id"
CONF_AMBIENT_PRESET_EFFECTS = "ambient_preset_effects"

# The physical remote's RGB button steps through exactly four presets, and the
# MCU reports that cursor as dp25. The list is positional: entry N is preset N.
AMBIENT_PRESET_COUNT = 4

dreo_ceiling_fan_ns = cg.esphome_ns.namespace("dreo_ceiling_fan")
DreoCeilingFan = dreo_ceiling_fan_ns.class_("DreoCeilingFan", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DreoCeilingFan),
        cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
        cv.Optional(CONF_AMBIENT_LIGHT_ID): cv.use_id(light.LightState),
        cv.Optional(CONF_AMBIENT_PRESET_EFFECTS): cv.All(
            cv.ensure_list(cv.string_strict),
            cv.Length(min=AMBIENT_PRESET_COUNT, max=AMBIENT_PRESET_COUNT),
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_ID])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    await cg.register_component(var, config)
    for name in config.get(CONF_AMBIENT_PRESET_EFFECTS, []):
        cg.add(var.add_ambient_preset_effect(name))
    if CONF_AMBIENT_LIGHT_ID in config:
        ambient = await cg.get_variable(config[CONF_AMBIENT_LIGHT_ID])
        cg.add(var.set_ambient_light(ambient))
