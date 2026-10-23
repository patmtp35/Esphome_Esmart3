import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_KILOWATT_HOURS,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import CONF_ESMART3_ID, ESmart3Component

DEPENDENCIES = ["esmart3"]


def _measure(unit, device_class, decimals):
    return sensor.sensor_schema(
        unit_of_measurement=unit,
        accuracy_decimals=decimals,
        device_class=device_class,
        state_class=STATE_CLASS_MEASUREMENT,
    )


def _energy():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        accuracy_decimals=2,
        device_class=DEVICE_CLASS_ENERGY,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    )


# clé YAML -> schéma. Le setter C++ est set_<clé>_sensor().
SENSORS = {
    # item 0 : mesures temps réel
    "pv_voltage": _measure(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "battery_voltage": _measure(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "charging_current": _measure(UNIT_AMPERE, DEVICE_CLASS_CURRENT, 1),
    "load_voltage": _measure(UNIT_VOLT, DEVICE_CLASS_VOLTAGE, 1),
    "load_current": _measure(UNIT_AMPERE, DEVICE_CLASS_CURRENT, 1),
    "charging_power": _measure(UNIT_WATT, DEVICE_CLASS_POWER, 0),
    "load_power": _measure(UNIT_WATT, DEVICE_CLASS_POWER, 0),
    "battery_temperature": _measure(UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, 0),
    "internal_temperature": _measure(UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE, 0),
    "battery_level": _measure(UNIT_PERCENT, DEVICE_CLASS_BATTERY, 0),
    "charge_mode": sensor.sensor_schema(
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "fault_code": sensor.sensor_schema(
        accuracy_decimals=0,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    # item 2 : énergies (kWh)
    "energy_today": _energy(),
    "energy_month": _energy(),
    "energy_total": _energy(),
    "load_energy_today": _energy(),
    "load_energy_month": _energy(),
    "load_energy_total": _energy(),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ESMART3_ID): cv.use_id(ESmart3Component),
        **{cv.Optional(key): schema for key, schema in SENSORS.items()},
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    for key in SENSORS:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
