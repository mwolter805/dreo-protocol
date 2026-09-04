#!/usr/bin/env python3

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


def wifi_policy_errors(config: str) -> list[str]:
    """Return portable DR-HEC005S Wi-Fi/API policy contract violations."""
    errors = []
    required = {
        "restart-mode script": "id: reconcile_wifi_icon",
        "script restart ownership": "mode: restart",
        "initialization wait": "DreoInitState::INIT_DONE",
        "five-second disconnected recheck": "delay: 5s",
        "Wi-Fi connect route": "on_connect:",
        "Wi-Fi disconnect route": "on_disconnect:",
        "internal API-state sensor": "internal: true",
        "initial API-state route": "trigger_on_initial_state: true",
        "state-subscriber filter": "is_connected_with_state_subscription()",
        "subscriber debounce": "delayed_on: 1s",
    }
    for label, fragment in required.items():
        if fragment not in config:
            errors.append(f"missing {label}")

    if config.count("wifi.connected:") < 2:
        errors.append("missing disconnected-state delay and recheck")
    if config.count("script.execute: reconcile_wifi_icon") != 3:
        errors.append("connect, disconnect, and API-state routes are not exact")
    for method in (
        "send_wifi_status_off()",
        "send_wifi_status_flash()",
        "send_wifi_status_solid()",
    ):
        if config.count(method) != 1:
            errors.append(f"status method is not used exactly once: {method}")

    forbidden = {
        "script parameters": "connected: bool",
        "forced dp25 write": "force_set_boolean_datapoint_value(25",
        "candidate status method": "send_candidate_wifi_status_once",
        "experiment marker": "experimental_full_report",
        "periodic sender": "interval:",
        "nonexistent API overload": "is_connected(true)",
    }
    for label, fragment in forbidden.items():
        if fragment in config:
            errors.append(f"forbidden {label}")
    return errors


class Phase1SourceContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.yaml = (ROOT / "example-dreo-hec005s-mbl01.yaml").read_text()
        cls.header = (ROOT / "components/dreo/dreo.h").read_text()
        cls.core = (ROOT / "components/dreo/dreo.cpp").read_text()

    def test_model_traits_are_bounded_without_a_corrective_write(self):
        mist = self.yaml.split("id: mist_level", 1)[1].split("\n\n", 1)[0]
        self.assertIn("min_value: 1", mist)
        self.assertIn("max_value: 4", mist)
        self.assertIn("clamp_reported_value: true", mist)
        low_water = self.yaml.split("id: low_water", 1)[1].split("\n\n", 1)[0]
        self.assertIn("bitmask: 0x01", low_water)

    def test_wifi_api_policy_contract(self):
        self.assertEqual(wifi_policy_errors(self.yaml), [])

    def test_wifi_api_policy_checker_rejects_required_wiring_mutation(self):
        mutated = self.yaml.replace("on_disconnect:", "on_disconnect_removed:", 1)
        self.assertIn("missing Wi-Fi disconnect route", wifi_policy_errors(mutated))

    def test_wifi_api_policy_checker_rejects_forbidden_construct_mutation(self):
        mutated = self.yaml + """

interval:
  - interval: 30s
    then:
      - lambda: id(dreo_hub).send_candidate_wifi_status_once(3);
"""
        self.assertIn("forbidden candidate status method", wifi_policy_errors(mutated))
        self.assertIn("forbidden periodic sender", wifi_policy_errors(mutated))

    def test_named_status_methods_use_one_exact_private_helper(self):
        declarations = (
            "void send_wifi_status_off();",
            "void send_wifi_status_flash();",
            "void send_wifi_status_solid();",
        )
        for declaration in declarations:
            self.assertIn(declaration, self.header)
        self.assertIn("private:\n  void send_wifi_status_(uint8_t status);", self.header)

        expected = {
            "off": "0x00",
            "flash": "0x03",
            "solid": "0x05",
        }
        for name, status in expected.items():
            wrapper = re.search(
                rf"void Dreo::send_wifi_status_{name}\(\) \{{(?P<body>.*?)\n\}}",
                self.core,
                re.DOTALL,
            )
            self.assertIsNotNone(wrapper)
            self.assertIn(f"this->send_wifi_status_({status});", wrapper.group("body"))

        helper = re.search(
            r"void Dreo::send_wifi_status_\(uint8_t status\) \{(?P<body>.*?)\n\}",
            self.core,
            re.DOTALL,
        )
        self.assertIsNotNone(helper)
        self.assertIn(".cmd = DreoCommandType::WIFI_STATE", helper.group("body"))
        self.assertIn(".payload = {status, 0x00}", helper.group("body"))
        self.assertNotIn("send_candidate_wifi_status_once", self.header + self.core)

    def test_full_table_request_is_guarded_and_diagnostic(self):
        request = re.search(
            r"bool Dreo::request_full_datapoint_report_once\(\) \{(?P<body>.*?)\n\}",
            self.core,
            re.DOTALL,
        )
        self.assertIsNotNone(request)
        for guard in (
            "this->init_state_ != DreoInitState::INIT_DONE",
            "this->datapoints_.empty()",
            "!this->command_queue_.empty()",
            "this->expected_response_.has_value()",
            "this->has_pending_transitions_()",
            "this->reconciliation_route_ != DreoReconciliationRoute::NONE",
            "this->notification_reconciliation_due_ != 0",
        ):
            self.assertIn(guard, request.group("body"))
        self.assertIn(".cmd = DreoCommandType::DATAPOINT_REPORT", request.group("body"))
        self.assertIn(".diagnostic_full_report = true", request.group("body"))
        self.assertIn("bool diagnostic_full_report{false};", self.header)
        self.assertNotIn("experimental_full_report", self.header + self.core)
        self.assertNotIn("Experimental full-table", self.core)


if __name__ == "__main__":
    unittest.main()
