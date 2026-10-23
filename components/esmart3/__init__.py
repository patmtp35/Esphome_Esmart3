"""Hub ESPHome pour le régulateur MPPT eSmart3 (RS485, 9600 8N1, protocole propriétaire)."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_ADDRESS, CONF_FLOW_CONTROL_PIN, CONF_ID

DEPENDENCIES = ["uart"]
# Les plateformes sensor / text_sensor / binary_sensor / switch partagent ce hub ; on les charge
# d'office pour que leurs en-têtes C++ soient toujours présents à la compilation.
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "switch"]
MULTI_CONF = True

CONF_ESMART3_ID = "esmart3_id"
CONF_SLOW_UPDATE_EVERY = "slow_update_every"
CONF_U32_WORD_ORDER = "u32_word_order"

esmart3_ns = cg.esphome_ns.namespace("esmart3")
ESmart3Component = esmart3_ns.class_(
    "ESmart3Component", cg.PollingComponent, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ESmart3Component),
            # 0 = adresse de diffusion : convient pour un seul régulateur sur le bus.
            cv.Optional(CONF_ADDRESS, default=0): cv.int_range(min=0, max=255),
            # Énergies et état de la sortie charge sont lus 1 fois sur N interrogations.
            cv.Optional(CONF_SLOW_UPDATE_EVERY, default=6): cv.int_range(
                min=1, max=255
            ),
            # Broche DE/RE (ou RSE) d'un module RS485 SANS direction automatique.
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            # Ordre des deux mots d'un entier 32 bits. Le PDF se contredit ; voir le README.
            cv.Optional(CONF_U32_WORD_ORDER, default="high_first"): cv.one_of(
                "high_first", "low_first", lower=True
            ),
        }
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "esmart3",
    baud_rate=9600,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_FLOW_CONTROL_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))

    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_slow_every(config[CONF_SLOW_UPDATE_EVERY]))
    cg.add(var.set_u32_high_word_first(config[CONF_U32_WORD_ORDER] == "high_first"))
