$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'build/kernels'
New-Item -ItemType Directory -Force $output | Out-Null
foreach ($tool in 'cl.exe', 'ml64.exe', 'link.exe') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw 'Run this script from an x64 Visual Studio Developer PowerShell.'
    }
}
Push-Location $output
try {
    & ml64 /nologo /c /Fo'kernels-asm.obj' (Join-Path $root 'patches/native/kernels.asm')
    if ($LASTEXITCODE) { throw 'Assembly failed' }
    & cl /nologo /c /O2 /GS- /GR- /Zl /std:c++17 /Brepro /Fo'kernels.obj' (Join-Path $root 'patches/native/kernels.cpp')
    if ($LASTEXITCODE) { throw 'Compilation failed' }
    $exports = @('KernelCreate','KernelLower','KernelUpper','KernelReadBits','KernelWriteBits','KernelSearch','KernelMove','KernelByte','KernelSort') | ForEach-Object { '/export:' + $_ }
    & link /nologo /dll /noentry /nodefaultlib /machine:x64 /opt:ref /Brepro /debug:none /out:StudioNativeKernels.dll kernels.obj kernels-asm.obj @exports
    if ($LASTEXITCODE) { throw 'Link failed' }
} finally { Pop-Location }
