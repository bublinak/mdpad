$ErrorActionPreference = "Stop"

[xml](Get-Content -Raw "src\MDpad\MDpad.vcxproj") | Out-Null
[xml](Get-Content -Raw "src\MDpad\Package.appxmanifest") | Out-Null
[xml](Get-Content -Raw "src\MDpad\App.xaml") | Out-Null
[xml](Get-Content -Raw "src\MDpad\MainWindow.xaml") | Out-Null

foreach ($asset in @(
    "src\MDpad\Assets\Square44x44Logo.png",
    "src\MDpad\Assets\Square150x150Logo.png",
    "src\MDpad\Assets\StoreLogo.png",
    "src\MDpad\Assets\Wide310x150Logo.png",
    "src\MDpad\Assets\renderer\vendor\dompurify\purify.min.js",
    "src\MDpad\Assets\renderer\vendor\highlight\highlight.min.js",
    "src\MDpad\Assets\renderer\vendor\highlight\github-dark.min.css",
    "src\MDpad\Assets\renderer\vendor\katex\katex.min.js",
    "src\MDpad\Assets\renderer\vendor\mermaid\mermaid.min.js",
    "tests\fixtures\images\sample.png"
)) {
    if (-not (Test-Path $asset)) {
        throw "Missing required asset: $asset"
    }
}

npm test
Write-Host "Repository validation passed."
