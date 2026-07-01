(() => {
  const content = document.getElementById("content");
  const iframeSandboxDefaults = [
    "allow-downloads",
    "allow-forms",
    "allow-modals",
    "allow-pointer-lock",
    "allow-popups",
    "allow-popups-to-escape-sandbox",
    "allow-presentation",
    "allow-same-origin",
    "allow-scripts"
  ];
  const iframeSandboxAllowed = new Set([
    ...iframeSandboxDefaults,
    "allow-storage-access-by-user-activation"
  ]);
  const iframeAllowDefaults = [
    "clipboard-read",
    "clipboard-write",
    "fullscreen",
    "gamepad"
  ];
  const iframeAllowAllowed = new Set([
    ...iframeAllowDefaults,
    "accelerometer",
    "autoplay",
    "encrypted-media",
    "gyroscope",
    "picture-in-picture",
    "web-share",
    "xr-spatial-tracking"
  ]);
  const styleValidators = new Map([
    ["align-items", (value) => isKeyword(value, ["baseline", "center", "end", "flex-end", "flex-start", "start", "stretch"])],
    ["border", isBorder],
    ["border-radius", (value) => isLengthList(value, 4)],
    ["box-sizing", (value) => isKeyword(value, ["border-box", "content-box"])],
    ["display", (value) => isKeyword(value, ["block", "flex", "grid", "inline-block", "inline-flex", "inline-grid"])],
    ["flex", isFlex],
    ["flex-direction", (value) => isKeyword(value, ["column", "column-reverse", "row", "row-reverse"])],
    ["flex-wrap", (value) => isKeyword(value, ["nowrap", "wrap", "wrap-reverse"])],
    ["gap", (value) => isLengthList(value, 2)],
    ["height", (value) => isSize(value, { allowAuto: true })],
    ["justify-content", (value) => isKeyword(value, ["center", "end", "flex-end", "flex-start", "space-around", "space-between", "space-evenly", "start", "stretch"])],
    ["margin", (value) => isLengthList(value, 4, { allowAuto: true })],
    ["max-width", (value) => isSize(value, { allowAuto: true })],
    ["min-height", (value) => isSize(value)],
    ["padding", (value) => isLengthList(value, 4)],
    ["text-align", (value) => isKeyword(value, ["center", "end", "justify", "left", "right", "start"])],
    ["width", (value) => isSize(value, { allowAuto: true })]
  ]);
  let currentBase = "";
  let currentTheme = "system";
  let lastQuery = "";
  let lastMatchIndex = -1;
  let mermaidSequence = 0;

  function sanitize(html) {
    if (!window.DOMPurify) {
      return html;
    }

    return window.DOMPurify.sanitize(html, {
      USE_PROFILES: { html: true },
      ADD_TAGS: ["iframe"],
      ADD_ATTR: [
        "allow",
        "allowfullscreen",
        "height",
        "loading",
        "referrerpolicy",
        "rel",
        "sandbox",
        "src",
        "style",
        "target",
        "title",
        "width"
      ],
      FORBID_TAGS: ["script", "object", "embed"],
      FORBID_ATTR: ["onerror", "onload", "onclick"]
    });
  }

  function isKeyword(value, keywords) {
    return keywords.includes(value.toLowerCase());
  }

  function isSafeCssValue(value) {
    return value.length <= 120
      && !/[<>\\]/.test(value)
      && !/(?:@import|behavior|expression|javascript:|vbscript:|data:)/i.test(value)
      && !/(?:url|image-set|cross-fade|paint|var|env)\s*\(/i.test(value);
  }

  function isLength(value) {
    return value === "0" || /^(?:\d+|\d*\.\d+)(?:px|em|rem|%|vh|vw|vmin|vmax|ch|ex)?$/i.test(value);
  }

  function isSize(value, options = {}) {
    const normalized = value.toLowerCase();
    return isLength(normalized)
      || normalized === "fit-content"
      || normalized === "max-content"
      || normalized === "min-content"
      || Boolean(options.allowAuto && normalized === "auto");
  }

  function isLengthList(value, maxTokens, options = {}) {
    const tokens = value.toLowerCase().split(/\s+/).filter(Boolean);
    return tokens.length > 0
      && tokens.length <= maxTokens
      && tokens.every((token) => isLength(token) || Boolean(options.allowAuto && token === "auto"));
  }

  function isColor(value) {
    return /^#[0-9a-f]{3,8}$/i.test(value)
      || /^[a-z]+$/i.test(value)
      || /^rgba?\(\s*\d{1,3}\s*,\s*\d{1,3}\s*,\s*\d{1,3}(?:\s*,\s*(?:0|1|0?\.\d+))?\s*\)$/i.test(value);
  }

  function isBorder(value) {
    const normalized = value.toLowerCase();
    if (normalized === "0" || normalized === "none") {
      return true;
    }

    const tokens = normalized.split(/\s+/).filter(Boolean);
    return tokens.length >= 2
      && tokens.length <= 3
      && tokens.some(isLength)
      && tokens.some((token) => isKeyword(token, ["dashed", "dotted", "double", "solid"]))
      && (tokens.length === 2 || tokens.some(isColor));
  }

  function isFlex(value) {
    const normalized = value.toLowerCase();
    if (isKeyword(normalized, ["auto", "initial", "none"])) {
      return true;
    }

    const tokens = normalized.split(/\s+/).filter(Boolean);
    return tokens.length > 0
      && tokens.length <= 3
      && tokens.every((token) => /^(?:\d+|\d*\.\d+)$/.test(token) || isSize(token, { allowAuto: true }));
  }

  function sanitizeInlineStyles(root) {
    const styled = root.querySelectorAll("[style]");
    for (const element of styled) {
      const style = element.style;
      const kept = [];
      const keptByName = new Map();

      for (const name of Array.from(style)) {
        const property = name.toLowerCase();
        const value = style.getPropertyValue(name).trim();
        const validator = styleValidators.get(property);
        if (!validator || style.getPropertyPriority(name) || !isSafeCssValue(value) || !validator(value)) {
          continue;
        }

        kept.push(`${property}: ${value}`);
        keptByName.set(property, value);
      }

      if (kept.length === 0) {
        element.removeAttribute("style");
        continue;
      }

      element.setAttribute("style", `${kept.join("; ")};`);
      const display = keptByName.get("display");
      if (display === "flex" || display === "inline-flex") {
        element.classList.add("mdpad-user-flex");
      }
    }
  }

  function applyTheme(theme) {
    const normalized = theme === "light" || theme === "dark" ? theme : "system";
    const root = document.documentElement;
    currentTheme = normalized;

    if (normalized === "system") {
      root.removeAttribute("data-mdpad-theme");
      root.style.colorScheme = "";
    } else {
      root.dataset.mdpadTheme = normalized;
      root.style.colorScheme = normalized;
    }

    const lightHighlight = document.getElementById("highlight-light-theme");
    const darkHighlight = document.getElementById("highlight-dark-theme");
    if (lightHighlight && darkHighlight) {
      lightHighlight.disabled = normalized === "dark";
      darkHighlight.disabled = normalized === "light";
      lightHighlight.media = normalized === "system" ? "(prefers-color-scheme: light)" : "all";
      darkHighlight.media = normalized === "system" ? "(prefers-color-scheme: dark)" : "all";
    }
  }

  function collectStylesForExport() {
    const styles = [];
    for (const sheet of Array.from(document.styleSheets)) {
      try {
        for (const rule of Array.from(sheet.cssRules || [])) {
          styles.push(rule.cssText);
        }
      } catch {
        // Some browser-managed style sheets may not expose rules.
      }
    }
    return styles.join("\n").replaceAll("</style", "<\\/style");
  }

  function rewriteDocumentResourceUrls(root) {
    if (!currentBase) {
      return;
    }

    const rewrite = (element, attribute) => {
      const value = element.getAttribute(attribute);
      if (!value) {
        return;
      }

      try {
        const url = new URL(value, document.location.href);
        if (url.href.startsWith(currentBase)) {
          element.setAttribute(attribute, decodeURIComponent(`${url.pathname.slice(1)}${url.search}${url.hash}`));
        }
      } catch {
        // Keep the existing value when it is not parseable as a URL.
      }
    };

    for (const element of root.querySelectorAll("img[src], iframe[src]")) {
      rewrite(element, "src");
    }
  }

  function exportHtml() {
    const html = document.documentElement.cloneNode(true);
    html.querySelectorAll("script, link[rel='stylesheet'], base[data-mdpad-document-base]").forEach((element) => element.remove());
    rewriteDocumentResourceUrls(html);

    const head = html.querySelector("head") || html.insertBefore(document.createElement("head"), html.firstChild);
    const style = document.createElement("style");
    style.textContent = collectStylesForExport();
    head.appendChild(style);

    return `<!doctype html>\n${html.outerHTML}`;
  }

  function setDocumentBase() {
    let base = document.querySelector("base[data-mdpad-document-base]");
    if (!currentBase) {
      base?.remove();
      return;
    }

    if (!base) {
      base = document.createElement("base");
      base.dataset.mdpadDocumentBase = "true";
      document.head.prepend(base);
    }
    base.href = currentBase;
  }

  function isAllowedResourceUrl(url, options = {}) {
    if (url.protocol === "http:" || url.protocol === "https:") {
      return true;
    }

    return Boolean(options.allowDataImage && url.protocol === "data:" && /^data:image\/(?:png|jpe?g|gif|webp|bmp);/i.test(url.href));
  }

  function resolveResourceUrl(value, options = {}) {
    const src = (value || "").trim();
    if (!src) {
      return "";
    }

    if (!currentBase && !/^(?:[a-z][a-z0-9+.-]*:|\/\/)/i.test(src)) {
      return src;
    }

    try {
      const resolved = new URL(src, currentBase || document.location.href);
      if (!isAllowedResourceUrl(resolved, options)) {
        return "";
      }
      return resolved.toString();
    } catch {
      return "";
    }
  }

  function mergeSandbox(value) {
    const tokens = new Set(iframeSandboxDefaults);
    for (const token of (value || "").toLowerCase().split(/\s+/)) {
      if (iframeSandboxAllowed.has(token)) {
        tokens.add(token);
      }
    }
    return Array.from(tokens).join(" ");
  }

  function mergeAllow(value) {
    const features = new Map(iframeAllowDefaults.map((feature) => [feature, feature]));
    for (const entry of (value || "").split(";")) {
      const trimmed = entry.trim();
      const feature = trimmed.split(/\s+/, 1)[0]?.toLowerCase();
      if (feature && iframeAllowAllowed.has(feature)) {
        features.set(feature, trimmed);
      }
    }
    return Array.from(features.values()).join("; ");
  }

  function absolutizeImages() {
    const images = content.querySelectorAll("img[src]");
    for (const image of images) {
      image.loading = "lazy";
      const resolved = resolveResourceUrl(image.getAttribute("src"), { allowDataImage: true });
      if (!resolved) {
        image.removeAttribute("src");
        image.alt = image.alt ? `${image.alt} (blocked image)` : "Blocked image";
        continue;
      }
      image.src = resolved;
    }
  }

  function hydrateIframes() {
    const frames = Array.from(content.querySelectorAll("iframe"));
    for (const [index, frame] of frames.entries()) {
      const resolved = resolveResourceUrl(frame.getAttribute("src"));
      if (!resolved) {
        const blocked = document.createElement("div");
        blocked.className = "blocked-embed";
        blocked.textContent = "Blocked embedded content";
        frame.replaceWith(blocked);
        continue;
      }

      frame.src = resolved;
      const loading = index === 0 ? "eager" : (frame.getAttribute("loading") || "lazy");
      frame.loading = loading;
      frame.referrerPolicy = "strict-origin-when-cross-origin";
      frame.allowFullscreen = true;
      frame.setAttribute("loading", loading);
      frame.setAttribute("referrerpolicy", "strict-origin-when-cross-origin");
      frame.setAttribute("sandbox", mergeSandbox(frame.getAttribute("sandbox")));
      frame.setAttribute("allow", mergeAllow(frame.getAttribute("allow")));
      frame.setAttribute("allowfullscreen", "");
      if (!frame.title) {
        frame.title = "Embedded content";
      }
    }
  }

  function hydrateLinks() {
    const links = content.querySelectorAll("a[href]");
    for (const link of links) {
      link.rel = "noreferrer";
      link.addEventListener("click", (event) => {
        event.preventDefault();
        const href = link.href || link.getAttribute("href") || "";
        if (window.chrome?.webview) {
          window.chrome.webview.postMessage(`open-link:${href}`);
        }
      });
    }
  }

  function hydrateCode() {
    if (!window.hljs) {
      return;
    }

    for (const block of content.querySelectorAll("pre code:not(.language-mermaid):not(.lang-mermaid)")) {
      window.hljs.highlightElement(block);
    }
  }

  function resolvedMermaidTheme() {
    if (currentTheme === "dark") {
      return "dark";
    }
    if (currentTheme === "system" && window.matchMedia?.("(prefers-color-scheme: dark)")?.matches) {
      return "dark";
    }
    return "default";
  }

  async function hydrateMermaid() {
    if (!window.mermaid?.render) {
      return;
    }

    const blocks = Array.from(content.querySelectorAll("pre > code.language-mermaid, pre > code.lang-mermaid"));
    if (blocks.length === 0) {
      return;
    }

    window.mermaid.initialize({
      startOnLoad: false,
      securityLevel: "strict",
      theme: resolvedMermaidTheme()
    });

    for (const block of blocks) {
      const source = block.textContent || "";
      const wrapper = document.createElement("div");
      wrapper.className = "mermaid-block";
      const diagram = document.createElement("div");
      diagram.className = "mermaid";
      wrapper.appendChild(diagram);
      block.closest("pre")?.replaceWith(wrapper);

      try {
        const id = `mdpad-mermaid-${Date.now()}-${mermaidSequence++}`;
        const result = await window.mermaid.render(id, source);
        diagram.innerHTML = result.svg || "";
        result.bindFunctions?.(diagram);
      } catch (error) {
        wrapper.classList.add("mermaid-error");
        const message = document.createElement("div");
        message.textContent = "Could not render Mermaid diagram";
        const pre = document.createElement("pre");
        const code = document.createElement("code");
        code.textContent = source;
        pre.appendChild(code);
        wrapper.replaceChildren(message, pre);
      }
    }
  }

  function postShortcut(command) {
    if (!window.chrome?.webview) {
      return false;
    }

    window.chrome.webview.postMessage(`shortcut:${command}`);
    return true;
  }

  function handleShortcut(event) {
    if (!event.ctrlKey || event.altKey || event.defaultPrevented) {
      return;
    }

    const key = event.key.toLowerCase();
    let command = "";
    if (key === "n" && !event.shiftKey) {
      command = "new";
    } else if (key === "o" && !event.shiftKey) {
      command = "open";
    } else if (key === "s") {
      command = event.shiftKey ? "save-as" : "save";
    } else if (key === "f" && !event.shiftKey) {
      command = "find";
    } else if (key === "a" && !event.shiftKey) {
      command = "select-all";
    } else if ((key === "+" || key === "=") && !event.shiftKey) {
      command = "zoom-in";
    } else if ((key === "-" || key === "_") && !event.shiftKey) {
      command = "zoom-out";
    } else if (key === "0" && !event.shiftKey) {
      command = "reset-zoom";
    }

    if (!command || !postShortcut(command)) {
      return;
    }

    event.preventDefault();
    event.stopPropagation();
  }

  function hydrateMath() {
    if (!window.renderMathInElement) {
      return;
    }

    window.renderMathInElement(content, {
      delimiters: [
        { left: "$$", right: "$$", display: true },
        { left: "\\[", right: "\\]", display: true },
        { left: "$", right: "$", display: false },
        { left: "\\(", right: "\\)", display: false }
      ],
      throwOnError: false
    });
  }

  function render(payload) {
    currentBase = payload.baseUri || "";
    setDocumentBase();
    applyTheme(payload.theme || "system");
    content.style.fontSize = `${Math.round((payload.zoom || 1) * 100)}%`;
    const template = document.createElement("template");
    template.innerHTML = sanitize(payload.html || "");
    sanitizeInlineStyles(template.content);
    content.replaceChildren(template.content);
    lastQuery = "";
    lastMatchIndex = -1;

    requestAnimationFrame(() => {
      absolutizeImages();
      hydrateIframes();
      hydrateLinks();
    });

    setTimeout(() => {
      hydrateMermaid();
    }, 0);
    setTimeout(hydrateCode, 16);
    setTimeout(hydrateMath, 32);
  }

  function clearFindHighlights() {
    for (const mark of content.querySelectorAll(".find-highlight")) {
      const text = document.createTextNode(mark.textContent || "");
      mark.replaceWith(text);
    }
    content.normalize();
  }

  window.mdpadFind = (query) => {
    if (!query) {
      return false;
    }

    if (query !== lastQuery) {
      clearFindHighlights();
      lastQuery = query;
      lastMatchIndex = -1;
    }

    const walker = document.createTreeWalker(content, NodeFilter.SHOW_TEXT);
    const matches = [];
    let node;
    while ((node = walker.nextNode())) {
      const index = node.nodeValue.toLocaleLowerCase().indexOf(query.toLocaleLowerCase());
      if (index >= 0) {
        matches.push({ node, index });
      }
    }

    if (matches.length === 0) {
      return false;
    }

    lastMatchIndex = (lastMatchIndex + 1) % matches.length;
    const match = matches[lastMatchIndex];
    const range = document.createRange();
    range.setStart(match.node, match.index);
    range.setEnd(match.node, match.index + query.length);

    clearFindHighlights();
    const mark = document.createElement("mark");
    mark.className = "find-highlight";
    range.surroundContents(mark);
    mark.scrollIntoView({ block: "center", behavior: "smooth" });
    return true;
  };

  if (window.chrome?.webview) {
    document.addEventListener("keydown", handleShortcut, true);
    window.chrome.webview.addEventListener("message", (event) => {
      const payload = event.data;
      if (payload?.kind === "render") {
        render(payload);
      }
    });
  }

  window.__mdpadRenderer = { render, sanitize, exportHtml };
})();
