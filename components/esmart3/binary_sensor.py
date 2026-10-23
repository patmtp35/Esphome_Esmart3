import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_POWER, DEVICE_CLASS_PROBLEM

from . import CONF_ESMART3_ID, ESmart3Component

DEPENDENCIES = ["esmart3"]

# clé YAML -> schéma. Le setter C++ est set_<clé>_binary_sensor().
BINARY_SENSORS = {
    "load_on": binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_POWER),
    "fault": binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ESMART3_ID): cv.use_id(ESmart3Component),
        **{cv.Optional(key): schema for key, schema in BINARY_SENSORS.items()},
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    for key in BINARY_SENSORS:
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_binary_sensor")(sens))
