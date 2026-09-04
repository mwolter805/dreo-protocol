from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import time, uart
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_SENSOR_DATAPOINT, CONF_TRIGGER_ID
from esphome.core import TimePeriod

DEPENDENCIES = ["uart"]
CODEOWNERS = ["@davidc"]

CONF_IGNORE_MCU_UPDATE_ON_DATAPOINTS = "ignore_mcu_update_on_datapoints"
CONF_COMMAND_DATAPOINT_MARKER = "command_datapoint_marker"
CONF_COMMAND_SPACING = "command_spacing"
CONF_WIFI_STATUS_SECOND_BYTE = "wifi_status_second_byte"
CONF_INTEGER_COMMAND_WIDTHS = "integer_command_widths"
CONF_ALLOW_SUB_ENTITY_CONTROL_WHILE_OFF = "allow_sub_entity_control_while_off"
CONF_COMMAND_AUTHORIZER = "command_authorizer"
CONF_TRANSITION_DATAPOINTS = "transition_datapoints"

CONF_ON_DATAPOINT_UPDATE = "on_datapoint_update"
CONF_DATAPOINT_TYPE = "datapoint_type"

dreo_ns = cg.esphome_ns.namespace("dreo")
DreoDatapointType = dreo_ns.enum("DreoDatapointType", is_class=True)
Dreo = dreo_ns.class_("Dreo", cg.Component, uart.UARTDevice)
DreoDatapointCommand = dreo_ns.struct("DreoDatapointCommand")

DPTYPE_ANY = "any"
DPTYPE_BOOL = "bool"
DPTYPE_INT = "int"
DPTYPE_UINT = "uint"
DPTYPE_ENUM = "enum"

DATAPOINT_TYPES = {
    DPTYPE_ANY: dreo_ns.struct("DreoDatapoint"),
    DPTYPE_BOOL: cg.bool_,
    DPTYPE_INT: cg.int_,
    DPTYPE_UINT: cg.uint32,
    DPTYPE_ENUM: cg.uint8,
}

DATAPOINT_TRIGGERS = {
    DPTYPE_ANY: dreo_ns.class_(
        "DreoDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_ANY]),
    ),
    DPTYPE_BOOL: dreo_ns.class_(
        "DreoBoolDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_BOOL]),
    ),
    DPTYPE_INT: dreo_ns.class_(
        "DreoIntDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_INT]),
    ),
    DPTYPE_UINT: dreo_ns.class_(
        "DreoUIntDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_UINT]),
    ),
    DPTYPE_ENUM: dreo_ns.class_(
        "DreoEnumDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_ENUM]),
    ),
}


def assign_declare_id(value):
    value = value.copy()
    value[CONF_TRIGGER_ID] = cv.declare_id(
        DATAPOINT_TRIGGERS[value[CONF_DATAPOINT_TYPE]]
    )(value[CONF_TRIGGER_ID].id)
    return value


CONF_DREO_ID = "dreo_id"
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Dreo),
            cv.Optional(CONF_COMMAND_DATAPOINT_MARKER, default=0): cv.uint8_t,
            # Minimum gap between consecutive transmissions. The default keeps
            # the historical 10 ms pacing; a product whose stock bridge paced
            # frames more slowly raises it here.
            cv.Optional(CONF_COMMAND_SPACING, default="10ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=TimePeriod(milliseconds=1),
                    max=TimePeriod(milliseconds=1000),
                ),
            ),
            # Second payload byte of the module status frame. The default keeps
            # the historical zero byte.
            cv.Optional(CONF_WIFI_STATUS_SECOND_BYTE, default=0): cv.uint8_t,
            cv.Optional(CONF_INTEGER_COMMAND_WIDTHS, default={}): cv.Schema(
                {cv.uint8_t: cv.one_of(1, 2, 4, int=True)}
            ),
            cv.Optional(CONF_ALLOW_SUB_ENTITY_CONTROL_WHILE_OFF, default=False): cv.boolean,
            cv.Optional(CONF_COMMAND_AUTHORIZER): cv.lambda_,
            cv.Optional(CONF_TRANSITION_DATAPOINTS): cv.ensure_list(cv.uint8_t),
            cv.Optional(CONF_IGNORE_MCU_UPDATE_ON_DATAPOINTS): cv.ensure_list(
                cv.uint8_t
            ),
            cv.Optional(CONF_ON_DATAPOINT_UPDATE): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        DATAPOINT_TRIGGERS[DPTYPE_ANY]
                    ),
                    cv.Required(CONF_SENSOR_DATAPOINT): cv.uint8_t,
                    cv.Optional(CONF_DATAPOINT_TYPE, default=DPTYPE_ANY): cv.one_of(
                        *DATAPOINT_TRIGGERS, lower=True
                    ),
                },
                extra_validators=assign_declare_id,
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_command_datapoint_marker(config[CONF_COMMAND_DATAPOINT_MARKER]))
    cg.add(var.set_command_spacing(config[CONF_COMMAND_SPACING].total_milliseconds))
    cg.add(var.set_wifi_status_second_byte(config[CONF_WIFI_STATUS_SECOND_BYTE]))
    for datapoint_id, width in config[CONF_INTEGER_COMMAND_WIDTHS].items():
        cg.add(var.set_integer_command_width(datapoint_id, width))
    cg.add(
        var.set_allow_sub_entity_control_while_off(
            config[CONF_ALLOW_SUB_ENTITY_CONTROL_WHILE_OFF]
        )
    )
    if CONF_COMMAND_AUTHORIZER in config:
        authorizer = await cg.process_lambda(
            config[CONF_COMMAND_AUTHORIZER],
            [(DreoDatapointCommand.operator("ref").operator("const"), "command")],
            return_type=cg.bool_,
        )
        cg.add(var.set_command_authorizer(authorizer))
    for dp in config.get(CONF_TRANSITION_DATAPOINTS, []):
        cg.add(var.add_transition_datapoint(dp))
    if CONF_IGNORE_MCU_UPDATE_ON_DATAPOINTS in config:
        for dp in config[CONF_IGNORE_MCU_UPDATE_ON_DATAPOINTS]:
            cg.add(var.add_ignore_mcu_update_on_datapoints(dp))
    for conf in config.get(CONF_ON_DATAPOINT_UPDATE, []):
        trigger = cg.new_Pvariable(
            conf[CONF_TRIGGER_ID], var, conf[CONF_SENSOR_DATAPOINT]
        )
        await automation.build_automation(
            trigger, [(DATAPOINT_TYPES[conf[CONF_DATAPOINT_TYPE]], "x")], conf
        )
