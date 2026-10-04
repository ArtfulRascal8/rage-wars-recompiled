param(
    [string]$BuildDir,
    [string]$DependencyDir,
    [string]$CMake = "cmake",
    [string]$Python = "python",
    [string]$Toolset,
    [string]$WindowsSDK,
    [int]$Jobs = 6,
    [switch]$Offline
)
$ErrorActionPreference = "Stop"
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if (-not $BuildDir) { $BuildDir = Join-Path $repo "build/pc-candidate" }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
if (-not $DependencyDir) { $DependencyDir = Join-Path $BuildDir "pc-dependencies" }
$deps = [IO.Path]::GetFullPath($DependencyDir)
$stage = Join-Path $BuildDir "package-stage"
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $Executable (exit $LASTEXITCODE)" }
}
$configure = @("-S", (Join-Path $repo "staging/rage-wars-recomp-spike/pc_runtime"),
    "-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DCMAKE_PROJECT_INCLUDE=$repo/staging/rage-wars-recomp-spike/pc_runtime/cmake/gate5_setup_once.cmake",
    "-DRAGE_WARS_REPO_ROOT=$repo", "-DXR64_CONSUMER_BUILD=ON",
    "-DXR64_PUBLIC_RELEASE=ON",
    "-DXR64_CALL_FAULT_JOURNAL=OFF", "-DXR64_FAULT_FRAME_CAPTURE=OFF",
    "-DXR64_RW_REPLAY_DIAGNOSTICS=OFF", "-DXR64_RENDER_DIAGNOSTICS=OFF",
    "-DXR64_RELEASE_PACKAGE_DIR=$stage", "-DXR64_PC_DEPENDENCIES_DIR=$deps")
if ($Toolset) { $configure += @("-T", $Toolset) }
if ($WindowsSDK) { $configure += "-DCMAKE_SYSTEM_VERSION=$WindowsSDK" }
if ($Offline) { $configure += "-DXR64_DEPENDENCIES_OFFLINE=ON" }
Invoke-Checked $CMake $configure
$audit = @("$repo/scripts/audit_pc_dependency_inputs.py", "--repo", $repo,
    "--build", $BuildDir, "--dependencies", $deps, "--output", "$BuildDir/dependency-input-audit.json")
Invoke-Checked $Python $audit
Invoke-Checked $CMake @("--build", $BuildDir, "--config", "Release", "--target",
    "rage_wars_pc", "rage_wars_xr_controls_test", "rage_wars_xr_camera_test",
    "rage_wars_decoded_frame_test", "rage_wars_audio_output_test", "rage_wars_weapon_calibration_test",
    "rage_wars_modern_controls_test", "rage_wars_guest_clock_test", "--parallel", "$Jobs")
Invoke-Checked $Python ($audit + "--require-read-logs")
$ctest = Join-Path (Split-Path (Get-Command $CMake).Source) "ctest.exe"
Invoke-Checked $ctest @("--test-dir", $BuildDir, "-C", "Release", "-R",
    "^rage_wars_(xr_controls_contract|xr_camera_math|decoded_frame_ownership|audio_output_contract|weapon_calibration_contract|modern_controls_contract|guest_clock_contract|release_package_contract|gate5_cli_help)$",
    "--output-on-failure")
Invoke-Checked $Python @("$repo/scripts/audit_demo_build.py", "--build", $BuildDir, "--exe", "$BuildDir/Release/RageWarsRecompiled.exe")
Invoke-Checked $CMake @("--build", $BuildDir, "--config", "Release", "--target", "rage_wars_package")
Invoke-Checked $Python @("$repo/scripts/audit_release_privacy.py", $stage, "--report", "$BuildDir/package-privacy.json")
Write-Output "Build-local package artifact: $stage"
Write-Output "Runtime acceptance must be attributed to this executable; see release notes."
