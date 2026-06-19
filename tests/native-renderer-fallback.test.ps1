$ErrorActionPreference = "Stop"

function Find-VisualStudioInstall {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($found) {
            return ($found | Select-Object -First 1)
        }
    }

    $candidates = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Community",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate "VC\Auxiliary\Build\vcvars64.bat")) {
            return $candidate
        }
    }

    throw "Visual Studio C++ tools were not found."
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$vsInstall = Find-VisualStudioInstall
$vcvars = Join-Path $vsInstall "VC\Auxiliary\Build\vcvars64.bat"
$outDir = Join-Path $root "obj\tests"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$sources = @(
    (Join-Path $root "tests\native-renderer-fallback.cpp"),
    (Join-Path $root "src\MDpad\Core\MarkdownRenderer.cpp"),
    (Join-Path $root "src\MDpad\Core\HtmlUtil.cpp"),
    (Join-Path $root "src\MDpad\Core\TextEncoding.cpp")
)

$includeStubs = Join-Path $root "tests\native_stubs"
$includeCore = Join-Path $root "src\MDpad\Core"
$exePath = Join-Path $outDir "native-renderer-fallback.exe"
$outDirForCl = ($outDir -replace "\\", "/")
$exePathForCl = ($exePath -replace "\\", "/")
$batchPath = Join-Path $outDir "build-native-renderer-fallback.cmd"
$compileLine = @(
    "cl",
    "/nologo",
    "/std:c++20",
    "/EHsc",
    "/DMDPAD_FORCE_FALLBACK_RENDERER=1",
    "/I`"$includeStubs`"",
    "/I`"$includeCore`"",
    "/Fo`"$outDirForCl/`"",
    "/Fe`"$exePathForCl`""
) + ($sources | ForEach-Object { "`"$_`"" })

Set-Content -Path $batchPath -Encoding ASCII -Value @(
    "@echo off",
    "call `"$vcvars`" >nul",
    ($compileLine -join " "),
    "exit /b %ERRORLEVEL%"
)

cmd.exe /d /c "`"$batchPath`""
if ($LASTEXITCODE -ne 0) {
    throw "Native fallback renderer test compilation failed."
}

& $exePath
if ($LASTEXITCODE -ne 0) {
    throw "Native fallback renderer test failed."
}
