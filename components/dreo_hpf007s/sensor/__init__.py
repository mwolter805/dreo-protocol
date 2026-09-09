import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import STATE_CLASS_MEASUREMENT

from .. import AXES, CONF_AXIS, CONF_DREO_HPF007S_ID, DreoHpf007s, dreo_hpf007s_ns

DEPENDENCIES = ["dreo_hpf007s"]

DreoHpf007sSensor = dreo_hpf007s_ns.class_(
    "DreoHpf007sSensor", sensor.Sensor, cg.Component
)

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        DreoHpf007sSensor,
        unit_of_measurement="°",
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            cv.GenerateID(CONF_DREO_HPF007S_ID): cv.use_id(DreoHpf007s),
            cv.Required(CONF_AXIS): cv.enum(AXES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_HPF007S_ID])
    var = await sensor.new_sensor(config, parent, config[CONF_AXIS])
    await cg.register_component(var, config)
