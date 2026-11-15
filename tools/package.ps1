# 生成 Silkit 便携发布包：构建 Release → 校验版本 → 复制文件 → 压缩为 zip。
#
#   .\tools\package.ps1                # 构建 + 测试 + 打包
#   .\tools\package.ps1 -SkipTests     # 构建 + 打包
#   .\tools\package.ps1 -SkipBuild     # 直接打包已构建的 build\Release\Silkit.exe
param(
    [string]$Config = "Release",
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $OutDir) { $OutDir = Join-Path $ProjectDir "dist" }

function Write-Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Write-Ok($text)   { Write-Host "    $text" -ForegroundColor Green }

# ---------------------------------------------------------------- 版本
$cmake = Get-Content (Join-Path $ProjectDir "CMakeLists.txt") -Raw
if ($cmake -notmatch "project\(\s*Silkit\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)") {
    throw "无法从 CMakeLists.txt 读取版本号"
}
$Version = $Matches[1]
Write-Step "Silkit $Version"

# ---------------------------------------------------------------- 构建
if (-not $SkipBuild) {
    $buildArgs = @{ Config = $Config }
    if ($SkipTests) { $buildArgs.SkipTests = $true }
    & (Join-Path $ProjectDir "build.ps1") @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "构建失败" }
}

$exe = Join-Path $ProjectDir "build\$Config\Silkit.exe"
if (-not (Test-Path $exe)) { throw "未找到 $exe，请先构建" }

# ---------------------------------------------------------------- 版本一致性
Write-Step "校验版本号"
& python (Join-Path $ProjectDir "tools\check_version.py")
if ($LASTEXITCODE -ne 0) { throw "版本号不一致，停止打包" }

# ---------------------------------------------------------------- 暂存
$name = "Silkit-$Version-win-x64"
$stage = Join-Path $OutDir $name
Write-Step "暂存到 $stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item $exe $stage
foreach ($file in @("README.md", "README.en.md", "LICENSE", "THIRD-PARTY-NOTICES.md")) {
    $path = Join-Path $ProjectDir $file
    if (Test-Path $path) { Copy-Item $path $stage } else { Write-Warning "缺少 $file，已跳过" }
}

# ---------------------------------------------------------------- 压缩
$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Write-Step "压缩 $zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal

$zipSize = [math]::Round((Get-Item $zip).Length / 1MB, 2)
$exeSize = [math]::Round((Get-Item $exe).Length / 1MB, 2)
Write-Ok "exe: $exeSize MB"
Write-Ok "zip: $zip（$zipSize MB）"
Write-Ok "包内文件:"
Get-ChildItem $stage -Name | ForEach-Object { Write-Host "      $_" }
