#!/usr/bin/env python3
"""Source contract for the compile-only DR-HPF007S package and wrapper.

These checks read the YAML as text so they run without ESPHome. The schema
tests validate the same files with the pinned ESPHome release.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


def datapoint_references(config: str) -> set[int]:
    ids = set()
    for key in (
        "switch_datapoint",
        "number_datapoint",
        "sensor_datapoint",
        "lock_datapoint",
        "enum_datapoint",
        "int_datapoint",
        "text_datapoint",
    ):
        ids.update(int(m) for m in re.findall(rf"{key}:\s*(\d+)", config))
    return ids


class Hpf007sPackageContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.package = (ROOT / "packages/dreo-hpf007s.yaml").read_text()
        cls.wrapper = (ROOT / "example-dreo-hpf007s-mbl01.yaml").read_text()
        cls.coordinator = (ROOT / "components/dreo_hpf007s/dreo_hpf007s.h").read_text()

    def test_hub_options_match_the_captured_transport(self):
        hub = self.package.split("dreo:", 1)[1].split("\ndreo_hpf007s:", 1)[0]
        self.assertIn("command_datapoint_marker: 1", hub)
        self.assertIn("enum_command_type: int", hub)
        self.assertIn("wifi_status_second_byte: 1", hub)
        self.assertIn("acknowledge_reports: true", hub)
        self.assertIn("transition_datapoints: [1, 5]", hub)
        widths = hub.split("integer_command_widths:", 1)[1].split("allow_sub", 1)[0]
        self.assertEqual(
            dict(re.findall(r"(\d+):\s*(\d)", widths)),
            {"12": "4", "13": "4", "19": "4", "22": "1", "25": "4", "26": "4"},
        )
        self.assertIn("allow_sub_entity_control_while_off: false", hub)
        self.assertIn("authorize_command(command)", hub)

    def test_entity_census_covers_mapped_datapoints_only(self):
        generic = datapoint_references(self.package)
        # Generic adapters carry the independent settings; the coordinator
        # owns 1, 2, 4, 5, 6, 7, 8, 23, 24, 25 and 26.
        self.assertEqual(
            generic, {9, 10, 11, 12, 13, 14, 17, 18, 19, 21, 22, 27, 28}
        )
        self.assertNotIn(15, generic)
        self.assertNotIn(20, generic)
        owned_ids = {
            int(m) for m in re.findall(r"static constexpr uint8_t DP_\w+ = (\d+);", self.coordinator)
        }
        self.assertTrue({1, 2, 4, 5, 6, 7, 8, 23, 24, 25, 26} <= owned_ids, owned_ids)
        self.assertFalse({15, 20} & owned_ids, owned_ids)
        # Weakly known or cloud-owned features stay out or stay diagnostic.
        schedule = self.package.split("sensor_datapoint: 14", 1)[1].split("\n\n", 1)[0]
        self.assertIn("entity_category: diagnostic", schedule)
        self.assertIn("disabled_by_default: true", schedule)
        self.assertNotIn("platform: dreo\n    id: hpf007s_datapoint_15", self.package)
        self.assertNotIn("rgb_datapoint", self.package)
        self.assertNotIn("light:", self.package)
        self.assertEqual(self.package.count("sensor_light_gradients:"), 1)
        self.assertEqual(len(re.findall(r"- name: .+\n\s+centre:", self.package)), 4)

    def test_fan_performs_no_restore_write(self):
        fan = self.package.split("platform: dreo_hpf007s\n    id: hpf007s_fan", 1)[1].split("\n\n", 1)[0]
        self.assertIn("restore_mode: NO_RESTORE", fan)

    def test_wrapper_uses_the_verified_application_boundary(self):
        self.assertIn('board_build.bkrbl_size_app: "0x110000"', self.wrapper)
        self.assertIn("board: generic-bk7231n-qfn32", self.wrapper)
        self.assertIn("baud_rate: 115200", self.wrapper)
        self.assertNotIn("generic-bk7231n-qfn32-tuya", self.wrapper)

    def test_wrapper_feeds_the_connection_policy(self):
        self.assertIn("on_connect:", self.wrapper)
        self.assertIn("on_disconnect:", self.wrapper)
        self.assertEqual(self.wrapper.count("set_wifi_connected("), 2)
        self.assertIn("is_connected_with_state_subscription()", self.wrapper)
        self.assertIn("delayed_on: 1s", self.wrapper)
        self.assertIn("trigger_on_initial_state: true", self.wrapper)
        self.assertEqual(self.wrapper.count("set_state_subscriber_connected("), 1)
        for forbidden in ("interval:", "send_wifi_status", "is_connected(true)"):
            self.assertNotIn(forbidden, self.wrapper)
        self.assertNotIn("second byte", self.wrapper.lower())

    def test_wrapper_names_secrets_only(self):
        for key in ("api_key", "ota_password", "wifi_ssid", "wifi_password"):
            self.assertIn(f"!secret", self.wrapper)
            self.assertIn(key, self.wrapper)
        self.assertNotIn("password: \"", self.wrapper)


if __name__ == "__main__":
    unittest.main()
