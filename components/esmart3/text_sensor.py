import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.components import text_sensor

from . import CONF_ESMART3_ID, ESmart3Component

DEPENDENCIES = ["esmart3"]

# clé YAML -> schéma. Le setter C++ est set_<clé>_text_sensor().
TEXT_SENSORS = {
    "charge_mode": text_sensor.text_sensor_schema(icon="mdi:solar-power"),
    "faults": text_sensor.text_sensor_schema(icon="mdi:alert-circle-outline"),
    "load_mode": text_sensor.text_sensor_schema(icon="mdi:power-socket-eu"),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ESMART3_ID): cv.use_id(ESmart3Component),
        **{cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()},
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    for key in TEXT_SENSORS:
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_text_sensor")(sens))
