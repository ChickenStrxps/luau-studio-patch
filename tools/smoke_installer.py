"""Exercise CLI refusal paths using disposable files, without opening Studio."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    exe = parser.parse_args().exe.resolve()
    with tempfile.TemporaryDirectory(prefix="studio-patch-check-") as name:
        root = Path(name)
        target = root / "RobloxStudioBeta.exe"
        original = b"Unsupported disposable fixture\n"
        target.write_bytes(original)
        env = dict(os.environ, LOCALAPPDATA=str(root / "local"))
        for index, (operation, success) in enumerate((("--studio-status", True), ("--install-studio", False), ("--restore-studio", False))):
            output = root / f"result-{index}.json"
            run = subprocess.run([str(exe), operation, "--studio-path", str(target), "--output", str(output)], env=env, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
            result = json.loads(output.read_text())
            if (run.returncode == 0) != success or target.read_bytes() != original:
                raise RuntimeError(f"Refusal check failed: {operation}")
            if success and result.get("supported") is not False:
                raise RuntimeError("Unknown image accepted")
        output = root / "existing.json"
        output.write_text("preserve")
        run = subprocess.run([str(exe), "--studio-status", "--studio-path", str(target), "--output", str(output)], env=env, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
        if run.returncode == 0 or output.read_text() != "preserve":
            raise RuntimeError("Existing output overwritten")
    print("Installer refusal and output-preservation checks passed")

if __name__ == "__main__":
    main()
