# Patch source

`native/kernels.cpp` and `native/kernels.asm` contain the native library kernels.
`native/make_candidate.py` maps them into the one supported Studio build and
constructs the required call adapters and Windows unwind metadata.
`unpack_patch.py` contains the in-place unpack recipe. `retain_guard.py` contains
the optional transform-buffer retention leaf. `pe_tail.py` provides the PE tail
extension used by that optional profile.

The installer consumes the fixed edit profiles in `profiles/`. Literal edits
contain patch code and header updates; copy edits reuse unchanged exception
metadata from the user's original file. No Studio executable is distributed.
Each profile records source/target hashes, sizes and a hash of its decoded edits.

Build native kernels from a Visual Studio x64 developer PowerShell:

```powershell
powershell -NoProfile -File tools/build-kernels.ps1
python -m pip install pefile==2024.8.26
python patches/native/make_candidate.py C:/path/to/original/RobloxStudioBeta.exe C:/path/to/new/RobloxStudioBeta.exe
```

The output path must not exist. This creates a candidate; it does not install it.
Different compiler versions can produce different candidate hashes. Replacing a
compatibility profile requires a new review and validation, not just a new hash.
The shipped profile is pinned independently of the local compiler output.

`tools/make_buffer_candidate.py` applies the retention leaf to a locally supplied
library-patched executable. Runtime tests and research captures are maintained
outside this distribution. Building successfully does not establish a speed gain.
