import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv

from .. import AXES, CONF_AXIS, CONF_DREO_HPF007S_ID, DreoHpf007s, dreo_hpf007s_ns

DEPENDENCIES = ["dreo_hpf007s"]

CONF_CURVE_BLOCK = "curve_block"

# Custom curve blocks in the stock application's editing order: <=67 F,
# 68-73, 74-79, 80-85, 86-91 and >=92 F. Each holds one fan speed 1-9.
CURVE_BLOCKS = 6
SPEED_MIN = 1
SPEED_MAX = 9

# Head-position targets in degrees, from the stock application's limits.
POSITION_LIMITS = {
    "vertical": (-30, 90),
    "horizontal": (-75, 75),
}

DreoHpf007sNumber = dreo_hpf007s_ns.class_(
    "DreoHpf007sNumber", number.Number, cg.Component
)

CONFIG_SCHEMA = cv.All(
    number.number_schema(DreoHpf007sNumber)
    .extend(
        {
            cv.GenerateID(CONF_DREO_HPF007S_ID): cv.use_id(DreoHpf007s),
            cv.Optional(CONF_AXIS): cv.enum(AXES, lower=True),
            cv.Optional(CONF_CURVE_BLOCK): cv.int_range(min=0, max=CURVE_BLOCKS - 1),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    cv.has_exactly_one_key(CONF_AXIS, CONF_CURVE_BLOCK),
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_HPF007S_ID])
    if CONF_AXIS in config:
        low, high = POSITION_LIMITS[str(config[CONF_AXIS])]
        var = await number.new_number(
            config, parent, config[CONF_AXIS], min_value=low, max_value=high, step=1
        )
    else:
        var = await number.new_number(
            config,
            parent,
            config[CONF_CURVE_BLOCK],
            min_value=SPEED_MIN,
            max_value=SPEED_MAX,
            step=1,
        )
    await cg.register_component(var, config)
