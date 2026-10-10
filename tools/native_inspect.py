#!/usr/bin/env python3
"""Read-only bounded ELF inspection, preserving exact binary identity.
Usage: PYTHONPATH=.artifacts/python python3 tools/native_inspect.py BDS_ELF
  [--address 0xRVA --size 1024] [--calls-to 0xRVA ...] [--type NAME]
  --output REPORT.json
Direct-call byte matches are candidates, not validated instruction boundaries.
No binary execution, mutation or inferred offsets. See tools/README.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from native_abi import Binary


def direct_calls(body,base,targets):
    result=[]
    for match in re.finditer(b'(?=\xe8....)',body,re.DOTALL):
        at=match.start(); target=base+at+5+struct.unpack_from('<i',body,at+1)[0]
        if target in targets:result.append({'address':hex(base+at),'target':hex(target)})
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary');parser.add_argument('--address',type=lambda s:int(s,0))
    parser.add_argument('--size',type=int,default=1024)
    parser.add_argument('--calls-to',nargs='*',default=[],type=lambda s:int(s,0))
    parser.add_argument('--type',action='append',default=[])
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if not 1<=args.size<=16384:parser.error('size must be 1..16384')
    binary=Binary(args.binary); result={'binary':binary.identity()}
    if args.address is not None:
        body=binary.read(args.address,args.size)
        result['range']={'address':hex(args.address),'bytes':len(body),'sha256':hashlib.sha256(body).hexdigest(),
                         'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str} for i in Cs(CS_ARCH_X86,CS_MODE_64).disasm(body,args.address)]}
    if args.calls_to:
        result['directCallCandidates']=[call for segment in binary.code for call in direct_calls(segment.data(),segment['p_vaddr'],set(args.calls_to))]
    if args.type:
        result['types']={}
        for name in args.type:
            entries=[]
            for address in binary.string_matches(name.encode()):
                for name_slot in binary.addresses(address):
                    for type_slot in binary.addresses(name_slot-8):
                        entries.append({'nameAddress':hex(address),'typeAddress':hex(name_slot-8),'addressPoint':hex(type_slot+8),
                                        'slots':[hex(binary.pointer(type_slot+8+8*i)) for i in range(8)]})
            result['types'][name]=entries
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'output':str(args.output),'buildId':binary.build_id,'callCandidates':len(result.get('directCallCandidates',[]))}))

if __name__=='__main__':main()
