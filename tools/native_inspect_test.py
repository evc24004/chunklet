import unittest
try:
    from native_inspect import direct_calls
except ImportError:
    direct_calls = None
class InspectTests(unittest.TestCase):
    def test_direct_call_target_is_signed_and_relative(self):
        self.assertIsNotNone(direct_calls)
        self.assertEqual(direct_calls(bytes.fromhex('90e8faffffffc3'),0x1000,{0x1000}),[{'address':'0x1001','target':'0x1000'}])
    def test_other_call_targets_are_excluded(self):
        self.assertIsNotNone(direct_calls)
        self.assertEqual(direct_calls(bytes.fromhex('e800000000c3'),0x1000,{0x9999}),[])
    def test_overlapping_candidate_does_not_hide_real_call(self):
        self.assertEqual(direct_calls(bytes.fromhex('e8e8faffffff'),0x1000,{0x1000}),[{'address':'0x1001','target':'0x1000'}])
