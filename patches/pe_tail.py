"""Development-only PE tail extension. Structural checks are not runtime proof.

Extends an existing executable last section, preserving its contents. Newly
emitted code must provide correct unwind metadata; this module does not relocate
instructions, infer stack contracts, or validate exception-handler semantics.
"""
import hashlib
import struct
from pathlib import Path

import pefile


def align(value, boundary):
    if boundary < 1 or boundary & (boundary - 1):
        raise ValueError('Alignment must be a positive power of two')
    return (value + boundary - 1) & -boundary


class TailImage:
    def __init__(self, original, expected_sha256):
        self.original = bytes(original)
        self.sha256 = hashlib.sha256(self.original).hexdigest()
        if self.sha256 != expected_sha256:
            raise ValueError('Unexpected source SHA256')
        self.pe = pefile.PE(data=self.original, fast_load=True)
        if self.pe.FILE_HEADER.Machine != 0x8664 or self.pe.OPTIONAL_HEADER.Magic != 0x20b:
            raise ValueError('Only AMD64 PE32+ is supported')
        self.pe.parse_data_directories(directories=[3])
        self.last = self.pe.sections[-1]
        s = self.last
        if s.Name.rstrip(b'\0') != b'.luaux' or s.Characteristics != 0x60000020:
            raise ValueError('Expected existing read/execute .luaux section')
        if s.PointerToRawData + s.SizeOfRawData != len(self.original):
            raise ValueError('Overlay or truncated last section')
        if any(x.VirtualAddress >= s.VirtualAddress for x in self.pe.sections[:-1]):
            raise ValueError('Last section is not last in virtual address order')
        d = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
        if not d.Size or d.Size % 12:
            raise ValueError('Invalid existing exception directory')
        table = self.pe.get_data(d.VirtualAddress, d.Size)
        if len(table) != d.Size:
            raise ValueError('Truncated exception directory')
        self.functions = list(struct.iter_unpack('<III', table))
        self._check_ranges(self.functions)
        self.start = align(max(s.Misc_VirtualSize, s.SizeOfRawData), 16)
        self.payload = bytearray()
        self.added = []
        self.sealed = False

    @staticmethod
    def _check_ranges(entries):
        if any(not (0 <= b < e <= 0xffffffff and 0 < u <= 0xffffffff and u % 4 == 0)
               for b,e,u in entries):
            raise ValueError('Invalid runtime-function range or unwind RVA')
        if any(a[1] > b[0] for a,b in zip(entries, entries[1:])):
            raise ValueError('Overlapping or unsorted runtime functions')

    def append(self, blob, alignment=16):
        if self.sealed:
            raise ValueError('Image is sealed')
        blob = bytes(blob)
        if not blob:
            raise ValueError('Empty append')
        offset = align(self.start + len(self.payload), alignment) - self.start
        self.payload.extend(bytes(offset - len(self.payload)))
        rva = self.last.VirtualAddress + self.start + offset
        if rva + len(blob) > 0xffffffff:
            raise ValueError('PE RVA overflow')
        self.payload.extend(blob)
        return rva

    def add_runtime(self, begin, end, unwind):
        if self.sealed:
            raise ValueError('Image is sealed')
        lo = self.last.VirtualAddress + self.start
        hi = lo + len(self.payload)
        if not lo <= begin < end <= hi:
            raise ValueError('New function must lie inside emitted bytes')
        if lo <= unwind < hi:
            header = self.payload[unwind-lo:unwind-lo+4]
        else:
            header = self.pe.get_data(unwind, 4)
        if len(header) != 4 or header[0] & 7 != 1:
            raise ValueError('Missing or unsupported unwind header')
        proposed = sorted(self.functions + self.added + [(begin,end,unwind)])
        self._check_ranges(proposed)
        self.added.append((begin,end,unwind))

    def chained_fragment(self, begin, end, parent_pc):
        """A fragment using the unchanged parent frame, not a new call frame."""
        parents = [e for e in self.functions if e[0] <= parent_pc < e[1]]
        if len(parents) != 1:
            raise ValueError('Parent PC must have exactly one runtime entry')
        info = self.append(b'\x21\0\0\0' + struct.pack('<III', *parents[0]), 4)
        self.add_runtime(begin, end, info)
        return info

    def finish(self):
        if self.sealed or not self.added:
            raise ValueError('Sealed image or missing new unwind entries')
        entries = sorted(self.functions + self.added)
        self._check_ranges(entries)
        table = b''.join(struct.pack('<III', *e) for e in entries)
        table_rva = self.append(table, 4)
        self.sealed = True
        s = self.last
        used = self.start + len(self.payload)
        raw_size = align(used, self.pe.OPTIONAL_HEADER.FileAlignment)
        image_size = align(s.VirtualAddress + used, self.pe.OPTIONAL_HEADER.SectionAlignment)
        if image_size > 0xffffffff:
            raise ValueError('PE image-size overflow')
        data = bytearray(self.original)
        data.extend(bytes(s.PointerToRawData + raw_size - len(data)))
        offset = s.PointerToRawData + self.start
        data[offset:offset+len(self.payload)] = self.payload
        fields = [(s.get_field_absolute_offset('Misc_VirtualSize'), used),
                  (s.get_field_absolute_offset('SizeOfRawData'), raw_size),
                  (self.pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfImage'), image_size),
                  (self.pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfCode'),
                   self.pe.OPTIONAL_HEADER.SizeOfCode + raw_size - s.SizeOfRawData),
                  (self.pe.OPTIONAL_HEADER.get_field_absolute_offset('CheckSum'), 0)]
        for at,value in fields:
            struct.pack_into('<I', data, at, value)
        directory = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
        struct.pack_into('<II', data, directory.get_file_offset(), table_rva, len(table))
        # Only PE fields above may change in the original file. Every prior
        # code/data byte, including apparent zero padding, remains identical.
        allowed = {i for at,_ in fields for i in range(at,at+4)}
        allowed.update(range(directory.get_file_offset(), directory.get_file_offset()+8))
        for offset in range(0, len(self.original), 1024*1024):
            end = min(offset+1024*1024, len(self.original))
            actual = bytearray(data[offset:end])
            for i in allowed:
                if offset <= i < end:
                    actual[i-offset] = self.original[i]
            if actual != self.original[offset:end]:
                raise ValueError('Unexpected modification to existing image')
        check = pefile.PE(data=bytes(data), fast_load=True)
        check.parse_data_directories(directories=[3])
        actual = [(e.struct.BeginAddress,e.struct.EndAddress,e.struct.UnwindData)
                  for e in check.DIRECTORY_ENTRY_EXCEPTION]
        if actual != entries:
            raise ValueError('Emitted exception directory failed round-trip')
        metadata = {'sourceSha256':self.sha256,'candidateSha256':hashlib.sha256(data).hexdigest(),
                    'addedRuntimeFunctions':self.added,'oldRuntimeFunctionsPreserved':len(self.functions),
                    'payloadRva':s.VirtualAddress+self.start,'payloadBytes':len(self.payload),
                    'exceptionDirectoryRva':table_rva,'exceptionDirectoryBytes':len(table),
                    'status':'structural prototype; runtime and visual behavior unverified'}
        return bytes(data), metadata


def write_new(path, data):
    """Exclusive output; never overwrite an installed executable or evidence."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as out:
        out.write(data)
