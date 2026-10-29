"""Generate the bounded transform-buffer retention adapter.

RCX -> an already-cleared native three-pointer vector. Only RAX/RDX/flags change.
The old native deallocator is the tail fallback; it is not included/executed here.
"""
import struct

LIMIT = 8 * 1024 * 1024  # per owner, not a process-wide memory bound
SITE = 0x4b1133e
ORIGINAL = 0x42cbb80
EXPECTED = bytes.fromhex('e83da87bff')

def payload(payload_rva, fallback_rva=ORIGINAL):
    # Empty, nonnegative, record-aligned capacity at most LIMIT may be retained.
    # Explicit labelled emitter keeps this small adapter reproducible.
    code = bytearray(); fix = []
    code += bytes.fromhex('488b01')                 # mov rax,[rcx]
    code += bytes.fromhex('483b4108')               # cmp rax,[rcx+8]
    fix.append(len(code)); code += b'\x75\x00'      # jne fallback
    code += bytes.fromhex('488b5110 483bd0')        # cap >= begin; reject wraparound
    fix.append(len(code)); code += b'\x72\x00'      # jb fallback
    code += bytes.fromhex('482bd0')                 # rdx=cap-begin (unsigned)
    code += bytes.fromhex('4881fa') + struct.pack('<I', LIMIT)
    fix.append(len(code)); code += b'\x77\x00'      # ja fallback
    code += bytes.fromhex('f6c27f')                 # test dl,127
    fix.append(len(code)); code += b'\x75\x00'      # jne fallback
    code += b'\xc3'                                # ret; no stack change
    fallback = len(code)
    for at in fix:
        code[at+1] = fallback-(at+2)
    code += b'\xe9' + struct.pack('<i', fallback_rva-(payload_rva+len(code)+5))
    return bytes(code)

def replacement(payload_rva):
    return b'\xe8' + struct.pack('<i', payload_rva-(SITE+5))

def retain(begin, end, cap):
    return begin == end and cap >= begin and cap-begin <= LIMIT and (cap-begin) % 128 == 0
