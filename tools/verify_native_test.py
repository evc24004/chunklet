"""Run: PYTHONPATH=.artifacts/python python3 -m unittest discover -s tools -p '*_test.py'."""
from pathlib import Path
from types import SimpleNamespace
import unittest
from verify_native import verify, require_bytes


class VerifyNativeTest(unittest.TestCase):
    def test_unknown_build_is_refused_before_any_native_read(self):
        binary = SimpleNamespace(build_id='unrecognized')
        with self.assertRaisesRegex(ValueError, 'unsupported BDS GNU build ID'):
            verify(binary, Path(__file__).resolve().parent.parent / 'src')

    def test_instruction_mutation_is_refused(self):
        binary = SimpleNamespace(read=lambda address, size: b'\x90\xc3')
        with self.assertRaisesRegex(ValueError, 'native byte guard mismatch'):
            require_bytes(binary, 0x1000, b'\x55\xc3', 'function')

    def test_instruction_match_is_accepted(self):
        binary = SimpleNamespace(read=lambda address, size: b'\x55\xc3')
        require_bytes(binary, 0x1000, b'\x55\xc3', 'function')
