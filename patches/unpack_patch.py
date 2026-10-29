"""Exact-build, in-place unpack fast-path candidate; never edits its input."""
import argparse
import hashlib
import json
from pathlib import Path
import pefile

ORIGINAL_SHA256='81be5bc2916ba979e0ecc68b5fd039a48821765c6e9a7bcfd8d27ad877cf1692'
RVA=0x316a870
EXPECTED=bytes.fromhex('83fb017537413b76087f3185f67e2133d2448bc6498b4e18488d5210488b47700f104411f00f114410f04983e80175e48bc648c1e00448014770eb2a')
# ebx=start, esi=count>0, r14=table, rdi=state; enclosing function saves RSI/RDI.
# Reject unsigned(start-1)>=size or count>size-(start-1); retain original fallback.
# Preserve return count in eax, copy 2 qwords/value, publish L->top after copying.
REPLACEMENT=bytes.fromhex('8d43ff413b46087333418b4e082bc83bf1772948c1e004490346188bce488bf08bc103c94c8bd7488b7f70f348a549897a70eb34')+b'\x90'*8
assert len(EXPECTED)==len(REPLACEMENT)==60

def candidate(source,destination):
    assert source.resolve()!=destination.resolve(),'Refusing to overwrite input'
    data=source.read_bytes();assert hashlib.sha256(data).hexdigest()==ORIGINAL_SHA256,'Unsupported Studio build'
    pe=pefile.PE(data=data,fast_load=True);offset=pe.get_offset_from_rva(RVA)
    assert data[offset:offset+60]==EXPECTED,'Unexpected original instructions'
    modified=data[:offset]+REPLACEMENT+data[offset+60:]
    with destination.open('xb') as out:out.write(modified)
    result={'scope':'table.unpack library subrange only; no FASTCALL/create/move modifications','original_sha256':ORIGINAL_SHA256,'candidate_sha256':hashlib.sha256(modified).hexdigest(),'rva':hex(RVA),'file_offset':hex(offset),'size':60,'original_hex':EXPECTED.hex(),'replacement_hex':REPLACEMENT.hex(),'signature':'Original Authenticode signature invalidated by code modification; no verification or security setting is bypassed.'}
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('destination',type=Path);p.add_argument('--report',type=Path);args=p.parse_args()
    result=candidate(args.source,args.destination);text=json.dumps(result,indent=2)
    if args.report:args.report.write_text(text+'\n')
    print(text)
