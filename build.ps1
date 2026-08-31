# SlaveDriver (PowerSlave Saturn) build script — GCC14 port.
# Wraps GNU make from MSYS2 with the sh2eb-elf toolchain SaturnRingLib unpacks, the same way
# Mimas's build.ps1 does.  Every extra argument is passed to make verbatim.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File build.ps1              # build/INIT.BIN MAIN.BIN KEYGEN.BIN (debug, like the CPEs)
#   powershell -ExecutionPolicy Bypass -File build.ps1 -NDebug      # same under build/ndebug/ (-DNDEBUG)
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Clean       # rm -rf build/ first
#   powershell -ExecutionPolicy Bypass -File build.ps1 size         # text/data/bss vs the original CPEs
#   powershell -ExecutionPolicy Bypass -File build.ps1 iso          # bootable test ISO (needs cd/ data)
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Probe iso iso-ipjump   # build/probe/: boot-probe INIT, 2 discs (SRL IP / jump-only IP)
#   powershell -ExecutionPolicy Bypass -File build.ps1 -k           # keep going (report what fails)
#
# Why a wrapper: C:\msys64\usr\bin\make.exe must be started from a NATIVE parent (PowerShell/cmd).
# Started from Git Bash (a different MSYS runtime) it dies with "Cannot create temporary file in
# C:\WINDOWS\" before running a single recipe.  From Git Bash use:
#   powershell -ExecutionPolicy Bypass -File build.ps1 <make args>
#   cmd //c "C:\msys64\usr\bin\make.exe <make args>"          (equivalent)

param(
    [switch]$Clean,
    [switch]$NDebug,
    [switch]$Probe,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$MakeArgs
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$toolchain = "C:\Users\pcico\Projects\Mimas\SaturnRingLib\Compiler\sh2eb-elf\bin"
if ($env:SH2EB_TOOLCHAIN) { $toolchain = $env:SH2EB_TOOLCHAIN }   # override without editing
$msysBin   = "C:\msys64\usr\bin"
$make      = Join-Path $msysBin "make.exe"

if (-not (Test-Path (Join-Path $toolchain "sh2eb-elf-gcc.exe"))) {
    throw "sh2eb-elf-gcc.exe not found in $toolchain (run SaturnRingLib\setup_compiler.bat, or set SH2EB_TOOLCHAIN)"
}
if (-not (Test-Path $make)) { throw "GNU make not found at $make (install MSYS2 at C:\msys64)" }

# Toolchain first, then MSYS2 /usr/bin (sh, mkdir, cp, xorrisofs for `make iso`).
$env:PATH = "$toolchain;$msysBin;" + $env:PATH

Push-Location $root
try {
    if ($Clean) {
        Write-Host "clean: removing build/"
        & $make clean
    }
    $args = @()
    if ($NDebug) { $args += "NDEBUG=1" }
    if ($Probe)  { $args += "BOOTPROBE=1" }
    if ($MakeArgs) { $args += $MakeArgs }
    Write-Host "make $($args -join ' ')"
    & $make @args
    if ($LASTEXITCODE -ne 0) { throw "make failed (exit $LASTEXITCODE)" }
}
finally {
    Pop-Location
}
