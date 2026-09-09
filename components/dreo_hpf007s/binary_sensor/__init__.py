import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_OCCUPANCY

from .. import CONF_DREO_HPF007S_ID, DreoHpf007s, dreo_hpf007s_ns

DEPENDENCIES = ["dreo_hpf007s"]

DreoHpf007sPresence = dreo_hpf007s_ns.class_(
    "DreoHpf007sPresence", binary_sensor.BinarySensor, cg.Component
)

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(
        DreoHpf007sPresence, device_class=DEVICE_CLASS_OCCUPANCY
    )
    .extend(
        {
            cv.GenerateID(CONF_DREO_HPF007S_ID): cv.use_id(DreoHpf007s),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_HPF007S_ID])
    var = await binary_sensor.new_binary_sensor(config, parent)
    await cg.register_component(var, config)
