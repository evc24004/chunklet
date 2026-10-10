#!/usr/bin/env python3
"""Require native-hook code residency after Endstone dlclose.
Usage: python3 tools/verify_residency.py PLUGIN.so
Run automatically by build.sh. Tests: python3 -m unittest discover -s tools -p residency_test.py
"""
import argparse
from pathlib import Path
import re
import subprocess

def verify(path):
    result=subprocess.run(['readelf','-d',str(path)],check=True,text=True,capture_output=True,timeout=10)
    flags=re.findall(r'\(FLAGS_1\)\s+Flags:\s*([^\n]+)',result.stdout)
    if not any('NODELETE' in flag.split() for flag in flags):
        raise ValueError('Native hook artifact requires ELF DF_1_NODELETE')
    return True

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('artifact',type=Path)
    args=parser.parse_args();verify(args.artifact)
    print('PASS: ELF DF_1_NODELETE retains native hook code until process exit')
if __name__=='__main__':main()
