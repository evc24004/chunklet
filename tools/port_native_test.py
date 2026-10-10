"""Run with native_abi_test.py via unittest discover -s tools -p '*_test.py'."""
import unittest
from port_native import mapped_addresses


class PortNativeTest(unittest.TestCase):
    def test_ambiguous_mapping_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'unresolved'):
            mapped_addresses({'mappings': {'0x1000': {'candidates': ['0x2000', '0x3000']}}})

    def test_missing_mapping_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'unresolved'):
            mapped_addresses({'mappings': {'0x1000': {'candidates': []}}})

    def test_unique_mapping_is_preserved(self):
        self.assertEqual(mapped_addresses({'mappings': {'0x1000': {'candidates': ['0x2000']}}}), {0x1000: 0x2000})
