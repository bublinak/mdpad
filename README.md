# MDpad

MDpad is a Windows-native Markdown notepad built with C++/WinRT, WinUI 3, and WebView2. It is designed to feel close to Windows 11 Notepad while opening Markdown in formatted preview mode by default.

## AI Disclosure

This repository was generated and iteratively modified with AI assistance from OpenAI Codex/GPT-5. The generated code was built and tested locally before publication.

## Features

- Windows 11 Notepad-style shell with `File`, `Edit`, and `View` menus.
- Top-left toggle between formatted preview and Markdown syntax/source mode.
- One document per window.
- Real keyboard shortcuts for file, edit, find, zoom, and navigation commands.
- Fast startup path: native Markdown rendering first, then deferred WebView hydration for heavier features.
- Markdown support through `cmark-gfm` when available, including headings, lists, tables, task lists, fenced code, links, and images.
- KaTeX math rendering for inline and display equations.
- highlight.js syntax highlighting for rendered code blocks.
- Relative image and iframe paths resolved against the opened Markdown file folder.
- Local Markdown file links open in MDpad, with a setting for new-window or current-window behavior.
- Back and forward buttons move through the current window's Markdown file history.
- Acrylic-backed app shell with transparent editor and preview surfaces.
- Sanitized raw HTML support for common document markup, including `div`, `img`, and `iframe`.
- Safe inline CSS subset for layout-oriented HTML, including side-by-side iframe layouts.
- Generated preview HTML export from the `File` menu.
- Settings dialog for theme, startup mode, Markdown file-link behavior, and transparency effect.
- `.md` and `.markdown` file associations in the packaged app manifest.
- Self-contained Windows App SDK release package.

## Releases

### v1.3

Adds navigation polish and shortcut fixes:

- Real keyboard accelerator handling for file, edit, find/replace, zoom, and history commands.
- Acrylic-aware chrome over the Windows 11 desktop acrylic backdrop.
- Local Markdown file links open in MDpad instead of being sent to the browser.
- Settings option for opening Markdown file links in a new window or the current window.
- Back and forward buttons for Markdown file history, including dirty-state prompts before replacing the active document.

### v1.2

Adds app-level settings and HTML export:

- `File > Save HTML...` exports the current generated WebView preview as an HTML snapshot.
- Settings button in the top-right toolbar.
- Transparency effect slider from 0-100%.
- App theme selection: system, light, or dark.
- Default startup view selection: formatted or syntax.
- GitHub and MIT License links in Settings.

### v1.1

Adds richer document HTML support:

- Relative Markdown and HTML image handling.
- Sanitized iframe embeds for interactive documents.
- Multi-line raw HTML iframe parsing.
- Safe inline layout CSS for `div` and iframe-heavy documents.
- Responsive collapse for side-by-side flex layouts on narrow windows.

### v1.0

Initial usable Markdown notepad release:

- Native Windows shell.
- Markdown preview/source toggle.
- Markdown tables, task lists, code blocks, links, images, and math.
- Dark/light renderer styling.
- Portable self-contained x64 package.

## Requirements

- Windows 10 1809 or newer; Windows 11 recommended.
- Visual Studio with C++ and WinUI application development tools.
- Windows App SDK tooling.
- Node.js/npm for vendoring renderer assets.
- vcpkg, or run `scripts/setup-prereqs.ps1 -InstallVcpkg`.

## Build

```powershell
npm install
.\scripts\setup-prereqs.ps1
.\scripts\build.ps1 -Configuration Debug -Platform x64
```

Use `-Configuration Release` for a release build.

## Package

```powershell
.\scripts\package-release.ps1 -Platform x64
```

The release script validates the repository, builds `Release|x64`, stages a portable self-contained app folder under `dist\`, removes build symbols and WebView2 runtime cache output, creates a zip, and writes a `.sha256` checksum next to it.

MSIX publishing still requires a trusted signing certificate and publisher identity before distribution outside developer sideloading.

## Test

```powershell
npm test
.\scripts\validate.ps1
```

The test suite covers renderer sanitization, link policy, image and iframe resource handling, math hydration, code highlighting, and the native fallback Markdown renderer.

## Security Model

MDpad previews local Markdown through WebView2, so raw HTML is intentionally constrained.

- Scripts, event handlers, `object`, `embed`, and unsafe resource schemes are blocked.
- External links are intercepted and opened through the Windows default browser.
- Document-local Markdown links are resolved against the active Markdown file folder before any browser handoff.
- Iframes are limited to HTTP(S) and sandboxed.
- Inline CSS is restricted to a small layout-oriented subset. Script-like CSS, external CSS URLs, fixed positioning, z-index overlays, animations, filters, and arbitrary visual styling are stripped.
- Preview mode is read-only. Saving always writes plain Markdown from source mode.

## License

MDpad is released under the MIT License. See [LICENSE](LICENSE).
