"""Execute actual ELF flag checker against resident and ordinary shared objects."""
import subprocess
import tempfile
import unittest
from pathlib import Path
try:
    from verify_residency import verify
except ImportError:
    verify=None
class ResidencyTest(unittest.TestCase):
    def test_only_nodelete_artifact_is_admitted(self):
        self.assertIsNotNone(verify)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'fixture.so'
            for resident in (False,True):
                subprocess.run(['cc','-x','c','-shared','-fPIC','-o',str(path),'-']+(['-Wl,-z,nodelete'] if resident else []),input='int fixture(void){return 42;}',text=True,check=True)
                if resident:self.assertTrue(verify(path))
                else:
                    with self.assertRaisesRegex(ValueError,'NODELETE'):verify(path)
if __name__=='__main__':unittest.main()
