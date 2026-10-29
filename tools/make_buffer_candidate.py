"""Build an offline buffer-reuse candidate from the exact native profile."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "patches"))
from pe_tail import TailImage, write_new
import retain_guard

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    profile = json.loads((root/"profiles/buffer-reuse.json").read_text())
    image = TailImage(args.source.read_bytes(), profile["source_sha256"])
    at = image.append(bytes(0), 16)
    code = retain_guard.payload(at)
    if image.append(code, 1) != at:
        raise RuntimeError("Unexpected payload placement")
    unwind = image.append(b"\x01\0\0\0", 4)
    image.add_runtime(at, at+len(code), unwind)
    data, _ = image.finish()
    result = bytearray(data)
    offset = image.pe.get_offset_from_rva(retain_guard.SITE)
    if result[offset:offset+5] != retain_guard.EXPECTED:
        raise ValueError("Unexpected call site")
    result[offset:offset+5] = retain_guard.replacement(at)
    write_new(args.destination, result)

if __name__ == "__main__":
    main()
