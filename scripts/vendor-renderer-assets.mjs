import { copyFile, mkdir, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const renderer = path.join(root, "src", "MDpad", "Assets", "renderer", "vendor");

async function mustExist(file) {
  await stat(file);
  return file;
}

async function copy(from, to) {
  await mkdir(path.dirname(to), { recursive: true });
  await copyFile(await mustExist(from), to);
  console.log(`${path.relative(root, from)} -> ${path.relative(root, to)}`);
}

await copy(
  path.join(root, "node_modules", "dompurify", "dist", "purify.min.js"),
  path.join(renderer, "dompurify", "purify.min.js")
);
await copy(
  path.join(root, "node_modules", "katex", "dist", "katex.min.js"),
  path.join(renderer, "katex", "katex.min.js")
);
await copy(
  path.join(root, "node_modules", "katex", "dist", "katex.min.css"),
  path.join(renderer, "katex", "katex.min.css")
);
await copy(
  path.join(root, "node_modules", "katex", "dist", "contrib", "auto-render.min.js"),
  path.join(renderer, "katex", "auto-render.min.js")
);
await copy(
  path.join(root, "node_modules", "@highlightjs", "cdn-assets", "styles", "github.min.css"),
  path.join(renderer, "highlight", "github.min.css")
);
await copy(
  path.join(root, "node_modules", "@highlightjs", "cdn-assets", "styles", "github-dark.min.css"),
  path.join(renderer, "highlight", "github-dark.min.css")
);
await copy(
  path.join(root, "node_modules", "@highlightjs", "cdn-assets", "highlight.min.js"),
  path.join(renderer, "highlight", "highlight.min.js")
);
