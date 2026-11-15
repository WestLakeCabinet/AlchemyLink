$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
Set-Location $projectRoot

$vsPath = "D:\Microsoft Visual Studio\18\Community"
$cmakeExe = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$vcpkgToolchain = "$vsPath\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"

# Fallback to system PATH if VS bundled cmake not found at hardcoded path
if (-not (Test-Path $cmakeExe)) {
    $cmakeExe = "cmake"
}

Write-Host "=============================================" -ForegroundColor Cyan
Write-Host " SkyPotion SKSE Plugin Build Script" -ForegroundColor Cyan
Write-Host "=============================================" -ForegroundColor Cyan
Write-Host "Using CMake: $cmakeExe" -ForegroundColor Gray
Write-Host "Using Toolchain: $vcpkgToolchain" -ForegroundColor Gray

# Clean stale CMakeCache if it still points to the old x64-windows-static triplet
$cacheFile = Join-Path $projectRoot "build\CMakeCache.txt"
if (Test-Path $cacheFile) {
    $cacheContent = Get-Content $cacheFile -Raw
    if ($cacheContent -match "x64-windows-static[\\/]") {
        Write-Host "Detected stale triplet cache in CMakeCache.txt. Refreshing cache..." -ForegroundColor Yellow
        Remove-Item $cacheFile -Force
    }
}

Write-Host "`n[1/2] Configuring CMake..." -ForegroundColor Yellow
& $cmakeExe -B build -S . `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_TESTS=OFF `
    -DCMAKE_TOOLCHAIN_FILE="$vcpkgToolchain" `
    -DVCPKG_TARGET_TRIPLET="x64-windows-static-md"

# Ensure CommonLibSSE-NG supports Skyrim 1.7.x (Cast / AE)
$moduleHeader = Join-Path $projectRoot "build\_deps\commonlibsse-ng-src\include\REL\Module.h"
if (Test-Path $moduleHeader) {
    $hdrText = [System.IO.File]::ReadAllText($moduleHeader)
    if ($hdrText -notmatch "case 7:") {
        Write-Host "Patching CommonLibSSE-NG Module.h for Skyrim 1.7.x support..." -ForegroundColor Cyan
        $hdrText = $hdrText -replace 'case 6:\s*(\r?\n\s*)_instance\._runtime = Runtime::AE;\s*(\r?\n\s*)break;\s*(\r?\n\s*)default:\s*(\r?\n\s*)_instance\._runtime = Runtime::SE;', 'case 6:$1case 7:$1    _instance._runtime = Runtime::AE;$1    break;$1default:$1    _instance._runtime = a_version[1] >= 6 ? Runtime::AE : Runtime::SE;'
        $hdrText = $hdrText -replace 'case 6:\s*(\r?\n\s*)_runtime = Runtime::AE;\s*(\r?\n\s*)break;\s*(\r?\n\s*)default:\s*(\r?\n\s*)_runtime = Runtime::SE;', 'case 6:$1case 7:$1    _runtime = Runtime::AE;$1    break;$1default:$1    _runtime = _version[1] >= 6 ? Runtime::AE : Runtime::SE;'
        [System.IO.File]::WriteAllText($moduleHeader, $hdrText)
    }
}

Write-Host "`n[2/2] Building SkyPotionSKSE.dll (Release) with parallel CPU threads..." -ForegroundColor Yellow
& $cmakeExe --build build --config Release --parallel

$dllPath = Join-Path $projectRoot "build\Release\SkyPotionSKSE.dll"
if (Test-Path $dllPath) {
    Write-Host "`n=============================================" -ForegroundColor Green
    Write-Host " BUILD SUCCESS!" -ForegroundColor Green
    Write-Host " Plugin DLL: $dllPath" -ForegroundColor White
    Write-Host " Copy this file to: <Skyrim SE>/Data/SKSE/Plugins/" -ForegroundColor Gray
    Write-Host "=============================================" -ForegroundColor Green
} else {
    Write-Host "`nBuild finished. Check 'build/' directory for output files." -ForegroundColor Cyan
}
