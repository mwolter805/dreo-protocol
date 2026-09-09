import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import CONF_DREO_ID, Dreo, dreo_ns

DEPENDENCIES = ["dreo"]

CONF_TEXT_DATAPOINT = "text_datapoint"

DreoTextSensor = dreo_ns.class_(
    "DreoTextSensor", text_sensor.TextSensor, cg.Component
)

CONFIG_SCHEMA = text_sensor.text_sensor_schema(DreoTextSensor).extend(
    {
        cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
        cv.Required(CONF_TEXT_DATAPOINT): cv.uint8_t,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await text_sensor.register_text_sensor(var, config)
    parent = await cg.get_variable(config[CONF_DREO_ID])
    cg.add(var.set_dreo_parent(parent))
    cg.add(var.set_text_id(config[CONF_TEXT_DATAPOINT]))
