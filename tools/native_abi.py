#!/usr/bin/env python3
"""Offline, read-only native port evidence. Never admits or patches a binary.

Usage: python3 tools/native_abi.py OLD_ELF NEW_ELF --source src --output REPORT.json
Dependencies: capstone==5.0.6, pyelftools==0.32 (see tools/README.md).
Reports unique relocation-normalized instruction matches and RTTI matches.
Ambiguous or absent matches require manual disassembly; no offset guessing.
"""
import argparse
import hashlib
import io
import json
import re
import struct
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_GRP_JUMP, CS_GRP_CALL
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile


def code_pattern(body, address):
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    mask = bytearray(b'\x01' * len(body))
    end = 0
    for ins in decoder.disasm(body, address):
        offset = ins.address - address
        if ins.group(CS_GRP_JUMP) or ins.group(CS_GRP_CALL):
            mask[offset + ins.imm_offset:offset + ins.imm_offset + ins.imm_size] = b'\0' * ins.imm_size
        if any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in ins.operands):
            mask[offset + ins.disp_offset:offset + ins.disp_offset + ins.disp_size] = b'\0' * ins.disp_size
        end = offset + ins.size
    return b''.join(re.escape(bytes([b])) if m else b'.' for b, m in zip(body[:end], mask[:end]))


def find_matches(body, pattern):
    compiled = re.compile(pattern, re.DOTALL)
    results = []
    cursor = 0
    while len(results) < 100:
        match = compiled.search(body, cursor)
        if match is None:
            break
        results.append(match.start())
        cursor = match.start() + 1
    return results


class Binary:
    def __init__(self, path):
        self.path = str(path)
        self.data = Path(path).read_bytes()
        self.elf = ELFFile(io.BytesIO(self.data))
        if self.elf['e_machine'] != 'EM_X86_64' or not self.elf.little_endian:
            raise ValueError('requires little-endian x86-64 ELF')
        self.segments = [p for p in self.elf.iter_segments() if p['p_type'] == 'PT_LOAD']
        self.code = [p for p in self.segments if p['p_flags'] & 1]
        self.relocs = {r['r_offset']: r['r_addend'] for r in self.elf.get_section_by_name('.rela.dyn').iter_relocations() if r['r_info_type'] == 8}
        self.build_id = next(note['n_desc'] for section in self.elf.iter_sections() if section['sh_type'] == 'SHT_NOTE' for note in section.iter_notes() if note['n_type'] == 'NT_GNU_BUILD_ID')

    def read(self, address, size):
        for seg in self.segments:
            if seg['p_vaddr'] <= address and address + size <= seg['p_vaddr'] + seg['p_filesz']:
                start = seg['p_offset'] + address - seg['p_vaddr']
                return self.data[start:start + size]
        raise ValueError(f'address not file backed: {address:#x}')

    def pointer(self, address):
        return self.relocs.get(address, struct.unpack('<Q', self.read(address, 8))[0])

    def executable(self, address):
        return any(p['p_vaddr'] <= address < p['p_vaddr'] + p['p_filesz'] for p in self.code)

    def addresses(self, value):
        return [slot for slot, target in self.relocs.items() if target == value]

    def string_matches(self, body):
        results = []
        for seg in self.segments:
            for at in find_matches(seg.data(), re.escape(body + b'\0')):
                results.append(seg['p_vaddr'] + at)
        return results

    def identity(self):
        return {'path': self.path, 'buildId': self.build_id, 'sha256': hashlib.sha256(self.data).hexdigest()}


def map_code(old, new, address):
    # Use a long context to avoid treating common function prologues as evidence.
    for size in (2048, 256, 128, 64):
        pattern = code_pattern(old.read(address, size), address)
        candidates = [seg['p_vaddr'] + at for seg in new.code for at in find_matches(seg.data(), pattern)]
        if candidates:
            return {'method': 'relocation-normalized-code', 'bytes': size, 'candidates': [hex(v) for v in candidates]}
    return {'method': 'relocation-normalized-code', 'candidates': []}


def map_rtti(old, new, address, name):
    slot = old.pointer(address - 8) + 8 if name.endswith('Vtable') else address
    type_name = old.read(old.pointer(slot), 256).split(b'\0', 1)[0]
    candidates = []
    for string_at in new.string_matches(type_name):
        for type_slot in new.addresses(string_at):
            if name.endswith('Vtable'):
                candidates.extend(v + 8 for v in new.addresses(type_slot - 8))
            else:
                candidates.append(type_slot)
    return {'method': 'rtti-name', 'name': type_name.decode(), 'candidates': [hex(v) for v in candidates]}


