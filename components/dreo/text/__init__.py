import esphome.codegen as cg
from esphome.components import text
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MAX_LENGTH, CONF_MIN_LENGTH, CONF_PATTERN

from .. import CONF_DREO_ID, Dreo, dreo_ns

DEPENDENCIES = ["dreo"]

CONF_TEXT_DATAPOINT = "text_datapoint"
CONF_VALIDATOR = "validator"

DreoText = dreo_ns.class_("DreoText", text.Text, cg.Component)


def validate_lengths(config):
    if config[CONF_MIN_LENGTH] > config[CONF_MAX_LENGTH]:
        raise cv.Invalid("min_length must not exceed max_length")
    return config

CONFIG_SCHEMA = cv.All(
    text.text_schema(DreoText, mode="TEXT").extend(
        {
            cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
            cv.Required(CONF_TEXT_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_MIN_LENGTH, default=3): cv.int_range(min=0, max=255),
            cv.Optional(CONF_MAX_LENGTH, default=255): cv.int_range(min=0, max=255),
            cv.Optional(CONF_PATTERN, default=r"^[0-9]+,[0-9]+$"): cv.string_strict,
            cv.Optional(CONF_VALIDATOR): cv.lambda_,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_lengths,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await text.register_text(
        var,
        config,
        min_length=config[CONF_MIN_LENGTH],
        max_length=config[CONF_MAX_LENGTH],
        pattern=config[CONF_PATTERN],
    )

    parent = await cg.get_variable(config[CONF_DREO_ID])
    cg.add(var.set_dreo_parent(parent))
    cg.add(var.set_text_id(config[CONF_TEXT_DATAPOINT]))
    if CONF_VALIDATOR in config:
        validator = await cg.process_lambda(
            config[CONF_VALIDATOR],
            [(cg.std_string.operator("ref").operator("const"), "value")],
            return_type=cg.bool_,
        )
        cg.add(var.set_validator(validator))
