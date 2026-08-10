<#
.SYNOPSIS
    SokuWinTracker のビルドスクリプト。

.DESCRIPTION
    - SokuDev/SokuMods を (未取得なら) clone
    - modules/SokuWinTracker/main.cpp として src/main.cpp を配置
    - ルートの CMakeLists.txt に SokuWinTracker モジュール登録を追記 (未登録の場合のみ)
    - cmake configure + build を実行
    - 生成された SokuWinTracker.dll を dist/SokuWinTracker_full/Modules/SokuWinTracker/ にコピー
    - dist/ 配下一式を SokuWinTracker.zip として再生成
    - -Deploy 指定時、実機の th123 インストール先 (既定 L:\th123) の
      Modules/SokuWinTracker/SokuWinTracker.dll を上書き (ini は保持)

.PARAMETER Clean
    SokuMods の build ディレクトリを削除してから configure し直す。

.PARAMETER Reclone
    既存の SokuMods チェックアウトを削除して clone し直す。

.PARAMETER Deploy
    ビルド後、DeployPath 配下の Modules/SokuWinTracker/SokuWinTracker.dll を上書きする。
    SokuWinTracker.ini (累計勝敗データ) には触れない。

.PARAMETER DeployPath
    Deploy 先の th123 インストールディレクトリ。既定は L:\th123。

.EXAMPLE
    ./build.ps1
    ./build.ps1 -Clean
    ./build.ps1 -Deploy
#>
param(
    [switch]$Clean,
    [switch]$Reclone,
    [switch]$Deploy,
    [string]$DeployPath = "L:\th123"
)

$ErrorActionPreference = "Stop"

$RootDir      = $PSScriptRoot
$SokuModsDir  = Join-Path $RootDir "SokuMods"
$ModuleSrcDir = Join-Path $SokuModsDir "modules\SokuWinTracker"
$CMakeListsPath = Join-Path $SokuModsDir "CMakeLists.txt"
$MainCppSrc   = Join-Path $RootDir "src\main.cpp"
$BuildDir     = Join-Path $SokuModsDir "build"
$DistModuleDir = Join-Path $RootDir "dist\SokuWinTracker_full\Modules\SokuWinTracker"

function Step($msg) {
    Write-Host "==> $msg" -ForegroundColor Cyan
}

if ($Reclone -and (Test-Path $SokuModsDir)) {
    Step "Removing existing SokuMods checkout for reclone"
    Remove-Item -Recurse -Force $SokuModsDir -Confirm:$false
}

if (-not (Test-Path $SokuModsDir)) {
    Step "Cloning SokuDev/SokuMods (recursive)"
    git clone --recursive https://github.com/SokuDev/SokuMods.git $SokuModsDir
    if (-not $?) { throw "git clone failed" }
}

if (-not (Test-Path $ModuleSrcDir)) {
    Step "Creating modules/SokuWinTracker"
    New-Item -ItemType Directory -Force $ModuleSrcDir | Out-Null
}

Step "Copying src/main.cpp into SokuMods module dir"
Copy-Item $MainCppSrc (Join-Path $ModuleSrcDir "main.cpp") -Force

$cmakeBlock = @'
module(SokuWinTracker)
target_sources(SokuWinTracker PRIVATE modules/SokuWinTracker/main.cpp)
target_link_libraries(
        SokuWinTracker
        SokuLib
        shlwapi
        ws2_32
        user32
        "${CMAKE_SOURCE_DIR}/lib/d3d9.lib"
        "${CMAKE_SOURCE_DIR}/lib/d3dx9.lib"
)
'@

$cmakeContent = Get-Content -Raw $CMakeListsPath
if ($cmakeContent -notmatch "module\(SokuWinTracker\)") {
    Step "Registering SokuWinTracker module in CMakeLists.txt"
    Add-Content -Path $CMakeListsPath -Value "`n$cmakeBlock`n" -Encoding utf8
} else {
    Step "SokuWinTracker module already registered in CMakeLists.txt"
}

if ($Clean -and (Test-Path $BuildDir)) {
    Step "Cleaning build directory"
    Remove-Item -Recurse -Force $BuildDir -Confirm:$false
}

Push-Location $SokuModsDir
try {
    Step "cmake configure (Win32)"
    $configureArgs = @(
        "-A", "Win32",
        "-B", "build",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "."
    )
    & cmake @configureArgs
    if (-not $?) { throw "cmake configure failed" }

    Step "cmake build (Release, target SokuWinTracker)"
    $buildArgs = @(
        "--build", "build",
        "--config", "Release",
        "--target", "SokuWinTracker"
    )
    & cmake @buildArgs
    if (-not $?) { throw "cmake build failed" }
} finally {
    Pop-Location
}

$builtDll = Join-Path $BuildDir "Release\SokuWinTracker.dll"
if (-not (Test-Path $builtDll)) {
    throw "Build succeeded but $builtDll not found"
}

if (-not (Test-Path $DistModuleDir)) {
    New-Item -ItemType Directory -Force $DistModuleDir | Out-Null
}
Step "Copying built DLL to dist/SokuWinTracker_full/Modules/SokuWinTracker/"
Copy-Item $builtDll $DistModuleDir -Force

$DistDir = Join-Path $RootDir "dist"
$ZipPath = Join-Path $RootDir "SokuWinTracker.zip"
Step "Repacking dist/ into SokuWinTracker.zip"
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force -Confirm:$false
}
$zipEntries = Get-ChildItem -Path $DistDir -Force | ForEach-Object { $_.FullName }
Compress-Archive -Path $zipEntries -DestinationPath $ZipPath -Force

Step "Done: $DistModuleDir\SokuWinTracker.dll"
Step "Done: $ZipPath"

if ($Deploy) {
    $DeployModuleDir = Join-Path $DeployPath "Modules\SokuWinTracker"
    if (-not (Test-Path $DeployPath)) {
        throw "Deploy path not found: $DeployPath"
    }
    if (-not (Test-Path $DeployModuleDir)) {
        throw "SokuWinTracker module dir not found under deploy path (expected it to already be installed/enabled): $DeployModuleDir"
    }
    Step "Deploying DLL to $DeployModuleDir (ini left untouched)"
    Copy-Item $builtDll $DeployModuleDir -Force
    Step "Deployed: $DeployModuleDir\SokuWinTracker.dll"
}
