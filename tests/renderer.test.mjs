import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { JSDOM } from "jsdom";
import createDOMPurify from "dompurify";
import hljs from "highlight.js";
import katex from "katex";
import renderMathInElement from "katex/contrib/auto-render";

const root = path.resolve(".");
const dom = new JSDOM(`<!doctype html><main id="content"></main>`, {
  url: "https://app.mdpad.local/index.html",
  pretendToBeVisual: true,
  runScripts: "outside-only"
});

const posted = [];
globalThis.Node = dom.window.Node;
globalThis.document = dom.window.document;
dom.window.chrome = { webview: { postMessage: (message) => posted.push(message), addEventListener: () => {} } };
dom.window.DOMPurify = createDOMPurify(dom.window);
dom.window.hljs = hljs;
dom.window.katex = katex;
dom.window.renderMathInElement = renderMathInElement;
dom.window.mermaid = {
  initialize: () => {},
  render: async (_id, source) => ({
    svg: `<svg role="img" data-source="${source.trim()}"></svg>`,
    bindFunctions: () => {}
  })
};
dom.window.requestAnimationFrame = (callback) => callback();
dom.window.scrollTo = () => {};
dom.window.HTMLElement.prototype.scrollIntoView = () => {};

const code = readFileSync(path.join(root, "src", "MDpad", "Assets", "renderer", "mdpad-renderer.js"), "utf8");
vm.runInContext(code, dom.getInternalVMContext());

dom.window.__mdpadRenderer.render({
  html: `
    <h1>Hello</h1>
    <script>window.bad = true</script>
    <p><a href="https://example.com/docs">Link</a></p>
    <p><a id="local-link" href="docs/next file.md#part">Local doc</a></p>
    <p><img alt="diagram" src="images/a.png" onerror="window.bad = true" onmouseover="window.bad = true"></p>
    <p><img alt="bad" src="javascript:window.bad = true"></p>
    <div id="layout" style="width: 100%; display: flex; justify-content: center; align-items: center; position: fixed; z-index: 999; background-image: url(javascript:bad);">
      <div id="left" style="width: 50%; display: inline-block; text-align: center; color: red;">
        <iframe
          src="embeds/circuit.html?ctz=CQAgjCAMB0l3BWEAWB0D+abc"
          width="100%"
          height="380"
          title="Falstad test"
          loading="lazy"
          sandbox="allow-scripts allow-top-navigation"
          allow="fullscreen; microphone"
          onload="window.bad = true"
          style="border: 1px solid red; border-radius: 8px; position: fixed; background-image: url(javascript:bad);">
        </iframe>
      </div>
      <div id="right" style="width: 50%; display: inline-block; text-align: center;">Side panel</div>
    </div>
    <iframe src="javascript:window.bad = true"></iframe>
    <p>$x^2$</p>
    <pre><code class="language-mermaid">graph TD
  A-->B
</code></pre>
    <pre><code class="language-js">const x = 1;</code></pre>
  `,
  baseUri: "https://doc.mdpad.local/",
  zoom: 1.25,
  theme: "dark"
});

await new Promise((resolve) => setTimeout(resolve, 32));

const document = dom.window.document;
const content = document.getElementById("content");

assert.equal(dom.window.bad, undefined);
assert.equal(content.style.fontSize, "125%");
assert.equal(document.querySelector("base[data-mdpad-document-base]").href, "https://doc.mdpad.local/");
assert.equal(document.documentElement.dataset.mdpadTheme, "dark");
assert.equal(document.documentElement.style.colorScheme, "dark");
assert.equal(document.querySelector("img[alt='diagram']").src, "https://doc.mdpad.local/images/a.png");
assert.equal(document.querySelector("img[alt='diagram']").getAttribute("onmouseover"), null);
assert.equal(document.querySelector("img[alt='bad']").hasAttribute("src"), false);
assert.equal(document.querySelector("script"), null);
assert.equal(document.querySelector("iframe").src, "https://doc.mdpad.local/embeds/circuit.html?ctz=CQAgjCAMB0l3BWEAWB0D+abc");
assert.equal(document.querySelector("iframe").getAttribute("loading"), "eager");
assert.equal(document.querySelector("iframe").getAttribute("sandbox"), "allow-downloads allow-forms allow-modals allow-pointer-lock allow-popups allow-popups-to-escape-sandbox allow-presentation allow-same-origin allow-scripts");
assert.equal(document.querySelector("iframe").getAttribute("allow"), "clipboard-read; clipboard-write; fullscreen; gamepad");
assert.equal(document.querySelector("iframe").getAttribute("allowfullscreen"), "");
assert.equal(document.querySelector("iframe").getAttribute("referrerpolicy"), "strict-origin-when-cross-origin");
assert.equal(document.querySelector("iframe").getAttribute("onload"), null);
assert.equal(document.querySelector("iframe").style.border, "1px solid red");
assert.equal(document.querySelector("iframe").style.borderRadius, "8px");
assert.equal(document.querySelector("iframe").style.position, "");
assert.equal(document.querySelector("iframe").style.backgroundImage, "");
assert.ok(document.getElementById("layout").classList.contains("mdpad-user-flex"));
assert.equal(document.getElementById("layout").style.display, "flex");
assert.equal(document.getElementById("layout").style.width, "100%");
assert.equal(document.getElementById("layout").style.justifyContent, "center");
assert.equal(document.getElementById("layout").style.alignItems, "center");
assert.equal(document.getElementById("layout").style.position, "");
assert.equal(document.getElementById("layout").style.zIndex, "");
assert.equal(document.getElementById("layout").style.backgroundImage, "");
assert.equal(document.getElementById("left").style.width, "50%");
assert.equal(document.getElementById("left").style.display, "inline-block");
assert.equal(document.getElementById("left").style.textAlign, "center");
assert.equal(document.getElementById("left").style.color, "");
assert.equal(document.querySelector(".blocked-embed").textContent, "Blocked embedded content");
assert.ok(document.querySelector(".katex"));
assert.ok(document.querySelector(".mermaid-block svg"));
assert.equal(document.querySelector("code.language-mermaid"), null);
assert.ok(document.querySelector("code").className.includes("hljs"));

document.querySelector("a").dispatchEvent(new dom.window.MouseEvent("click", { bubbles: true, cancelable: true }));
document.getElementById("local-link").dispatchEvent(new dom.window.MouseEvent("click", { bubbles: true, cancelable: true }));
assert.deepEqual(posted, [
  "open-link:https://example.com/docs",
  "open-link:https://doc.mdpad.local/docs/next%20file.md#part"
]);

posted.length = 0;
document.dispatchEvent(new dom.window.KeyboardEvent("keydown", {
  key: "n",
  ctrlKey: true,
  bubbles: true,
  cancelable: true
}));
assert.deepEqual(posted, ["shortcut:new"]);

assert.equal(dom.window.mdpadFind("hello"), true);
assert.ok(document.querySelector(".find-highlight"));

const exported = dom.window.__mdpadRenderer.exportHtml();
assert.ok(exported.startsWith("<!doctype html>"));
assert.ok(exported.includes("<style>"));
assert.ok(exported.includes("data-mdpad-theme=\"dark\""));
assert.ok(!exported.includes("<script"));
assert.ok(!exported.includes("https://doc.mdpad.local/images/a.png"));
assert.ok(exported.includes("src=\"images/a.png\""));

console.log("Renderer tests passed.");
