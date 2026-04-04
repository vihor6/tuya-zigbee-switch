from pathlib import Path

import yaml

from tests.client import StubProc
from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
)


def _load_device_db() -> dict:
    return yaml.safe_load(Path("device_db.yaml").read_text())


def test_parser_supports_multidigit_and_port_f_gpio_tokens() -> None:
    cfg = "Mfr;Model;SD15u;RF5B11;IF1i;"

    with StubProc(device_config=cfg) as proc:
        device = Device(proc)

        assert (
            device.read_zigbee_attr(
                1,
                ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
                ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
            )
            is not None
        )

        device.press_button("D15")
        assert device.zcl_relay_get(2) == "1"

        device.release_button("D15")
        assert device.zcl_relay_get(2) == "0"


def test_vqjob26p_device_db_entries_match_requested_mapping() -> None:
    db = _load_device_db()

    expected = {
        "SWITCH_TUYA_VQJOB26P_TS0011": {
            "stock_model_name": "TS0011",
            "firmware_image_type": 47003,
            "config_tokens": ["SA1u", "RF5B11", "IF1i", "M"],
            "info_fragment": "inferred",
        },
        "SWITCH_TUYA_VQJOB26P_TS0012": {
            "stock_model_name": "TS0012",
            "firmware_image_type": 47004,
            "config_tokens": ["SD15u", "RA3A5", "IF2i", "SA0u", "RA2F4", "IF0i", "M"],
            "info_fragment": "confirmed",
        },
        "SWITCH_TUYA_VQJOB26P_TS0013": {
            "stock_model_name": "TS0013",
            "firmware_image_type": 47005,
            "config_tokens": [
                "SD15u",
                "RA3A5",
                "IF2i",
                "SA1u",
                "RF5B11",
                "IF1i",
                "SA0u",
                "RA2F4",
                "IF0i",
                "M",
            ],
            "info_fragment": "inferred",
        },
    }

    for key, entry_expectation in expected.items():
        entry = db[key]

        assert entry["category"] == "switch"
        assert entry["neutral"] == "without"
        assert entry["output"] == "relay_latching"
        assert entry["device_type"] == "end_device"
        assert entry["stock_manufacturer_name"] == "_TZ3000_vqjob26p"
        assert entry["stock_model_name"] == entry_expectation["stock_model_name"]
        assert entry["stock_converter_model"] == entry_expectation["stock_model_name"]
        assert entry["tuya_module"] == "TYZS3"
        assert entry["mcu_family"] == "Silabs"
        assert entry["mcu"] == "EFR32MG13P732F512GM48"
        assert entry["firmware_image_type"] == entry_expectation["firmware_image_type"]
        assert entry["status"] == "mostly_supported"
        assert entry_expectation["info_fragment"] in entry["info"]

        config_parts = [part for part in entry["config_str"].split(";") if part]
        assert config_parts[0] == "vqjob26p"
        assert config_parts[1].startswith(entry_expectation["stock_model_name"])
        assert config_parts[2:] == entry_expectation["config_tokens"]


def test_device_db_schema_allows_mg13_tyzs3_rows() -> None:
    schema = yaml.safe_load(Path("device_db.schema.json").read_text())
    item_schema = schema["patternProperties"]["^[A-Z][A-Z0-9_]*$"]["properties"]
    mcu_enum = item_schema["mcu"]["oneOf"][0]["enum"]
    module_enum = item_schema["tuya_module"]["oneOf"][0]["enum"]

    assert "EFR32MG13P732F512GM48" in mcu_enum
    assert "TYZS3" in module_enum
