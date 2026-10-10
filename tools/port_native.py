#!/usr/bin/env python3
"""Apply an entirely resolved native_abi.py report to a clean old-version source.

Usage: python3 tools/port_native.py REPORT.json --source src [--apply]
Defaults to a dry-run. Verifies exact ELF hashes/build IDs and original guard
bytes before refreshing mapped addresses and instruction guards. Never resolves
ambiguous mappings. Review the diff and run an actual native runtime proof.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
from native_abi import Binary


def mapped_addresses(report):
    result = {}
    for address, item in report['mappings'].items():
        if len(item['candidates']) != 1:
            raise ValueError(f'unresolved native address {address}')
        result[int(address, 0)] = int(item['candidates'][0], 0)
    return result


def byte_list(body):
    return bytes(int(value, 16) for value in re.findall(r'0x[0-9a-fA-F]+', body))


def format_bytes(body):
    return '\n    ' + ', '.join(f'0x{byte:02x}' for byte in body) + ',\n'


def update_guards(text, old, new, mapping):
    addresses = {int(value, 16) for value in re.findall(r'0x[0-9a-fA-F]+', text)} & mapping.keys()
    def array(match):
        original = byte_list(match['body'])
        if len(original) != int(match['size']):
            raise ValueError(f'array has unexpected initializer: {match["name"]}')
        candidates = [at for at in addresses if old.executable(at) and old.read(at, len(original)) == original]
        if not candidates:
            # Non-guard arrays (e.g. a generated jump sequence) remain untouched.
            return match[0]
        replacement = {new.read(mapping[at], len(original)) for at in candidates}
        if len(replacement) != 1:
            raise ValueError(f'ambiguous byte guard {match["name"]}')
        actual = replacement.pop()
        return match[0] if actual == original else match['head'] + format_bytes(actual) + '};'
    text = re.sub(r'(?P<head>constexpr std::array<unsigned char,\s*(?P<size>\d+)>\s+(?P<name>\w+)\s*\{)(?P<body>[^}]+)\};', array, text)
    def inline(match):
        address = int(match['address'], 0)
        original = byte_list(match['body'])
        size = int(match['size'])
        if address not in mapping or len(original) != size or old.read(address, size) != original:
            raise ValueError('invalid inline native instruction guard')
        actual = new.read(mapping[address], size)
        return match[0] if original == actual else match['head'] + format_bytes(actual) + '}'
    text = re.sub(r'(?P<head>\{(?P<address>0x[0-9a-f]+),\s*(?P<size>\d+),\s*\{)(?P<body>[^}]+)\}', inline, text)
    def region(match):
        address = int(match['address'], 0)
        original = bytes.fromhex(match['bytes'])
        if old.read(address, len(original)) != original:
            raise ValueError('invalid shuffle guard')
        return match['head'] + new.read(mapping[address], len(original)).hex() + '"'
    text = re.sub(r'(?P<head>Region\{(?P<address>0x[0-9a-f]+),[^\n]+\n\s*")(?P<bytes>[0-9a-f]+)"', region, text)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--source', type=Path, default=Path('src'))
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    old, new = Binary(report['old']['path']), Binary(report['new']['path'])
    if old.identity() != report['old'] or new.identity() != report['new']:
        raise ValueError('report ELF identity changed')
    mapping = mapped_addresses(report)
    updates = []
    for path in sorted(args.source.rglob('*')):
        if path.suffix not in ('.cpp', '.h'):
            continue
        original = path.read_text()
        updated = update_guards(original, old, new, mapping)
        updated = re.sub(r'0x[0-9a-fA-F]+', lambda m: hex(mapping[int(m[0], 16)]) if int(m[0], 16) in mapping else m[0], updated)
        updated = updated.replace(old.build_id, new.build_id)
        if original != updated:
            updates.append((path, original, updated))
    for path, original, updated in updates:
        if args.apply:
            if path.read_text() != original:
                raise ValueError(f'source changed concurrently: {path}')
            path.write_text(updated)
        print(f'{"updated" if args.apply else "would update"}: {path}')
    print(f'{len(updates)} files; {len(mapping)} exact mappings; report SHA256={hashlib.sha256(args.report.read_bytes()).hexdigest()}')


if __name__ == '__main__':
    main()
