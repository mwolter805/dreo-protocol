import esphome.codegen as cg
from esphome.components import text
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import CONF_DREO_ID, Dreo, dreo_ns

DEPENDENCIES = ["dreo"]

CONF_TEXT_DATAPOINT = "text_datapoint"
CONF_VALIDATOR = "validator"

DreoText = dreo_ns.class_("DreoText", text.Text, cg.Component)

CONFIG_SCHEMA = text.text_schema(DreoText, mode="TEXT").extend(
    {
        cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
        cv.Required(CONF_TEXT_DATAPOINT): cv.uint8_t,
        cv.Optional(CONF_VALIDATOR): cv.lambda_,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await text.register_text(
        var,
        config,
        min_length=3,
        max_length=255,
        pattern=r"^[0-9]+,[0-9]+$",
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
