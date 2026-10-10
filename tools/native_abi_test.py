"""Run: PYTHONPATH=.artifacts/python python3 -m unittest discover -s tools -p native_abi_test.py"""
import unittest
from native_abi import code_pattern, find_matches, relative_references


class NativeAbiTest(unittest.TestCase):
    def test_relative_call_target_is_relocated(self):
        old = bytes.fromhex('554889e5e811223344488b05010203044883c001c3')
        new = bytes.fromhex('554889e5e855667788488b05050607084883c001c3')
        self.assertEqual(find_matches(new, code_pattern(old, 0x1000)), [0])

    def test_struct_field_offset_is_not_masked(self):
        old = bytes.fromhex('488b87b80000004883c001c3')
        new = bytes.fromhex('488b87c80000004883c001c3')
        self.assertEqual(find_matches(new, code_pattern(old, 0x1000)), [])

    def test_duplicate_candidates_are_reported(self):
        body = bytes.fromhex('554889e54883c001c3')
        self.assertEqual(find_matches(body + body, code_pattern(body, 0x1000)), [0, len(body)])

    def test_rip_reference_recovers_actual_data_destination(self):
        before = bytes.fromhex('488b0501000000c3')
        after = bytes.fromhex('488b0511000000c3')
        self.assertEqual(relative_references(before, after, 0x1000, 0x2000), [(0x1008, 0x2018, 0x1000)])

    def test_rip_reference_rejects_changed_code(self):
        before = bytes.fromhex('488b0501000000c3')
        after = bytes.fromhex('48890511000000c3')
        self.assertEqual(relative_references(before, after, 0x1000, 0x2000), [])


if __name__ == '__main__':
    unittest.main()
