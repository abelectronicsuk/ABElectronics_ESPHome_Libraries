import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_CHANNEL,
    CONF_GAIN,
    CONF_RESOLUTION,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
)

from . import MCP3424Component, mcp3424_ns

DEPENDENCIES = ["mcp3424"]

CONF_MCP3424_ID = "mcp3424_id"

MCP3424Sensor = mcp3424_ns.class_("MCP3424Sensor", sensor.Sensor)

# resolution (bits) -> sample-rate bits in the config register
RESOLUTIONS = {12: 0, 14: 1, 16: 2, 18: 3}
# PGA gain -> gain bits in the config register
GAINS = {1: 0, 2: 1, 4: 2, 8: 3}

CONFIG_SCHEMA = sensor.sensor_schema(
    MCP3424Sensor,
    unit_of_measurement=UNIT_VOLT,
    accuracy_decimals=6,
    device_class=DEVICE_CLASS_VOLTAGE,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(
    {
        cv.GenerateID(CONF_MCP3424_ID): cv.use_id(MCP3424Component),
        cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=4),
        cv.Optional(CONF_RESOLUTION, default=18): cv.one_of(*RESOLUTIONS, int=True),
        cv.Optional(CONF_GAIN, default=1): cv.one_of(*GAINS, int=True),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCP3424_ID])
    var = await sensor.new_sensor(config)
    cg.add(var.set_channel(config[CONF_CHANNEL] - 1))
    cg.add(var.set_resolution_bits(config[CONF_RESOLUTION]))
    cg.add(var.set_gain(config[CONF_GAIN]))
    cg.add(parent.register_sensor(var))
