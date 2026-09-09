#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests" / "host" / "fixtures" / "schema"


class SchemaTestBase(unittest.TestCase):
    def validate(self, fixture: str):
        return subprocess.run(
            [sys.executable, "-m", "esphome", "config", str(FIXTURES / fixture)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )

    def expect_valid(self, fixture: str):
        result = self.validate(fixture)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def expect_rejected(self, fixture: str, message: str):
        result = self.validate(fixture)
        self.assertNotEqual(result.returncode, 0, fixture + " unexpectedly validated")
        self.assertIn(message, result.stdout + result.stderr)


class IntegerCommandWidthSchemaTest(SchemaTestBase):
    def run_config(self, fixture: str, expected_success: bool):
        if expected_success:
            self.expect_valid(fixture)
        else:
            self.expect_rejected(fixture, "valid options are '1', '2', '4'")

    def test_valid_widths_and_omitted_default(self):
        self.run_config("widths-valid.yaml", True)
        self.run_config("widths-omitted.yaml", True)
        self.run_config("policy-true.yaml", True)

    def test_invalid_widths(self):
        for fixture in ("width-0.yaml", "width-3.yaml", "width-8.yaml"):
            with self.subTest(fixture=fixture):
                self.run_config(fixture, False)


class SenderOptionSchemaTest(SchemaTestBase):
    """The two opt-in sender settings: omitted, explicit and out-of-range."""

    def test_omitted_and_explicit_are_valid(self):
        self.expect_valid("widths-omitted.yaml")
        self.expect_valid("sender-valid.yaml")

    def test_command_spacing_is_bounded(self):
        self.expect_rejected("spacing-zero.yaml", "value must be at least 1ms")
        self.expect_rejected("spacing-too-long.yaml", "value must be at most 1000ms")

    def test_wifi_status_second_byte_is_a_byte(self):
        self.expect_rejected("status-byte-too-large.yaml", "wifi_status_second_byte")

    def test_report_acknowledgement_is_boolean(self):
        self.expect_rejected("acknowledge-invalid.yaml", "boolean value")

    def test_numeric_button_event_automation_is_valid(self):
        self.expect_valid("sender-valid.yaml")


class StringAdapterSchemaTest(SchemaTestBase):
    def test_configurable_writable_and_read_only_strings_are_valid(self):
        self.expect_valid("text-constraints-valid.yaml")

    def test_minimum_length_cannot_exceed_maximum(self):
        self.expect_rejected(
            "text-constraints-invalid.yaml", "min_length must not exceed max_length"
        )


class Hpf007sProductSchemaTest(SchemaTestBase):
    """The hardware-free DR-HPF007S package and its product platforms."""

    def test_package_validates(self):
        self.expect_valid("hpf007s-package-valid.yaml")

    def test_enum_command_type_is_bounded(self):
        self.expect_rejected("hpf007s-enum-type-invalid.yaml", "enum_command_type")

    def test_sweep_options_are_stock_widths(self):
        self.expect_rejected("hpf007s-sweep-invalid.yaml", "not a stock sweep width")

    def test_number_has_exactly_one_role(self):
        self.expect_rejected("hpf007s-number-role-invalid.yaml", "axis")

    def test_gradient_select_names_its_own_options(self):
        self.expect_rejected("hpf007s-select-role-invalid.yaml", "options belong to a sweep select")


if __name__ == "__main__":
    unittest.main()
