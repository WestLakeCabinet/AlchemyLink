param(
    [string]$skyrimPluginDir = "D:\Steam\steamapps\common\Skyrim Special Edition\Data\SKSE\Plugins"
)

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
Write-Host "Target Plugin Dir: $skyrimPluginDir" -ForegroundColor Gray

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

Write-Host "`n[2/2] Building SkyPotionSKSE.dll (Release) with parallel CPU threads..." -ForegroundColor Yellow
& $cmakeExe --build build --config Release --parallel

$dllPath = Join-Path $projectRoot "build\Release\SkyPotionSKSE.dll"
if (Test-Path $dllPath) {
    Write-Host "`n=============================================" -ForegroundColor Green
    Write-Host " BUILD SUCCESS!" -ForegroundColor Green
    Write-Host " Plugin DLL: $dllPath" -ForegroundColor White

    # Copy DLL to Skyrim SKSE Plugins directory
    if (-not [string]::IsNullOrWhiteSpace($skyrimPluginDir)) {
        if (-not (Test-Path $skyrimPluginDir)) {
            Write-Host " Target directory not found. Creating: $skyrimPluginDir" -ForegroundColor Yellow
            New-Item -ItemType Directory -Path $skyrimPluginDir -Force | Out-Null
        }
        $destPath = Join-Path $skyrimPluginDir "SkyPotionSKSE.dll"
        try {
            Copy-Item -Path $dllPath -Destination $destPath -Force
            Write-Host " Deployed to: $destPath" -ForegroundColor Cyan
        } catch {
            Write-Host " [WARNING] Failed to copy DLL to Skyrim folder: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host " (스카이림이 실행 중이면 게임을 종료한 뒤 다시 빌드하거나 복사해 주세요)" -ForegroundColor Yellow
        }
    }

    Write-Host "=============================================" -ForegroundColor Green
} else {
    Write-Host "`nBuild finished. Check 'build/' directory for output files." -ForegroundColor Cyan
}
