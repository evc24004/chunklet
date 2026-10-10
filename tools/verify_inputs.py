#!/usr/bin/env python3
"""Verify exact named dependency trees using the maintained SDK pin format.

Usage: python3 tools/verify_inputs.py ZLIB_SOURCE EXPECTED_LITE_SOURCE
Does not follow symlink directories or inspect paths outside its arguments.
"""
import argparse
import hashlib
import os
import stat
from pathlib import Path

PINS = {'zlib': '1c61e493a656d0c5879667d462bdd2f99845966a4a87e929f654acce6f7827f4',
        'expected': 'c760bfc375ba6e2349dfc097dcc181da448839e83f064620b812174f428cc856'}


def tree_hash(root):
    files = []
    for directory, directories, names in os.walk(root):
        if Path(directory) == root:
            directories[:] = [value for value in directories if value != '.git']
            names = [value for value in names if value != '.git']
        for name in list(directories):
            if (Path(directory) / name).is_symlink():
                directories.remove(name)
                names.append(name)
        files.extend(Path(directory) / name for name in names)
    total = hashlib.sha256()
    for path in sorted(files, key=lambda value: str(value.relative_to(root)).encode()):
        metadata = path.lstat()
        if stat.S_ISLNK(metadata.st_mode):
            kind, executable, content = 'link', '-', os.readlink(path).encode()
        elif stat.S_ISREG(metadata.st_mode):
            kind, executable, content = 'file', 'x' if metadata.st_mode & 0o111 else '-', path.read_bytes()
        else:
            raise ValueError(f'unsupported dependency entry: {path}')
        digest = hashlib.sha256(content).hexdigest()
        total.update(f'{path.relative_to(root)}\0{kind}\0{executable}\0{digest}\n'.encode())
    return total.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zlib', type=Path)
    parser.add_argument('expected', type=Path)
    args = parser.parse_args()
    for name, expected in PINS.items():
        actual = tree_hash(getattr(args, name).resolve())
        if actual != expected:
            raise ValueError(f'{name} input hash mismatch: {actual}')
        print(f'{name} source pin verified: {actual}')


if __name__ == '__main__':
    main()
