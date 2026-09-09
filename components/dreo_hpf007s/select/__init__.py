import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_NAME, CONF_OPTIONS

from .. import AXES, CONF_AXIS, CONF_DREO_HPF007S_ID, DreoHpf007s, dreo_hpf007s_ns

DEPENDENCIES = ["dreo_hpf007s"]

CONF_SENSOR_LIGHT_GRADIENTS = "sensor_light_gradients"
CONF_CENTRE = "centre"
CONF_EDGE = "edge"

# Sweep widths the stock panel offers. Horizontal sweeps are symmetric about
# centre, so the option value is the total width (30-150 degrees in steps of
# 30); vertical sweeps move only the upper limit (30, 60 or 90 degrees).
SWEEP_OPTIONS = {
    "horizontal": (30, 60, 90, 120, 150),
    "vertical": (30, 60, 90),
}

DreoHpf007sSelect = dreo_hpf007s_ns.class_(
    "DreoHpf007sSelect", select.Select, cg.Component
)


def sweep_options(value):
    value = cv.Schema({cv.int_range(min=1, max=255): cv.string_strict})(value)
    return value


GRADIENT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.string_strict,
        cv.Required(CONF_CENTRE): cv.hex_uint32_t,
        cv.Required(CONF_EDGE): cv.hex_uint32_t,
    }
)


def validate_sweep(config):
    if CONF_AXIS in config:
        if CONF_OPTIONS not in config:
            raise cv.Invalid("a sweep select needs options mapping degrees to names")
        allowed = SWEEP_OPTIONS[str(config[CONF_AXIS])]
        for degrees in config[CONF_OPTIONS]:
            if degrees not in allowed:
                raise cv.Invalid(
                    f"{degrees} is not a stock sweep width for the {config[CONF_AXIS]} axis; "
                    f"valid values are {', '.join(str(d) for d in allowed)}"
                )
    elif CONF_OPTIONS in config:
        raise cv.Invalid("options belong to a sweep select; the gradient select names its own")
    return config


CONFIG_SCHEMA = cv.All(
    select.select_schema(DreoHpf007sSelect)
    .extend(
        {
            cv.GenerateID(CONF_DREO_HPF007S_ID): cv.use_id(DreoHpf007s),
            cv.Optional(CONF_AXIS): cv.enum(AXES, lower=True),
            cv.Optional(CONF_OPTIONS): sweep_options,
            cv.Optional(CONF_SENSOR_LIGHT_GRADIENTS): cv.All(
                cv.ensure_list(GRADIENT_SCHEMA), cv.Length(min=1)
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    cv.has_exactly_one_key(CONF_AXIS, CONF_SENSOR_LIGHT_GRADIENTS),
    validate_sweep,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_HPF007S_ID])
    if CONF_AXIS in config:
        options = config[CONF_OPTIONS]
        var = await select.new_select(
            config, parent, config[CONF_AXIS], options=list(options.values())
        )
        cg.add(var.set_degree_mappings(list(options.keys())))
    else:
        gradients = config[CONF_SENSOR_LIGHT_GRADIENTS]
        var = await select.new_select(
            config, parent, options=[g[CONF_NAME] for g in gradients]
        )
        for gradient in gradients:
            cg.add(
                parent.add_gradient(
                    gradient[CONF_NAME], gradient[CONF_CENTRE], gradient[CONF_EDGE]
                )
            )
    await cg.register_component(var, config)
