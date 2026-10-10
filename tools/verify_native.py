#!/usr/bin/env python3
"""Verify all pinned native layout/patch guards offline against a BDS ELF.

Usage: python3 tools/verify_native.py BDS_ELF [--source src]
This complements (does not replace) startup guards and live native tests.
"""
import argparse
import json
import re
from pathlib import Path
from native_abi import Binary
from port_native import byte_list

ARRAYS = {
    'allocation/transient/object_pool.cpp': {
        'kFirstAllocationOriginal': 'kFirstAllocation', 'kSecondAllocationOriginal': 'kSecondAllocation',
        'kFirstReleaseOriginal': 'kFirstRelease', 'kSecondReleaseOriginal': 'kSecondRelease', 'kRedundantCopyOriginal': 'kRedundantCopy'},
    'cache/ownership/optimizer.cpp': {'kOriginal': 'kResolve'},
    'compression/zlib/optimizer.cpp': {'kInitOriginal': 'kDeflateInit', 'kInit2Original': 'kDeflateInit2', 'kEndOriginal': 'kDeflateEnd', 'kDeflateOriginal': 'kDeflate'},
    'compression/zlib/leveldb_optimizer.cpp': {'kOriginal':'kCompress','kInflateInitOriginal':'kInflateInit','kInflateOriginal':'kInflate','kInflateEndOriginal':'kInflateEnd'},
    'timing/monotonic/optimizer.cpp': {'kOriginal': 'kClock'},
    'noise/area/optimizer.cpp': {'kOriginal': 'kReadArea'},
    'noise/construction/direct_hook.cpp': {'kOriginal': 'kConstructor'},
    'noise/construction/reserve.cpp': {'kOriginal': 'kConstructorInit'},
    'noise/distance/optimizer.cpp': {'kOriginal': 'kInitializer'},
    'noise/interpolation/optimizer.cpp': {'kOriginal': 'kMultiOctaveSample'},
    'noise/perlin/optimizer.cpp': {'kOriginal': 'kSample'},
    'noise/perlin/octave_optimizer.cpp': {'kOriginal': 'kEvaluate'},
    'spatial/containment/optimizer.cpp': {'kOriginal': 'kEvaluate'},
}


def constants(text):
    return {name: int(value, 0) for name, value in re.findall(r'\b(k\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;', text)}


def require_bytes(binary, address, expected, name):
    if binary.read(address, len(expected)) != expected:
        raise ValueError(f'native byte guard mismatch: {name} at {address:#x}')


def verify(binary, source):
    layout = (source / 'native/layout.h').read_text()
    expected_id = re.search(r'kSupportedBuildId\[\]\s*=\s*"([a-f0-9]+)"', layout)[1]
    if binary.build_id != expected_id:
        raise ValueError('unsupported BDS GNU build ID: ' + binary.build_id)
    c = constants(layout)
    checked = {'rtti': 0, 'vtableSlots': 0, 'codeGuards': 0, 'initializationFingerprints': 0}
    implementation = (source / 'native/layout.cpp').read_text()
    for name, expected in re.findall(r'check_name\(base, (k\w+), "([^"]+)"\)', implementation):
        require_bytes(binary, binary.pointer(c[name]), expected.encode() + b'\0', name)
        checked['rtti'] += 1
    pairs = [('kOverworldVtable', 'kDimensionBlockSourceSlot', 'kDimensionBlockSourceTarget'),
             ('kBlockSourceVtable', 'kBlockSourceChunkSourceSlot', 'kBlockSourceChunkSourceTarget'),
             ('kLevelStorageWriteBatchVtable', 'kWriteBatchPutSlot', 'kWriteBatchPutTarget')]
    for table, target in [('kMainChunkSourceVtable', 'kMainGetOrLoadTarget'), ('kNetworkChunkSourceVtable', 'kNetworkGetOrLoadTarget'), ('kWorldLimitChunkSourceVtable', 'kMainGetOrLoadTarget')]:
        pairs.extend([(table, 'kGetOrLoadSlot', target), (table, 'kFlushThreadBatchSlot', 'kFlushThreadBatchTarget')])
    for table, slot, target in pairs:
        if binary.pointer(c[table] + c[slot] * 8) != c[target]:
            raise ValueError(f'native vtable guard mismatch: {table}/{slot}')
        checked['vtableSlots'] += 1
    for name, body in re.findall(r'check_code\(base, (k\w+),\s*std::array<unsigned char, \d+>\{([^}]+)\}', implementation):
        require_bytes(binary, c[name], byte_list(body), name)
        checked['codeGuards'] += 1
    for filename, arrays in ARRAYS.items():
        text = (source / filename).read_text()
        values = constants(text)
        for name, target in arrays.items():
            body = re.search(r'constexpr std::array<unsigned char,\s*\w+>\s*' + name + r'\s*\{([^}]+)\}', text)[1]
            require_bytes(binary, values[target], byte_list(body), filename + '/' + name)
            checked['codeGuards'] += 1
    mutex = (source / 'synchronization/shared_mutex/optimizer.cpp').read_text()
    for address, size, body in re.findall(r'\{(0x[0-9a-f]+),\s*(\d+),\s*\{([^}]+)\}', mutex):
        expected = byte_list(body)
        if len(expected) != int(size):
            raise ValueError('inline guard length mismatch')
        require_bytes(binary, int(address, 0), expected, 'shared_mutex')
        checked['codeGuards'] += 1
    shuffle = (source / 'noise/shuffle/optimizer.cpp').read_text()
    for address, body in re.findall(r'Region\{(0x[0-9a-f]+),[^\n]+\n\s*"([0-9a-f]+)"', shuffle):
        require_bytes(binary, int(address, 0), bytes.fromhex(body), 'noise shuffle')
        checked['codeGuards'] += 1
    pool = (source / 'allocation/transient/object_pool.cpp').read_text()
    values = constants(pool)
    expected = int(re.search(r'kInitializationFingerprint = (0x[0-9a-f]+)', pool)[1], 0)
    actual = 0xcbf29ce484222325
    for byte in binary.read(values['kInitializationStart'], values['kInitializationEnd'] - values['kInitializationStart']):
        actual = ((actual ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    if actual != expected:
        raise ValueError('transient initialization fingerprint mismatch')
    checked['initializationFingerprints'] += 1
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary')
    parser.add_argument('--source', type=Path, default=Path('src'))
    args = parser.parse_args()
    binary = Binary(args.binary)
    print(json.dumps({'binary': binary.identity(), 'verified': verify(binary, args.source)}, indent=2))


if __name__ == '__main__':
    main()
