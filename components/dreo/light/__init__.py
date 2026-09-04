import esphome.codegen as cg
from esphome.components import light
from esphome.components.light.effects import register_rgb_effect
from esphome.components.light.types import LightEffect
import esphome.config_validation as cv
from esphome.const import (
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_EFFECTS,
    CONF_GAMMA_CORRECT,
    CONF_NAME,
    CONF_OUTPUT_ID,
    CONF_SWITCH_DATAPOINT,
    CONF_VALUE,
)

from .. import CONF_DREO_ID, Dreo, dreo_ns

DEPENDENCIES = ["dreo"]

CONF_BRIGHTNESS_DATAPOINT = "brightness_datapoint"
CONF_BRIGHTNESS_OPTIONS = "brightness_options"
CONF_EFFECT_DATAPOINT = "effect_datapoint"
CONF_EFFECT_OPTIONS = "effect_options"
CONF_RGB_DATAPOINT = "rgb_datapoint"
CONF_CONSTANT_EFFECT = "constant_effect"

DreoLight = dreo_ns.class_("DreoLight", light.LightOutput, cg.Component)
DreoLightEffect = dreo_ns.class_("DreoLightEffect", LightEffect)


@register_rgb_effect(
    "dreo_mode",
    DreoLightEffect,
    "Dreo Mode",
    {cv.Required(CONF_VALUE): cv.uint32_t},
)
async def dreo_mode_effect_to_code(config, effect_id):
    return cg.new_Pvariable(effect_id, config[CONF_NAME], config[CONF_VALUE])


def _brightness_options(value):
    value = cv.Schema({cv.uint32_t: cv.percentage})(value)
    if not value:
        raise cv.Invalid("brightness_options must not be empty")
    if len(set(value.values())) != len(value):
        raise cv.Invalid("brightness_options percentages must be unique")
    return value


def _effect_options(value):
    value = cv.Schema({cv.uint32_t: cv.string_strict})(value)
    if not value:
        raise cv.Invalid("effect_options must not be empty")
    if len(set(value.values())) != len(value):
        raise cv.Invalid("effect_options names must be unique")
    return value


def _expand_effect_options(config):
    if CONF_EFFECT_OPTIONS not in config:
        return config
    if CONF_EFFECTS in config:
        raise cv.Invalid("effects cannot be combined with effect_options")
    options = _effect_options(config[CONF_EFFECT_OPTIONS])
    config = dict(config)
    config[CONF_EFFECT_OPTIONS] = options
    config[CONF_EFFECTS] = [
        {"dreo_mode": {CONF_NAME: name, CONF_VALUE: value}}
        for value, name in options.items()
    ]
    return config


def _validate(config):
    if CONF_BRIGHTNESS_DATAPOINT in config and CONF_BRIGHTNESS_OPTIONS not in config:
        raise cv.Invalid("brightness_options is required with brightness_datapoint")
    if CONF_BRIGHTNESS_OPTIONS in config and CONF_BRIGHTNESS_DATAPOINT not in config:
        raise cv.Invalid("brightness_datapoint is required with brightness_options")
    if CONF_EFFECT_DATAPOINT in config and CONF_EFFECT_OPTIONS not in config:
        raise cv.Invalid("effect_options is required with effect_datapoint")
    if CONF_EFFECT_OPTIONS in config and CONF_EFFECT_DATAPOINT not in config:
        raise cv.Invalid("effect_datapoint is required with effect_options")
    if CONF_RGB_DATAPOINT in config:
        if CONF_EFFECT_DATAPOINT not in config or CONF_CONSTANT_EFFECT not in config:
            raise cv.Invalid(
                "effect_datapoint and constant_effect are required with rgb_datapoint"
            )
        if config[CONF_CONSTANT_EFFECT] not in config[CONF_EFFECT_OPTIONS]:
            raise cv.Invalid("constant_effect must be a key in effect_options")
    return config


CONFIG_SCHEMA = cv.All(
    _expand_effect_options,
    light.light_schema(DreoLight, light.LightType.RGB)
    .extend(
        {
            cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
            cv.Optional(CONF_SWITCH_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_BRIGHTNESS_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_BRIGHTNESS_OPTIONS): _brightness_options,
            cv.Optional(CONF_EFFECT_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_EFFECT_OPTIONS): _effect_options,
            cv.Optional(CONF_RGB_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_CONSTANT_EFFECT): cv.uint32_t,
            cv.Optional(CONF_DEFAULT_TRANSITION_LENGTH, default="0s"):
                cv.positive_time_period_milliseconds,
            cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    cv.has_at_least_one_key(
        CONF_SWITCH_DATAPOINT,
        CONF_BRIGHTNESS_DATAPOINT,
        CONF_EFFECT_DATAPOINT,
        CONF_RGB_DATAPOINT,
    ),
    _validate,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_ID])
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent)
    await cg.register_component(var, config)
    await light.register_light(var, config)

    if CONF_SWITCH_DATAPOINT in config:
        cg.add(var.set_switch_id(config[CONF_SWITCH_DATAPOINT]))
    if CONF_BRIGHTNESS_DATAPOINT in config:
        cg.add(var.set_brightness_id(config[CONF_BRIGHTNESS_DATAPOINT]))
        for value, brightness in config[CONF_BRIGHTNESS_OPTIONS].items():
            cg.add(var.add_brightness_mapping(value, brightness))
    if CONF_EFFECT_DATAPOINT in config:
        cg.add(var.set_effect_id(config[CONF_EFFECT_DATAPOINT]))
        for value, name in config[CONF_EFFECT_OPTIONS].items():
            cg.add(var.add_effect_mapping(value, name))
    if CONF_RGB_DATAPOINT in config:
        cg.add(var.set_rgb_id(config[CONF_RGB_DATAPOINT]))
        cg.add(var.set_constant_effect(config[CONF_CONSTANT_EFFECT]))
