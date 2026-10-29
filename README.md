# Studio Luau Patch

[![Windows build](https://github.com/ChickenStrxps/luau-studio-patch/actions/workflows/windows.yml/badge.svg)](https://github.com/ChickenStrxps/luau-studio-patch/actions/workflows/windows.yml)

A small Windows installer for experimental native Luau library optimizations in
Roblox Studio. It checks the complete executable hash, saves the original, applies
the supported patch, and can restore the original file.

## In Studio

`string.find`: **6.08s → 0.80s** in a repeated-prefix stress test.
Actual Studio capture at 1× playback; this measures workload completion, not FPS.

![Original and patched string.find running the same Studio workload](docs/media/search.gif)

<details>
<summary>More library comparisons — included in the installer</summary>

| `table.unpack` · 4.78× | Packed buffer bits · 1.20× |
| --- | --- |
| ![table.unpack before and after](docs/media/unpack.gif) | ![buffer.readbits and writebits before and after](docs/media/bits.gif) |

`string.upper` · **3.29×** on 256 KiB ASCII:

![ASCII case conversion before and after](docs/media/case.gif)

Synthetic repeated workloads on one machine. Results depend on input and do not
predict a game's FPS.

</details>

<details>
<summary>Engine experiments — not included in the installer</summary>

Historical Studio FPS tests, each against its own restored baseline:

| Render priority · +6.6% | Text layout · +8.6% |
| --- | --- |
| ![Render priority FPS comparison in a 1600-rig scene](docs/media/render-priority.png) | ![Text layout FPS comparison with 8000 changing labels](docs/media/text-layout.png) |

Fixed synthetic scenes on one PC. These prototypes are separate from the shipped
library profile; the text-layout ownership and reentrancy review remains open.

Isolated native routines:

| Sphere/box predicate | Mesh vertex packing |
| --- | --- |
| ![Sphere predicate isolated throughput comparison](docs/media/sphere-predicate.png) | ![Mesh packing isolated throughput comparison](docs/media/mesh-packing.png) |

These cards measure routine throughput, **not Studio FPS**. Later Studio tests
did not qualify an FPS gain: sphere timings overlapped the baselines, and the
mesh scene reached its frame cap. Full GPU-buffer equivalence remains unproven.

</details>

## Compatibility

- Windows x64; Studio **0.744.0.7441077**, exact supported executable only.
- Studio must be closed before installation or restoration.
- Unknown builds are refused. A Studio update replaces the patched executable.
- Patching invalidates the executable's original signature.

The library profile covers filled `table.create`, `table.unpack`, dense
`table.move`, byte ranges, ASCII case conversion, long plain string search,
the default numeric sort comparison, and packed buffer bits. Original fallback
paths are retained where the fast path does not apply.

Transform-buffer reuse is optional and experimental. It can retain up to 8 MiB
per renderer manager. No general game FPS gain is established. The newer sphere,
mesh, and ray experiments are not included in this installer.

This changes the local Studio executable. It does not change published games,
Roblox's hosted servers, service limits, or other players' clients. This project
is not affiliated with Roblox.

## Use

Download **[LuauStudioPatch.exe](https://github.com/ChickenStrxps/luau-studio-patch/releases/download/v0.5.2/LuauStudioPatch.exe)**
from [Releases](https://github.com/ChickenStrxps/luau-studio-patch/releases/tag/v0.5.2)
and double-click it. Close Studio, then choose **Install patch**. Use **Restore
original** to undo the patch. No Python or build tools are needed to run it.

The Windows ZIP includes the same executable and all license notices.
Keep the included license notices with the executable.

Command-line status, install, and restore operations require a JSON output path:

```powershell
.\LuauStudioPatch.exe --studio-status --output status.json
.\LuauStudioPatch.exe --install-studio --output install.json
.\LuauStudioPatch.exe --restore-studio --output restore.json
```

Add `--studio-path "C:\path\to\RobloxStudioBeta.exe"` to select a specific
installation. Add `--reuse-transform-buffer` only to an install command to enable
the optional profile. Backups are stored in `%LOCALAPPDATA%\LuauRuntimeLab\backups`.

## Build

Install Python 3.11+, CMake 3.20+, and Visual Studio C++ Build Tools with the
Windows SDK. No Studio binary is needed to build the installer.

```powershell
python tools/check_export.py
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
python tools/smoke_installer.py build/Release/LuauStudioPatch.exe
python tools/package.py build/Release/LuauStudioPatch.exe
```

The build embeds the checked compatibility profiles. [Patch source](patches/README.md)
describes how those profiles relate to the native code and a locally supplied
Studio executable. Test tools are source-only; they are not bundled in the app.

## License

MIT. Third-party code retains its original notices in [licenses](licenses).
No Roblox executable, game asset, or copied executable exception table is included.