def relative_references(old_body, new_body, old_address, new_address):
    if re.fullmatch(code_pattern(old_body, old_address), new_body, re.DOTALL) is None:
        return []
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    refs = []
    for a, b in zip(decoder.disasm(old_body, old_address), decoder.disasm(new_body, new_address)):
        for ao, bo in zip(a.operands, b.operands):
            if ao.type == X86_OP_MEM and ao.mem.base == X86_REG_RIP:
                refs.append((a.address + a.size + ao.mem.disp, b.address + b.size + bo.mem.disp, a.address))
    return refs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('old')
    parser.add_argument('new')
    parser.add_argument('--source', type=Path, default=Path('src'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reuse', type=Path, help='reuse prior candidate report after exact input hash validation')
    parser.add_argument('--probe', action='append', default=[], help='extra disassembled code start (hex), e.g. containing function prologue')
    args = parser.parse_args()
    old, new = Binary(args.old), Binary(args.new)
    previous = json.loads(args.reuse.read_text()) if args.reuse else None
    if previous and (previous['old'] != old.identity() or previous['new'] != new.identity()):
        raise ValueError('cached report inputs differ')
    found = {}
    for path in sorted(args.source.rglob('*')):
        if path.suffix not in ('.cpp', '.h'):
            continue
        for line in path.read_text().splitlines():
            for literal in re.findall(r'0x[0-9a-fA-F]+', line):
                address = int(literal, 16)
                if not 0x4000000 <= address < 0x10000000:
                    continue
                item = found.setdefault(address, {'references': []})
                item['references'].append({'path': str(path), 'line': line.strip()})
    for probe in args.probe:
        found.setdefault(int(probe, 0), {'references': [{'path': 'manual-prologue-probe', 'line': probe}]})
    for address, item in found.items():
        cached = previous['mappings'].get(hex(address)) if previous else None
        if cached and len(cached['candidates']) == 1:
            item.update(cached)
            continue
        if old.executable(address):
            item.update(map_code(old, new, address))
        else:
            match = re.search(r'(k\w+(?:Vtable|TypeNameSlot))\s*=', item['references'][0]['line'])
            item.update(map_rtti(old, new, address, match[1]) if match else {'method': 'requires-data-reference', 'candidates': []})
        print(f'{address:#x}: {item["method"]} {item["candidates"]}', flush=True)
    data_refs = {}
    for address, item in found.items():
        if item['method'] != 'relocation-normalized-code' or len(item['candidates']) != 1:
            continue
        destination = int(item['candidates'][0], 0)
        size = item['bytes']
        # Use complete instruction prefix (the scanner may truncate a tail instruction).
        decoder = Cs(CS_ARCH_X86, CS_MODE_64)
        decoded = list(decoder.disasm(old.read(address, size), address))
        size = decoded[-1].address + decoded[-1].size - address
        for before, after, ins_at in relative_references(old.read(address, size), new.read(destination, size), address, destination):
            data_refs.setdefault(before, []).append({'target': hex(after), 'instruction': hex(ins_at)})
    for address, item in found.items():
        if item['method'] == 'requires-data-reference' and address in data_refs:
            item.update(method='matched-code-rip-reference', candidates=sorted({ref['target'] for ref in data_refs[address]}), evidence=data_refs[address])
        elif item['method'] == 'rtti-name' and 'Vtable' in item['references'][0]['line']:
            item['candidates'] = [value for value in item['candidates'] if new.pointer(int(value, 0) - 16) == old.pointer(address - 16)]
            for slot in (13, 53):
                known = found.get(old.pointer(address + slot * 8))
                if known and len(known['candidates']) == 1:
                    item['candidates'] = [value for value in item['candidates'] if new.pointer(int(value, 0) + slot * 8) == int(known['candidates'][0], 0)]
        if len(item['candidates']) != 1:
            print(f'UNRESOLVED {address:#x}: {item["candidates"]}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'old': old.identity(), 'new': new.identity(), 'mappings': {hex(a): v for a, v in found.items()}}, indent=2) + '\n')


if __name__ == '__main__':
    main()
