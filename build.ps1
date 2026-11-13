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

if (-not (Test-Path "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake")) {
    throw "未找到 vcpkg: $VcpkgRoot（设置 VCPKG_ROOT 环境变量或安装到 C:\vcpkg）"
}
Write-Ok "vcpkg: $VcpkgRoot"

$wv2Include = "$VcpkgRoot\installed\$Triplet\include\WebView2.h"
if (-not (Test-Path $wv2Include)) {
    Write-Warn "缺少 WebView2 SDK，正在安装（首次需要几分钟）..."
    & "$VcpkgRoot\vcpkg.exe" install "webview2:$Triplet" --classic
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install webview2:$Triplet 失败" }
}
Write-Ok "WebView2 SDK: $wv2Include"

$py = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $py) { throw "未找到 python，前端资源打包需要 Python 3。" }
Write-Ok "Python: $($py.Source)"

# ---------------------------------------------------------------- 配置
Write-Step "配置 CMake（$Generator / $Arch / $Config）"

$cmakeArgs = @(
    "-G", $Generator,
    "-A", $Arch,
    "-B", $BuildDir,
    "-S", $ProjectDir,
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=$Triplet",
    "-DVCPKG_MANIFEST_MODE=ON"
)

if ($DevMode) {
    $cmakeArgs += "-DEMBED_FRONTEND=OFF"
    Write-Warn "DevMode：前端从 ./frontend 读取，不嵌入 exe"
} else {
    $cmakeArgs += "-DEMBED_FRONTEND=ON"
}

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败" }

# ---------------------------------------------------------------- 编译
Write-Step "编译 $Config"
& cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "编译失败" }

# ---------------------------------------------------------------- 测试
if (-not $SkipTests) {
    Write-Step "运行 CTest"
    & ctest --test-dir $BuildDir -C $Config --output-on-failure
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
