param(
    [string]$Config = "Release",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Arch = "x64",
    [string]$Triplet = "x64-windows-static",
    [switch]$Clean,
    [switch]$DevMode,       # 不嵌入前端资源，直接读取 frontend/ 目录，改完刷新即可
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectDir "build"
$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "C:\vcpkg" }

function Write-Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Write-Ok($text)   { Write-Host "    $text" -ForegroundColor Green }
function Write-Warn($text) { Write-Host "    $text" -ForegroundColor Yellow }

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "清理 build/"
    Remove-Item -Recurse -Force $BuildDir
}

# ---------------------------------------------------------------- 环境检查
Write-Step "检查构建环境"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "未找到 vswhere.exe，请安装 Visual Studio 2022（含 C++ 桌面开发工作负载）。"
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "未找到带 MSVC 工具链的 Visual Studio 2022。"
}
Write-Ok "Visual Studio: $vsPath"

# CMake/CTest：优先 PATH，其次 VS 自带，最后独立安装目录。
$cmakeExe = $null
$ctestExe = $null
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakeExe = $cmakeCommand.Source
} else {
    $cmakeCandidates = @(
        (Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\CMake\bin\cmake.exe"
    )
    foreach ($candidate in $cmakeCandidates) {
        if (Test-Path $candidate) { $cmakeExe = $candidate; break }
    }
}
if (-not $cmakeExe) {
    throw "未找到 cmake.exe，请安装 CMake 或在 Visual Studio Installer 中勾选 C++ CMake 工具。"
}
$ctestExe = Join-Path (Split-Path -Parent $cmakeExe) "ctest.exe"
if (-not (Test-Path $ctestExe)) { $ctestExe = "ctest.exe" }
Write-Ok "CMake: $cmakeExe"

if (-not (Test-Path "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake")) {
    throw "未找到 vcpkg: $VcpkgRoot（设置 VCPKG_ROOT 环境变量或安装到 C:\vcpkg）"
}
Write-Ok "vcpkg: $VcpkgRoot"

$wv2Include = "$VcpkgRoot\installed\$Triplet\include\WebView2.h"
if (Test-Path $wv2Include) {
    Write-Ok "WebView2 SDK: $wv2Include"
} else {
    Write-Warn "WebView2 SDK 将由 vcpkg manifest 在 CMake 配置阶段安装。"
}

$py = (Get-Command python -ErrorAction SilentlyContinue)
if ($py) {
    Write-Ok "Python: $($py.Source)"
} else {
    Write-Warn "未找到 Python，前端资源将使用 CMake 内置打包器。"
}

# ---------------------------------------------------------------- 配置
Write-Step "配置 CMake（$Generator / $Arch / $Config）"

# vcpkg 不允许在已配置的构建目录中切换 manifest 模式：
# 已有 build/ 时沿用其缓存值；全新构建目录在依赖已按 classic 安装时也直接复用。
$manifestMode = "ON"
$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cacheFile) {
    # vcpkg 用 Z_VCPKG_CHECK_MANIFEST_MODE 记录首次配置时的模式，切换会直接报错。
    $stamp = Select-String -Path $cacheFile -Pattern '^Z_VCPKG_CHECK_MANIFEST_MODE:INTERNAL=(ON|OFF)' |
        Select-Object -First 1
    $cachedMode = if ($stamp) {
        $stamp
    } else {
        Select-String -Path $cacheFile -Pattern '^VCPKG_MANIFEST_MODE:BOOL=(ON|OFF)' |
            Select-Object -First 1
    }
    if ($cachedMode) {
        $manifestMode = $cachedMode.Matches[0].Groups[1].Value
        if ($manifestMode -eq "OFF") {
            Write-Warn "build/ 已按非 manifest 模式配置，本次沿用（如需 manifest 请加 -Clean 重建）"
        }
    }
} elseif (Test-Path "$VcpkgRoot\installed\$Triplet\share\webview2") {
    $manifestMode = "OFF"
    Write-Warn "检测到 $Triplet 依赖已安装，使用 classic 依赖（-DVCPKG_MANIFEST_MODE=OFF）"
}

$cmakeArgs = @(
    "-G", $Generator,
    "-A", $Arch,
    "-B", $BuildDir,
    "-S", $ProjectDir,
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=$Triplet",
    "-DVCPKG_MANIFEST_MODE=$manifestMode"
)

if ($DevMode) {
    $cmakeArgs += "-DEMBED_FRONTEND=OFF"
    Write-Warn "DevMode：前端从 ./frontend 读取，不嵌入 exe"
} else {
    $cmakeArgs += "-DEMBED_FRONTEND=ON"
}

& $cmakeExe @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败" }

# ---------------------------------------------------------------- 编译
Write-Step "编译 $Config"
& $cmakeExe --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "编译失败" }

# ---------------------------------------------------------------- 测试
if (-not $SkipTests) {
    Write-Step "运行 CTest"
    & $ctestExe --test-dir $BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "测试失败" }
}

# ---------------------------------------------------------------- 结果
$exe = Join-Path $BuildDir "$Config\Silkit.exe"
if (-not (Test-Path $exe)) { throw "未生成 $exe" }

$sizeMb = [math]::Round((Get-Item $exe).Length / 1MB, 2)
Write-Step "构建完成"
Write-Ok "产物: $exe（$sizeMb MB）"
if ($DevMode) {
    Write-Warn "DevMode 运行前请把 frontend/ 放到 exe 同级目录，或使用 -DevMode 的调试配置。"
}
