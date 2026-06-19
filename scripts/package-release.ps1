param(
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",
    [switch]$SkipValidation
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$rootPath = $root.Path.TrimEnd('\')

function Assert-UnderRoot {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (-not ($fullPath.Equals($rootPath, [System.StringComparison]::OrdinalIgnoreCase) -or
            $fullPath.StartsWith($rootPath + "\", [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "Refusing to operate outside repository root: $fullPath"
    }
}

Push-Location $root
try {
    if (-not $SkipValidation) {
        & ".\scripts\validate.ps1"
    }

    & ".\scripts\build.ps1" -Configuration Release -Platform $Platform

    [xml]$manifest = Get-Content -Raw ".\src\MDpad\Package.appxmanifest"
    $version = $manifest.Package.Identity.Version
    if (-not $version) {
        throw "Could not read package version from Package.appxmanifest."
    }

    $releaseDir = Join-Path $root "out\$Platform\Release"
    if (-not (Test-Path (Join-Path $releaseDir "MDpad.exe"))) {
        throw "Release output is missing MDpad.exe: $releaseDir"
    }

    $distDir = Join-Path $root "dist"
    $stageName = "MDpad-$version-win-$Platform-portable"
    $stageDir = Join-Path $distDir $stageName
    $zipPath = Join-Path $distDir "$stageName.zip"
    $hashPath = Join-Path $distDir "$stageName.sha256"

    foreach ($path in @($distDir, $stageDir, $zipPath, $hashPath)) {
        Assert-UnderRoot $path
    }

    New-Item -ItemType Directory -Force -Path $distDir | Out-Null

    if (Test-Path $stageDir) {
        Remove-Item -LiteralPath $stageDir -Recurse -Force
    }
    foreach ($file in @($zipPath, $hashPath)) {
        if (Test-Path $file) {
            Remove-Item -LiteralPath $file -Force
        }
    }

    New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
    Copy-Item -Path (Join-Path $releaseDir "*") -Destination $stageDir -Recurse -Force

    foreach ($runtimeByproduct in @(
            "MDpad.exe.WebView2",
            "MDpad.pdb",
            "MDpad.exp",
            "MDpad.lib",
            "MDpad.build.appxrecipe"
        )) {
        $target = Join-Path $stageDir $runtimeByproduct
        if (Test-Path $target) {
            Remove-Item -LiteralPath $target -Recurse -Force
        }
    }

    Get-ChildItem -Path $stageDir -Recurse -Force -Include "*.pdb", "*.ilk", "*.exp", "*.lib", "*.appxrecipe" |
        Remove-Item -Force

    Compress-Archive -Path $stageDir -DestinationPath $zipPath -CompressionLevel Optimal -Force
    $hash = Get-FileHash -Path $zipPath -Algorithm SHA256
    Set-Content -Path $hashPath -Encoding ASCII -Value "$($hash.Hash)  $(Split-Path $zipPath -Leaf)"

    Write-Host "Release package: $zipPath"
    Write-Host "SHA256: $hashPath"
}
finally {
    Pop-Location
}
