# Contributing

Pull requests are welcome. Contributions are submitted under this project's MIT
license. Keep existing copyright and third-party license notices intact.

Keep changes limited to the installer and supported patch sources. Do not add
Studio executables, game assets, disassembly dumps, machine-specific paths,
credentials, runtime records, or private integration code.

Run `python tools/check_export.py`, then build and run the installer smoke test.
Compatibility changes need exact source/output hashes, fallback and restoration
checks, and relevant native correctness evidence. Keep benchmark claims scoped
to what was measured. Do not infer game FPS from an isolated routine benchmark.
