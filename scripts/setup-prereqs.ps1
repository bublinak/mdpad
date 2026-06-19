param(
    [switch]$InstallVcpkg
)

$ErrorActionPreference = "Stop"

function Test-Command($Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

Write-Host "Checking MDpad prerequisites..."

if (-not (Test-Command "winget")) {
    Write-Warning "winget was not found. Install App Installer from Microsoft Store or install prerequisites manually."
} else {
    Write-Host "winget found."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -property installationPath
    if ($vs) {
        Write-Host "Visual Studio found: $vs"

        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\amd64\MSBuild.exe" | Select-Object -First 1
        if (-not $msbuild) {
            $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\MSBuild.exe" | Select-Object -First 1
        }
        if ($msbuild) {
            Write-Host "MSBuild found: $msbuild"
        } else {
            Write-Warning "MSBuild was not found in the Visual Studio install."
        }

        $toolset = Get-ChildItem (Join-Path $vs "MSBuild\Microsoft\VC") -Recurse -Directory -Filter "v145" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($toolset) {
            Write-Host "VS 2026 C++ platform toolset found: v145"
        } else {
            Write-Warning "C++ platform toolset v145 was not found. Install the Windows C++ workload for Visual Studio 2026."
        }
    } else {
        Write-Warning "Visual Studio was not found. Install Visual Studio WinUI application development with C++ WinUI app development tools."
        Write-Host "Microsoft's quick setup command is:"
        Write-Host "  winget configure -f https://aka.ms/winui-config"
    }
} else {
    Write-Warning "vswhere was not found. Visual Studio may not be installed."
}

if (-not (Test-Command "vcpkg")) {
    if ($InstallVcpkg) {
        $target = Join-Path $env:USERPROFILE "vcpkg"
        if (-not (Test-Path $target)) {
            git clone https://github.com/microsoft/vcpkg $target
        }
        & (Join-Path $target "bootstrap-vcpkg.bat")
        Write-Host "Add $target to PATH or set VCPKG_ROOT=$target before building."
    } else {
        Write-Warning "vcpkg was not found. Run this script with -InstallVcpkg or install vcpkg manually."
    }
} else {
    Write-Host "vcpkg found."
}

if (-not (Test-Command "npm")) {
    Write-Warning "npm was not found. Node.js/npm is required to vendor renderer assets."
} else {
    Write-Host "npm found."
}
