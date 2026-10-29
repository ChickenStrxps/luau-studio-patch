"""Package the installer and required notices, without development files."""
import argparse
import hashlib
from pathlib import Path
import shutil
import zipfile
from prepare_resources import ROOT

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    payload = args.exe.read_bytes()
    if b"RSDS" in payload or b"\\Users\\" in payload or "\\Users\\".encode("utf-16-le") in payload:
        raise ValueError("Debug or personal path found in installer")
    args.output.mkdir(parents=True, exist_ok=True)
    bundle = args.output / "LuauStudioPatch-windows-x64.zip"
    paths = [(args.exe, "LuauStudioPatch.exe"), (ROOT/"LICENSE", "LICENSE"), (ROOT/"THIRD_PARTY_NOTICES.txt", "THIRD_PARTY_NOTICES.txt")]
    paths += [(p, "licenses/"+p.name) for p in sorted((ROOT/"licenses").glob("*.txt"))]
    with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED) as out:
        for source, name in paths:
            out.write(source, name)
    shutil.copy2(args.exe, args.output / "LuauStudioPatch.exe")
    outputs = (bundle, args.output/"LuauStudioPatch.exe")
    (args.output/"SHA256SUMS.txt").write_text("".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in outputs))
    print(bundle)

if __name__ == "__main__":
    main()
