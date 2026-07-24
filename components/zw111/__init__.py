"""ZW111 Hailingke Fingerprint Module - ESPHome External Component."""
import gzip
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, binary_sensor, text_sensor, button
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome import pins


def _generate_web_asset():
    component_dir = Path(__file__).resolve().parent
    source = component_dir / "zw111_web_source.html"
    header_path = component_dir / "zw111_web.h"
    raw = source.read_bytes()
    compressed = gzip.compress(raw, compresslevel=9, mtime=0)
    rows = []
    for offset in range(0, len(compressed), 16):
        chunk = compressed[offset : offset + 16]
        rows.append("  " + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
    header = (
        "#pragma once\n\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n\n"
        "namespace esphome {\n"
        "namespace zw111 {\n\n"
        "inline constexpr uint8_t ZW111_HTML_GZIP[] = {\n"
        + "\n".join(rows)
        + "\n};\n"
        "inline constexpr size_t ZW111_HTML_GZIP_LEN = sizeof(ZW111_HTML_GZIP);\n\n"
        "}  // namespace zw111\n"
        "}  // namespace esphome\n"
    )
    if not header_path.exists() or header_path.read_text(encoding="utf-8") != header:
        header_path.write_text(header, encoding="utf-8", newline="\n")


_generate_web_asset()

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "text_sensor", "button"]

zw111_ns = cg.esphome_ns.namespace("zw111")
ZW111Component = zw111_ns.class_("ZW111Component", cg.Component, uart.UARTDevice)
ZW111Button = zw111_ns.class_("ZW111Button", button.Button)
ZW111SleepButton = zw111_ns.class_("ZW111SleepButton", button.Button)

CONF_CONNECTION_STATUS = "connection_status"
CONF_WEB_PORT = "web_port"
CONF_WEB_USERNAME = "web_username"
CONF_WEB_PASSWORD = "web_password"
CONF_FP_IDENTIFY_SENSOR = "fp_identify_action"
CONF_IDENTIFY_BUTTON = "identify_button"
CONF_SLEEP_BUTTON = "sleep_button"
CONF_SENSOR_CHECK_STATUS = "sensor_check_status"
CONF_POWER_CTL_PIN = "power_ctl_pin"
CONF_TOUCH_SENSE_PIN = "touch_sense_pin"
CONF_TOUCH_SENSOR = "touch_sensor"

CONFIG_SCHEMA = cv.ensure_list(cv.Schema({
    cv.GenerateID(): cv.declare_id(ZW111Component),

    cv.Optional(CONF_WEB_PORT, default=8080): cv.port,
    cv.Optional(CONF_WEB_USERNAME, default=""): cv.string,
    cv.Optional(CONF_WEB_PASSWORD, default=""): cv.string,

    cv.Optional(CONF_CONNECTION_STATUS): binary_sensor.binary_sensor_schema(
        device_class="connectivity",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),

    cv.Optional(CONF_FP_IDENTIFY_SENSOR): text_sensor.text_sensor_schema(
        icon="mdi:fingerprint",
    ),

    cv.Optional(CONF_IDENTIFY_BUTTON): button.button_schema(ZW111Button),
    cv.Optional(CONF_SLEEP_BUTTON): button.button_schema(ZW111SleepButton),

    cv.Optional(CONF_SENSOR_CHECK_STATUS): binary_sensor.binary_sensor_schema(
        device_class="problem",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),

    cv.Optional(CONF_TOUCH_SENSOR): binary_sensor.binary_sensor_schema(),

    cv.Optional(CONF_POWER_CTL_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_TOUCH_SENSE_PIN): pins.gpio_input_pin_schema,
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA))


async def to_code(config):
    for i, conf in enumerate(config):
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        await uart.register_uart_device(var, conf)

        cg.add(var.set_nvs_prefix(str(conf[CONF_ID])))

        cg.add(var.set_web_port(conf[CONF_WEB_PORT]))
        if conf[CONF_WEB_USERNAME]:
            cg.add(var.set_web_username(conf[CONF_WEB_USERNAME]))
        if conf[CONF_WEB_PASSWORD]:
            cg.add(var.set_web_password(conf[CONF_WEB_PASSWORD]))

        if i > 0:
            cg.add(var.set_web_port(config[0][CONF_WEB_PORT] + i))

        if CONF_CONNECTION_STATUS in conf:
            sens = await binary_sensor.new_binary_sensor(conf[CONF_CONNECTION_STATUS])
            cg.add(var.set_connection_status(sens))

        if CONF_FP_IDENTIFY_SENSOR in conf:
            sens = await text_sensor.new_text_sensor(conf[CONF_FP_IDENTIFY_SENSOR])
            cg.add(var.set_fp_identify_sensor(sens))

        if CONF_IDENTIFY_BUTTON in conf:
            btn = await button.new_button(conf[CONF_IDENTIFY_BUTTON])
            cg.add(btn.set_parent(var))

        if CONF_SLEEP_BUTTON in conf:
            btn = await button.new_button(conf[CONF_SLEEP_BUTTON])
            cg.add(btn.set_parent(var))

        if CONF_SENSOR_CHECK_STATUS in conf:
            sens = await binary_sensor.new_binary_sensor(conf[CONF_SENSOR_CHECK_STATUS])
            cg.add(var.set_sensor_check_status(sens))

        if CONF_TOUCH_SENSOR in conf:
            sens = await binary_sensor.new_binary_sensor(conf[CONF_TOUCH_SENSOR])
            cg.add(var.set_touch_sensor(sens))

        if CONF_POWER_CTL_PIN in conf:
            pin = await cg.gpio_pin_expression(conf[CONF_POWER_CTL_PIN])
            cg.add(var.set_power_ctl_pin(pin))

        if CONF_TOUCH_SENSE_PIN in conf:
            pin = await cg.gpio_pin_expression(conf[CONF_TOUCH_SENSE_PIN])
            cg.add(var.set_touch_sense_pin(pin))
