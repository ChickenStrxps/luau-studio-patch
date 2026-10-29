"""Check profile integrity and reject private or compiled source exports."""
import json
from pathlib import Path
import re
import subprocess
from prepare_resources import ROOT, packed_profile

def main():
    profiles = [packed_profile(ROOT / "profiles" / (name+".json"))[0] for name in ("native", "buffer-reuse")]
    if profiles[0]["target_sha256"] != profiles[1]["source_sha256"]:
        raise ValueError("Profile chain mismatch")
    if (ROOT / ".git").exists():
        names = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
        files = [ROOT / name for name in names if name]
    else:
        files = [p for p in ROOT.rglob("*") if p.is_file() and not set(p.relative_to(ROOT).parts) & {"build", "artifacts", "dist", ".qa", "__pycache__", ".git"}]
    private = re.compile(rb"[A-Za-z]:[/\\]Users[/\\]|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}", re.I)
    banned = {".exe", ".dll", ".pdb", ".bin", ".rbxl", ".rbxm", ".db", ".pyc", ".key"}
    for path in files:
        if path.suffix.lower() in banned:
            raise ValueError(f"Compiled or private file: {path.relative_to(ROOT)}")
        data = path.read_bytes()
        if private.search(data) or private.search(data.replace(b"\0", b"")):
            raise ValueError(f"Personal path or credential: {path.relative_to(ROOT)}")
    for profile in profiles:
        for edit in profile["edits"]:
            data = bytes.fromhex(edit.get("hex", ""))
            if private.search(data.replace(b"\0", b"")):
                raise ValueError("Private content in profile")
    print(json.dumps({"source_files": len(files), "profiles": len(profiles), "status": "passed"}))

if __name__ == "__main__":
    main()
