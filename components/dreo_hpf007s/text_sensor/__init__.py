import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from .. import CONF_DREO_HPF007S_ID, DreoHpf007s, dreo_hpf007s_ns

DEPENDENCIES = ["dreo_hpf007s"]

DreoHpf007sZone = dreo_hpf007s_ns.class_(
    "DreoHpf007sZone", text_sensor.TextSensor, cg.Component
)

CONFIG_SCHEMA = text_sensor.text_sensor_schema(DreoHpf007sZone).extend(
    {
        cv.GenerateID(CONF_DREO_HPF007S_ID): cv.use_id(DreoHpf007s),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_HPF007S_ID])
    var = await text_sensor.new_text_sensor(config, parent)
    await cg.register_component(var, config)
