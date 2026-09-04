import esphome.codegen as cg
from esphome.components import lock
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import CONF_DREO_ID, Dreo, dreo_ns

DEPENDENCIES = ["dreo"]

CONF_LOCK_DATAPOINT = "lock_datapoint"

DreoLock = dreo_ns.class_("DreoLock", lock.Lock, cg.Component)

CONFIG_SCHEMA = lock.lock_schema(DreoLock).extend(
    {
        cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
        cv.Required(CONF_LOCK_DATAPOINT): cv.uint8_t,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await lock.register_lock(var, config)

    parent = await cg.get_variable(config[CONF_DREO_ID])
    cg.add(var.set_dreo_parent(parent))
    cg.add(var.set_lock_id(config[CONF_LOCK_DATAPOINT]))
