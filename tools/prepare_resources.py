"""Validate patch instructions and build Windows installer resources."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]

def packed_profile(path):
    p = json.loads(Path(path).read_text(encoding="utf-8"))
    if p["format"] != 2 or not 0 < len(p["edits"]) <= 128:
        raise ValueError("Unsupported profile")
    source, target = p["source_size"], p["target_size"]
    out = bytearray(b"LABPAT02" + struct.pack("<QQI", source, target, len(p["edits"])))
    last = 0
    for edit in p["edits"]:
        offset, length = edit["offset"], edit["length"]
        if not last <= offset <= target or not 0 < length <= target-offset:
            raise ValueError("Invalid edit range")
        copy = "copy_from" in edit
        out += struct.pack("<QII", offset, length, int(copy))
        if copy:
            start = edit["copy_from"]
            if offset < source or not 0 <= start <= source-length:
                raise ValueError("Invalid copy range")
            out += struct.pack("<Q", start)
        else:
            data = bytes.fromhex(edit["hex"])
            if len(data) != length:
                raise ValueError("Invalid literal length")
            out += data
        last = offset + length
    if last != target or hashlib.sha256(out).hexdigest() != p["packed_sha256"]:
        raise ValueError("Profile digest mismatch")
    for key in ("source_sha256", "target_sha256"):
        if len(bytes.fromhex(p[key])) != 32:
            raise ValueError("Invalid image digest")
    return p, bytes(out)

def compress(data):
    cabinet = ctypes.WinDLL("cabinet", use_last_error=True)
    create = cabinet.CreateCompressor
    create.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    create.restype = ctypes.c_int
    run = cabinet.Compress
    run.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    run.restype = ctypes.c_int
    close = cabinet.CloseCompressor
    close.argtypes = [ctypes.c_void_p]
    handle = ctypes.c_void_p()
    if not create(4, None, ctypes.byref(handle)):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        source = ctypes.create_string_buffer(data)
        size = ctypes.c_size_t()
        run(handle, source, len(data), None, 0, ctypes.byref(size))
        output = ctypes.create_string_buffer(size.value)
        if not run(handle, source, len(data), output, len(output), ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        return output.raw[:size.value]
    finally:
        close(handle)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    dest = parser.parse_args().output.resolve()
    dest.mkdir(parents=True, exist_ok=True)
    rows = []
    for resource, name in ((301, "native"), (302, "buffer-reuse")):
        _, data = packed_profile(ROOT / "profiles" / (name + ".json"))
        path = dest / (name + ".bin")
        path.write_bytes(compress(data))
        rows.append(f'{resource} RCDATA "{path.as_posix()}"')
    (dest / "profiles.rc").write_text("\n".join(rows)+"\n", encoding="utf-8")

if __name__ == "__main__":
    main()
