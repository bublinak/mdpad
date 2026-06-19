$ErrorActionPreference = "Stop"

if (-not (Test-Path "node_modules")) {
    npm install
}

node scripts/vendor-renderer-assets.mjs
