param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$Configuration = "Release"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$previousPath = $env:PATH
$env:PATH = "$QtRoot/bin;$env:PATH"
Push-Location $repo
try {
    cmake -S . -B build -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot"
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
    cmake --build build --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    ctest --test-dir build -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
    cmake --install build --config $Configuration --prefix out/ShareKeys
    if ($LASTEXITCODE -ne 0) { throw "Install failed" }
    $deployFlag = if ($Configuration -eq "Debug") { "--debug" } else { "--release" }
    & "$QtRoot/bin/windeployqt.exe" $deployFlag --no-translations out/ShareKeys/sharekeys.exe
    if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed" }
    Write-Host "Portable application is ready in out/ShareKeys"
} finally {
    Pop-Location
    $env:PATH = $previousPath
}
