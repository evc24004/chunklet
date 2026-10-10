"""Run with unittest discover -s tools -p '*_test.py'."""
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from verify_inputs import tree_hash


class VerifyInputsTest(unittest.TestCase):
    def test_content_mutation_changes_digest(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            file = root / 'source.cpp'
            file.write_text('old')
            first = tree_hash(root)
            file.write_text('changed')
            self.assertNotEqual(first, tree_hash(root))

    def test_symlink_target_is_hashed_without_following_it(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'link').symlink_to('/nonexistent/source')
            self.assertEqual(len(tree_hash(root)), 64)
