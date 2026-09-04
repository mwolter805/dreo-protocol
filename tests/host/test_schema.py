#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests" / "host" / "fixtures" / "schema"


class IntegerCommandWidthSchemaTest(unittest.TestCase):
    def run_config(self, fixture: str, expected_success: bool):
        result = subprocess.run(
            [sys.executable, "-m", "esphome", "config", str(FIXTURES / fixture)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        if expected_success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, fixture + " unexpectedly validated")
            self.assertIn("valid options are '1', '2', '4'", result.stdout + result.stderr)

    def test_valid_widths_and_omitted_default(self):
        self.run_config("widths-valid.yaml", True)
        self.run_config("widths-omitted.yaml", True)
        self.run_config("policy-true.yaml", True)

    def test_invalid_widths(self):
        for fixture in ("width-0.yaml", "width-3.yaml", "width-8.yaml"):
            with self.subTest(fixture=fixture):
                self.run_config(fixture, False)


if __name__ == "__main__":
    unittest.main()
