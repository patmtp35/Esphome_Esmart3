import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.components import switch

from . import CONF_ESMART3_ID, ESmart3Component, esmart3_ns

DEPENDENCIES = ["esmart3"]

CONF_LOAD = "load"

ESmart3LoadSwitch = esmart3_ns.class_("ESmart3LoadSwitch", switch.Switch)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ESMART3_ID): cv.use_id(ESmart3Component),
        # Écrit wLoadModuleSelect1 (5117 = toujours ON, 5118 = toujours OFF) dans le
        # régulateur : cela CHANGE SA CONFIGURATION. Voir le README avant de l'activer.
        cv.Optional(CONF_LOAD): switch.switch_schema(
            ESmart3LoadSwitch, icon="mdi:power-socket-eu"
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    if CONF_LOAD in config:
        sw = await switch.new_switch(config[CONF_LOAD])
        cg.add(sw.set_parent(hub))
        cg.add(hub.set_load_switch(sw))
