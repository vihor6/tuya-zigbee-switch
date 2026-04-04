from pathlib import Path

import pytest
import yaml

from tests.client import StubProc
from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
)


def _load_device_db() -> dict:
    return yaml.safe_load(Path("device_db.yaml").read_text())


@pytest.mark.parametrize(
    ("device_key", "switch_map"),
    [
        (
            "SWITCH_TUYA_VQJOB26P_TS0011",
            [("A1", 2, "F5", "B11", "F1")],
        ),
        (
            "SWITCH_TUYA_VQJOB26P_TS0012",
            [
                ("D15", 3, "A3", "A5", "F2"),
                ("A0", 4, "A2", "F4", "F0"),
            ],
        ),
        (
            "SWITCH_TUYA_VQJOB26P_TS0013",
            [
                ("D15", 4, "A3", "A5", "F2"),
                ("A1", 5, "F5", "B11", "F1"),
                ("A0", 6, "A2", "F4", "F0"),
            ],
        ),
    ],
)
def test_vqjob26p_runtime_mappings_follow_hardware_grounded_pinout(
    device_key: str,
    switch_map: list[tuple[str, int, str, str, str]],
) -> None:
    cfg = _load_device_db()[device_key]["config_str"]

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

        for button_pin, relay_endpoint, on_pin, off_pin, indicator_pin in switch_map:
            assert not device.get_gpio(on_pin, refresh=True)
            assert not device.get_gpio(off_pin, refresh=True)
            assert device.get_gpio(indicator_pin, refresh=True)

            device.press_button(button_pin)
            assert device.zcl_relay_get(relay_endpoint) == "1"
            assert device.get_gpio(on_pin, refresh=True)
            assert not device.get_gpio(off_pin, refresh=True)
            assert not device.get_gpio(indicator_pin, refresh=True)

            device.release_button(button_pin)
            assert device.zcl_relay_get(relay_endpoint) == "0"
            assert not device.get_gpio(on_pin, refresh=True)
            assert device.get_gpio(off_pin, refresh=True)
            assert device.get_gpio(indicator_pin, refresh=True)

            device.step_time(50)
            assert not device.get_gpio(on_pin, refresh=True)
            assert not device.get_gpio(off_pin, refresh=True)


def test_multidigit_button_pull_down_still_marks_pressed_high() -> None:
    cfg = "Mfr;Model;SD15d;RD2;"

    with StubProc(device_config=cfg) as proc:
        device = Device(proc)

        device.set_gpio("D15", 1)
        device.step_time(60)
        assert device.zcl_relay_get(2) == "1"

        device.set_gpio("D15", 0)
        device.step_time(60)
        assert device.zcl_relay_get(2) == "0"


def test_vqjob26p_device_db_entries_match_requested_mapping() -> None:
    db = _load_device_db()

    expected = {
        "SWITCH_TUYA_VQJOB26P_TS0011": {
            "stock_model_name": "TS0011",
            "firmware_image_type": 47003,
            "config_tokens": ["SA1u", "RF5B11", "IF1i", "M"],
            "info_fragment": "Hardware-grounded mapping: S2 only",
        },
        "SWITCH_TUYA_VQJOB26P_TS0012": {
            "stock_model_name": "TS0012",
            "firmware_image_type": 47004,
            "config_tokens": ["SD15u", "RA3A5", "IF2i", "SA0u", "RA2F4", "IF0i", "M"],
            "info_fragment": "Hardware-grounded mapping: S1 + S3",
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
            "info_fragment": "Hardware-grounded mapping: S1 + S2 + S3",
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
