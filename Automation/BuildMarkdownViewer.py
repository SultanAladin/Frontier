#!/usr/bin/env python3
"""Builds Documentation/Plans/MarkdownStudioViewer.html — a Visual Studio Dark .md viewer
embedding all repository Markdown documents so they can be browsed offline in one place."""

from __future__ import annotations

import json
import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
OUTPUT_HTML = REPO_ROOT / "Documentation" / "Plans" / "MarkdownStudioViewer.html"


def collect_markdown_documents() -> list[dict[str, str]]:
    ordered_paths: list[pathlib.Path] = []

    plans_dir = REPO_ROOT / "Documentation" / "Plans"
    if plans_dir.exists():
        # Put active focused plan first, then remaining plans alphabetically
        preferred = plans_dir / "DocumentSpacesAndRevisionPlan.md"
        if preferred.exists():
            ordered_paths.append(preferred)
        for p in sorted(plans_dir.glob("*.md")):
            if p not in ordered_paths:
                ordered_paths.append(p)

    doc_dir = REPO_ROOT / "Documentation"
    for p in sorted(doc_dir.glob("*.md")):
        ordered_paths.append(p)

    skills_dir = doc_dir / "AgenticInstructions"
    if skills_dir.exists():
        for p in sorted(skills_dir.glob("*.md")):
            ordered_paths.append(p)

    for root_name in ("README.md", "CLAUDE.md", "AGENTS.md"):
        rp = REPO_ROOT / root_name
        if rp.exists():
            ordered_paths.append(rp)

    docs: list[dict[str, str]] = []
    for path in ordered_paths:
        rel = path.relative_to(REPO_ROOT).as_posix()
        parts = rel.split("/")
        if rel.startswith("Documentation/Plans/"):
            group = "Plans"
        elif rel.startswith("Documentation/AgenticInstructions/"):
            group = "AgenticInstructions"
        elif rel.startswith("Documentation/"):
            group = "Documentation"
        else:
            group = "Repository Root"
        docs.append(
            {
                "path": rel,
                "name": parts[-1],
                "group": group,
                "content": path.read_text(encoding="utf-8"),
            }
        )
    return docs


HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Frontier Markdown Studio — Documentation &amp; Plans</title>
<style>
  :root {
    --vs-activity: #181818;
    --vs-sidebar: #181818;
    --vs-editor: #1e1e1e;
    --vs-header: #181818;
    --vs-tab-inactive: #181818;
    --vs-tab-active: #1e1e1e;
    --vs-border: #2b2b2b;
    --vs-border-strong: #3c3c3c;
    --vs-hover: #2a2d2e;
    --vs-selection: #04395e;
    --vs-accent: #0078d4;
    --vs-text: #cccccc;
    --vs-text-bright: #e8e8e8;
    --vs-text-dim: #858585;
    --vs-code-bg: #141414;
    --syn-heading: #569cd6;
    --syn-bold: #e5c07b;
    --syn-code: #ce9178;
    --syn-quote: #6a9955;
    --syn-table: #4ec9b0;
    --syn-link: #4fc1ff;
    --syn-comment: #6a9955;
    --doc-scale: 1;
    --font-ui: "Segoe UI", "Inter", -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
    --font-mono: "Cascadia Code", "Consolas", "JetBrains Mono", ui-monospace, SFMono-Regular, Menlo, monospace;
  }

  * {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
    box-shadow: none !important;
  }

  html, body {
    height: 100%;
    overflow: hidden;
    background: var(--vs-editor);
    color: var(--vs-text);
    font-family: var(--font-ui);
    font-size: 13px;
    -webkit-font-smoothing: antialiased;
  }

  .ide-root {
    display: grid;
    grid-template-rows: 38px 1fr 24px;
    height: 100vh;
    width: 100vw;
  }

  /* Top Visual Studio Title / Command Bar */
  .titlebar {
    background: var(--vs-header);
    border-bottom: 1px solid var(--vs-border);
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 12px;
    gap: 12px;
    user-select: none;
  }

  .titlebar-left {
    display: flex;
    align-items: center;
    gap: 10px;
    min-width: 0;
  }

  .vs-mark {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 22px;
    height: 22px;
    border-radius: 4px;
    background: var(--vs-accent);
    color: #ffffff;
    font-family: var(--font-mono);
    font-size: 11px;
    font-weight: 700;
  }

  .app-title {
    font-size: 12.5px;
    font-weight: 600;
    color: var(--vs-text-bright);
    white-space: nowrap;
  }

  .search-shell {
    flex: 0 1 380px;
    display: flex;
    align-items: center;
    background: #242424;
    border: 1px solid var(--vs-border-strong);
    border-radius: 5px;
    padding: 0 9px;
    height: 26px;
    gap: 7px;
  }

  .search-shell:focus-within {
    border-color: var(--vs-accent);
  }

  .search-shell input {
    background: transparent;
    border: none;
    outline: none;
    color: var(--vs-text-bright);
    font-family: var(--font-ui);
    font-size: 12.5px;
    width: 100%;
  }

  .search-shell input::placeholder {
    color: var(--vs-text-dim);
  }

  .toolbar-right {
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .mode-group {
    display: inline-flex;
    border: 1px solid var(--vs-border-strong);
    border-radius: 5px;
    overflow: hidden;
    background: #202020;
  }

  .mode-btn, .tool-btn {
    appearance: none;
    border: none;
    background: transparent;
    color: var(--vs-text);
    font-family: var(--font-ui);
    font-size: 12px;
    padding: 4px 10px;
    cursor: pointer;
    height: 24px;
    display: inline-flex;
    align-items: center;
    gap: 5px;
  }

  .mode-btn + .mode-btn {
    border-left: 1px solid var(--vs-border-strong);
  }

  .mode-btn:hover, .tool-btn:hover {
    background: var(--vs-hover);
    color: #ffffff;
  }

  .mode-btn.active {
    background: var(--vs-accent);
    color: #ffffff;
  }

  .tool-btn {
    border: 1px solid var(--vs-border-strong);
    border-radius: 5px;
    background: #202020;
    padding: 0 8px;
  }

  .tool-btn.active {
    border-color: var(--vs-accent);
    color: #ffffff;
  }

  /* Main Workbench */
  .workbench {
    display: grid;
    grid-template-columns: 270px 1fr 230px;
    min-height: 0;
    overflow: hidden;
  }

  .workbench.hide-sidebar {
    grid-template-columns: 0px 1fr 230px;
  }

  .workbench.hide-outline {
    grid-template-columns: 270px 1fr 0px;
  }

  .workbench.hide-sidebar.hide-outline {
    grid-template-columns: 0px 1fr 0px;
  }

  /* Explorer Sidebar */
  .sidebar {
    background: var(--vs-sidebar);
    border-right: 1px solid var(--vs-border);
    display: flex;
    flex-direction: column;
    min-height: 0;
    overflow: hidden;
    user-select: none;
  }

  .pane-caption {
    height: 32px;
    padding: 0 12px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--vs-text-dim);
    border-bottom: 1px solid var(--vs-border);
    flex-shrink: 0;
  }

  .tree-scroll {
    overflow-y: auto;
    flex: 1;
    padding: 6px 0 14px;
  }

  .folder-header {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 5px 10px;
    cursor: pointer;
    color: var(--vs-text-bright);
    font-weight: 600;
    font-size: 12px;
  }

  .folder-header:hover {
    background: var(--vs-hover);
  }

  .caret {
    display: inline-block;
    width: 12px;
    text-align: center;
    color: var(--vs-text-dim);
    font-size: 10px;
  }

  .folder-items.collapsed {
    display: none;
  }

  .tree-row {
    display: flex;
    align-items: center;
    gap: 7px;
    padding: 5px 12px 5px 26px;
    cursor: pointer;
    color: var(--vs-text);
    font-size: 12.5px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .tree-row:hover {
    background: var(--vs-hover);
    color: var(--vs-text-bright);
  }

  .tree-row.active {
    background: var(--vs-selection);
    color: #ffffff;
  }

  .md-tag {
    font-family: var(--font-mono);
    font-size: 10px;
    font-weight: 700;
    color: #519aba;
    flex-shrink: 0;
  }

  /* Center Editor Area */
  .editor-column {
    display: flex;
    flex-direction: column;
    min-width: 0;
    min-height: 0;
    background: var(--vs-editor);
  }

  .tabs-strip {
    display: flex;
    align-items: stretch;
    height: 35px;
    background: var(--vs-header);
    border-bottom: 1px solid var(--vs-border);
    overflow-x: auto;
    scrollbar-width: none;
    user-select: none;
    flex-shrink: 0;
  }

  .tabs-strip::-webkit-scrollbar {
    display: none;
  }

  .editor-tab {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 0 10px 0 12px;
    background: var(--vs-tab-inactive);
    border-right: 1px solid var(--vs-border);
    border-top: 2px solid transparent;
    color: var(--vs-text-dim);
    font-size: 12.5px;
    cursor: pointer;
    white-space: nowrap;
    flex-shrink: 0;
  }

  .editor-tab:hover {
    background: #1f1f1f;
    color: var(--vs-text-bright);
  }

  .editor-tab.active {
    background: var(--vs-tab-active);
    border-top-color: var(--vs-accent);
    color: #ffffff;
  }

  .tab-close {
    width: 18px;
    height: 18px;
    border-radius: 4px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font-size: 13px;
    color: var(--vs-text-dim);
  }

  .tab-close:hover {
    background: #353535;
    color: #ffffff;
  }

  .breadcrumb-bar {
    height: 26px;
    padding: 0 16px;
    background: var(--vs-editor);
    border-bottom: 1px solid var(--vs-border);
    display: flex;
    align-items: center;
    justify-content: space-between;
    color: var(--vs-text-dim);
    font-size: 12px;
    flex-shrink: 0;
  }

  .breadcrumb-path span.sep {
    margin: 0 6px;
    color: #555555;
  }

  .breadcrumb-path span.leaf {
    color: var(--vs-text-bright);
  }

  /* Split / Single Editor Viewport */
  .editor-panes {
    flex: 1;
    min-height: 0;
    display: grid;
    grid-template-columns: 1fr;
    overflow: hidden;
  }

  .editor-panes.mode-split {
    grid-template-columns: 1fr 1fr;
  }

  .source-pane {
    overflow: auto;
    background: var(--vs-editor);
    border-right: 1px solid var(--vs-border);
    font-family: var(--font-mono);
    font-size: calc(13px * var(--doc-scale));
    line-height: 1.6;
    padding: 12px 0 40px;
  }

  .source-line {
    display: grid;
    grid-template-columns: 54px 1fr;
    padding-right: 18px;
  }

  .source-line:hover {
    background: #242424;
  }

  .line-num {
    text-align: right;
    padding-right: 16px;
    color: #6e7681;
    user-select: none;
    font-size: 0.92em;
  }

  .line-code {
    white-space: pre-wrap;
    word-break: break-word;
    color: #d4d4d4;
  }

  /* Rendered Markdown Preview */
  .preview-pane {
    overflow-y: auto;
    background: var(--vs-editor);
    padding: 28px 40px 64px;
  }

  .md-body {
    max-width: 920px;
    margin: 0 auto;
    font-size: calc(14px * var(--doc-scale));
    line-height: 1.65;
    color: #d4d4d4;
  }

  .md-body h1 {
    font-size: 1.75em;
    font-weight: 650;
    color: #ffffff;
    padding-bottom: 10px;
    margin: 0 0 16px;
    border-bottom: 1px solid var(--vs-border-strong);
    letter-spacing: -0.01em;
  }

  .md-body h2 {
    font-size: 1.32em;
    font-weight: 620;
    color: #f0f0f0;
    padding-bottom: 6px;
    margin: 28px 0 12px;
    border-bottom: 1px solid var(--vs-border);
  }

  .md-body h3 {
    font-size: 1.1em;
    font-weight: 600;
    color: #e6e6e6;
    margin: 20px 0 8px;
  }

  .md-body p {
    margin: 10px 0;
  }

  .md-body ul, .md-body ol {
    margin: 8px 0 12px 24px;
  }

  .md-body li {
    margin: 4px 0;
  }

  .md-body hr {
    border: none;
    border-top: 1px solid var(--vs-border-strong);
    margin: 22px 0;
  }

  .md-body blockquote {
    border-left: 3px solid var(--vs-accent);
    background: #171717;
    border-top: 1px solid var(--vs-border);
    border-right: 1px solid var(--vs-border);
    border-bottom: 1px solid var(--vs-border);
    border-radius: 0 6px 6px 0;
    padding: 10px 14px;
    margin: 14px 0;
    color: #b8b8b8;
  }

  .md-body code {
    font-family: var(--font-mono);
    font-size: 0.88em;
    background: var(--vs-code-bg);
    border: 1px solid #2e2e2e;
    color: #ce9178;
    padding: 1px 5px;
    border-radius: 4px;
  }

  .md-body pre {
    background: var(--vs-code-bg);
    border: 1px solid var(--vs-border-strong);
    border-radius: 6px;
    padding: 14px 16px;
    margin: 14px 0;
    overflow-x: auto;
    line-height: 1.5;
  }

  .md-body pre code {
    background: none;
    border: none;
    padding: 0;
    color: #d4d4d4;
    font-size: 0.86em;
  }

  .md-body .table-scroll {
    overflow-x: auto;
    margin: 14px 0;
    border: 1px solid var(--vs-border-strong);
    border-radius: 6px;
  }

  .md-body table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.93em;
  }

  .md-body th {
    background: #161616;
    color: #e8e8e8;
    font-weight: 600;
    padding: 9px 12px;
    border-bottom: 1px solid var(--vs-border-strong);
    border-right: 1px solid var(--vs-border);
    text-align: left;
  }

  .md-body th:last-child, .md-body td:last-child {
    border-right: none;
  }

  .md-body td {
    padding: 8px 12px;
    border-bottom: 1px solid var(--vs-border);
    border-right: 1px solid var(--vs-border);
    vertical-align: top;
  }

  .md-body tr:last-child td {
    border-bottom: none;
  }

  .md-body tr:nth-child(even) td {
    background: rgba(255, 255, 255, 0.018);
  }

  .md-body a {
    color: var(--syn-link);
    text-decoration: none;
  }

  .md-body a:hover {
    text-decoration: underline;
  }

  .md-body mark {
    background: rgba(234, 179, 8, 0.35);
    color: #ffffff;
    border-radius: 2px;
    padding: 0 2px;
  }

  /* Right Outline Rail */
  .outline-rail {
    background: var(--vs-sidebar);
    border-left: 1px solid var(--vs-border);
    display: flex;
    flex-direction: column;
    min-height: 0;
    overflow: hidden;
    user-select: none;
  }

  .outline-list {
    overflow-y: auto;
    flex: 1;
    padding: 8px 0 16px;
  }

  .outline-entry {
    padding: 5px 12px;
    font-size: 12px;
    color: var(--vs-text);
    cursor: pointer;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .outline-entry:hover {
    background: var(--vs-hover);
    color: #ffffff;
  }

  .outline-entry.lvl-2 { padding-left: 22px; color: #b5b5b5; }
  .outline-entry.lvl-3 { padding-left: 34px; color: var(--vs-text-dim); font-size: 11.5px; }

  /* Status Bar */
  .statusbar {
    background: #181818;
    border-top: 1px solid var(--vs-border);
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 12px;
    font-size: 11.5px;
    color: var(--vs-text-dim);
    user-select: none;
  }

  .status-left, .status-right {
    display: flex;
    align-items: center;
    gap: 14px;
  }

  .status-accent {
    color: #58a6ff;
    font-family: var(--font-mono);
  }

  .hidden {
    display: none !important;
  }

  /* Syntax colors inside Raw Source */
  .tok-h { color: var(--syn-heading); font-weight: 600; }
  .tok-q { color: var(--syn-quote); }
  .tok-t { color: var(--syn-table); }
  .tok-c { color: var(--syn-code); }
  .tok-b { color: var(--syn-bold); font-weight: 600; }
  .tok-l { color: var(--syn-link); }
</style>
</head>
<body>
<div class="ide-root">

  <!-- TOP VISUAL STUDIO BAR -->
  <header class="titlebar">
    <div class="titlebar-left">
      <button class="tool-btn active" id="btnToggleSidebar" title="Toggle Solution Explorer">Explorer</button>
      <span class="vs-mark">MD</span>
      <span class="app-title">Frontier Markdown Studio</span>
    </div>

    <div class="search-shell">
      <span style="color:#858585;font-size:12px;">🔍</span>
      <input type="text" id="searchInput" placeholder="Filter .md files or highlight text in active document...">
    </div>

    <div class="toolbar-right">
      <div class="mode-group">
        <button class="mode-btn active" data-mode="preview" onclick="setViewMode('preview', this)">Preview</button>
        <button class="mode-btn" data-mode="split" onclick="setViewMode('split', this)">Split</button>
        <button class="mode-btn" data-mode="source" onclick="setViewMode('source', this)">Raw .md</button>
      </div>
      <button class="tool-btn" onclick="adjustZoom(-0.08)" title="Zoom Out">A-</button>
      <button class="tool-btn" id="zoomLabel" onclick="resetZoom()" title="Reset Zoom">100%</button>
      <button class="tool-btn" onclick="adjustZoom(0.08)" title="Zoom In">A+</button>
      <button class="tool-btn active" id="btnToggleOutline" title="Toggle Document Outline">Outline</button>
    </div>
  </header>

  <!-- WORKBENCH -->
  <div class="workbench" id="workbench">

    <!-- LEFT EXPLORER -->
    <aside class="sidebar" id="sidebar">
      <div class="pane-caption">
        <span>Solution Documents (.md)</span>
        <span id="docCountLabel">0</span>
      </div>
      <div class="tree-scroll" id="treeContainer"></div>
    </aside>

    <!-- CENTER EDITOR -->
    <main class="editor-column">
      <div class="tabs-strip" id="tabsStrip"></div>
      <div class="breadcrumb-bar">
        <div class="breadcrumb-path" id="breadcrumbPath"></div>
        <div id="matchInfo" style="font-family:var(--font-mono);font-size:11px;color:#9cdcfe;"></div>
      </div>
      <div class="editor-panes" id="editorPanes">
        <div class="source-pane hidden" id="sourcePane"></div>
        <div class="preview-pane" id="previewPane">
          <article class="md-body" id="mdBody"></article>
        </div>
      </div>
    </main>

    <!-- RIGHT OUTLINE -->
    <aside class="outline-rail" id="outlineRail">
      <div class="pane-caption">
        <span>Document Outline</span>
      </div>
      <div class="outline-list" id="outlineList"></div>
    </aside>

  </div>

  <!-- BOTTOM STATUS BAR -->
  <footer class="statusbar">
    <div class="status-left">
      <span class="status-accent" id="statusPath">—</span>
    </div>
    <div class="status-right">
      <span id="statusLines">0 lines</span>
      <span id="statusWords">0 words</span>
      <span id="statusSize">0.0 KB</span>
      <span>UTF-8</span>
      <span>LF</span>
      <span>Markdown</span>
    </div>
  </footer>

</div>

<script id="embeddedDocs" type="application/json">__DOCUMENTS_JSON__</script>
<script>
(function() {
  const DOCS = JSON.parse(document.getElementById('embeddedDocs').textContent);
  const docByPath = new Map(DOCS.map(d => [d.path, d]));

  let openTabs = [];
  let activePath = '';
  let viewMode = 'preview';
  let zoomScale = 1.0;
  let searchQuery = '';
  const collapsedGroups = new Set();

  // Pre-open the Plans in tabs so switching is instant
  DOCS.filter(d => d.group === 'Plans').forEach(d => openTabs.push(d.path));
  if (openTabs.length === 0 && DOCS.length > 0) {
    openTabs.push(DOCS[0].path);
  }
  activePath = openTabs[0] || '';

  function escapeHtml(str) {
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function highlightQuery(htmlText) {
    if (!searchQuery || searchQuery.length < 2) return htmlText;
    const escapedQ = searchQuery.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const re = new RegExp('(' + escapedQ + ')', 'gi');
    // Only highlight outside HTML tags
    return htmlText.replace(/(>[^<]*<)/g, seg => {
      const inner = seg.slice(1, -1);
      return '>' + inner.replace(re, '<mark>$1</mark>') + '<';
    });
  }

  function resolveRelativePath(basePath, href) {
    if (/^(https?:|mailto:|#)/i.test(href)) return null;
    const cleanHref = href.split('#')[0];
    const baseParts = basePath.split('/').slice(0, -1);
    for (const part of cleanHref.split('/')) {
      if (!part || part === '.') continue;
      if (part === '..') baseParts.pop();
      else baseParts.push(part);
    }
    const candidate = baseParts.join('/');
    return docByPath.has(candidate) ? candidate : null;
  }

  function formatInline(raw, currentDocPath) {
    // Split inline code spans first so inner symbols are untouched
    const tokens = [];
    let s = escapeHtml(raw);
    s = s.replace(/`([^`]+)`/g, (_, code) => {
      const idx = tokens.push('<code>' + code + '</code>') - 1;
      return '\x00TOK' + idx + '\x00';
    });

    // Links [label](href)
    s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, (_, label, href) => {
      const resolved = resolveRelativePath(currentDocPath, href);
      if (resolved) {
        return '<a href="#" data-doclink="' + escapeHtml(resolved) + '">' + label + '</a>';
      }
      return '<a href="' + href + '" target="_blank" rel="noopener">' + label + '</a>';
    });

    // Bold & Italic
    s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    s = s.replace(/(^|[^*])\*([^*\n]+)\*/g, '$1<em>$2</em>');

    s = s.replace(/\x00TOK(\d+)\x00/g, (_, i) => tokens[Number(i)]);
    return s;
  }

  function parseMarkdown(md, currentDocPath) {
    // Strip optional HTML comments at top (like PARSE:EMOJI-RULES)
    const cleaned = md.replace(/<!--[\s\S]*?-->/g, '');
    const lines = cleaned.replace(/\r\n/g, '\n').split('\n');
    let html = '';
    const headings = [];
    let i = 0;

    while (i < lines.length) {
      const line = lines[i];

      // Fenced code block
      if (line.trimStart().startsWith('```')) {
        i++;
        const buf = [];
        while (i < lines.length && !lines[i].trimStart().startsWith('```')) {
          buf.push(escapeHtml(lines[i]));
          i++;
        }
        i++; // skip closing fence
        html += '<pre><code>' + buf.join('\n') + '</code></pre>';
        continue;
      }

      // Horizontal rule
      if (/^\s*([-*_])\s*\1\s*\1[\s\-*_]*$/.test(line)) {
        html += '<hr>';
        i++;
        continue;
      }

      // Heading
      const hMatch = line.match(/^(#{1,6})\s+(.*)$/);
      if (hMatch) {
        const level = hMatch[1].length;
        const text = hMatch[2].trim();
        const slug = 'sec-' + headings.length;
        if (level <= 3) {
          headings.push({ level, text: text.replace(/[`*]/g, ''), slug });
        }
        html += '<h' + level + ' id="' + slug + '">' + formatInline(text, currentDocPath) + '</h' + level + '>';
        i++;
        continue;
      }

      // GFM Table
      if (line.includes('|') && i + 1 < lines.length && /^\s*\|?[\s:-]+\|[\s|:-]*$/.test(lines[i + 1])) {
        const splitRow = r => {
          let t = r.trim();
          if (t.startsWith('|')) t = t.slice(1);
          if (t.endsWith('|')) t = t.slice(0, -1);
          return t.split('|').map(c => c.trim());
        };
        const headers = splitRow(line);
        const aligns = splitRow(lines[i + 1]).map(a => {
          if (a.startsWith(':') && a.endsWith(':')) return 'center';
          if (a.endsWith(':')) return 'right';
          return 'left';
        });
        i += 2;
        let tbl = '<div class="table-scroll"><table><thead><tr>';
        headers.forEach((h, idx) => {
          tbl += '<th style="text-align:' + (aligns[idx] || 'left') + '">' + formatInline(h, currentDocPath) + '</th>';
        });
        tbl += '</tr></thead><tbody>';
        while (i < lines.length && lines[i].includes('|') && lines[i].trim() !== '') {
          const cells = splitRow(lines[i]);
          tbl += '<tr>';
          headers.forEach((_, idx) => {
            tbl += '<td style="text-align:' + (aligns[idx] || 'left') + '">' + formatInline(cells[idx] || '', currentDocPath) + '</td>';
          });
          tbl += '</tr>';
          i++;
        }
        tbl += '</tbody></table></div>';
        html += tbl;
        continue;
      }

      // Blockquote
      if (line.trimStart().startsWith('>')) {
        const qLines = [];
        while (i < lines.length && lines[i].trimStart().startsWith('>')) {
          qLines.push(lines[i].replace(/^\s*>\s?/, ''));
          i++;
        }
        html += '<blockquote>' + qLines.map(ql => '<p>' + formatInline(ql, currentDocPath) + '</p>').join('') + '</blockquote>';
        continue;
      }

      // Unordered list
      if (/^\s*[-*+]\s+/.test(line)) {
        html += '<ul>';
        while (i < lines.length && /^\s*[-*+]\s+/.test(lines[i])) {
          const itemText = lines[i].replace(/^\s*[-*+]\s+/, '');
          html += '<li>' + formatInline(itemText, currentDocPath) + '</li>';
          i++;
        }
        html += '</ul>';
        continue;
      }

      // Ordered list
      if (/^\s*\d+\.\s+/.test(line)) {
        html += '<ol>';
        while (i < lines.length && /^\s*\d+\.\s+/.test(lines[i])) {
          const itemText = lines[i].replace(/^\s*\d+\.\s+/, '');
          html += '<li>' + formatInline(itemText, currentDocPath) + '</li>';
          i++;
        }
        html += '</ol>';
        continue;
      }

      // Blank line
      if (line.trim() === '') {
        i++;
        continue;
      }

      // Paragraph
      const pLines = [];
      while (
        i < lines.length &&
        lines[i].trim() !== '' &&
        !lines[i].trimStart().startsWith('#') &&
        !lines[i].trimStart().startsWith('```') &&
        !lines[i].trimStart().startsWith('>') &&
        !/^\s*[-*+]\s+/.test(lines[i]) &&
        !/^\s*\d+\.\s+/.test(lines[i])
      ) {
        pLines.push(formatInline(lines[i], currentDocPath));
        i++;
      }
      html += '<p>' + pLines.join(' ') + '</p>';
    }

    return { html, headings };
  }

  function colorizeSourceLine(rawLine) {
    let s = escapeHtml(rawLine);
    if (/^#{1,6}\s/.test(rawLine)) return '<span class="tok-h">' + s + '</span>';
    if (/^\s*&gt;/.test(s)) return '<span class="tok-q">' + s + '</span>';
    if (rawLine.trimStart().startsWith('|')) return '<span class="tok-t">' + s + '</span>';
    s = s.replace(/`([^`]+)`/g, '<span class="tok-c">`$1`</span>');
    s = s.replace(/\*\*([^*]+)\*\*/g, '<span class="tok-b">**$1**</span>');
    s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<span class="tok-l">[$1]($2)</span>');
    return s;
  }

  function renderTree() {
    const container = document.getElementById('treeContainer');
    const q = searchQuery.toLowerCase();
    const groups = ['Plans', 'Documentation', 'AgenticInstructions', 'Repository Root'];
    let visibleTotal = 0;
    let out = '';

    for (const grp of groups) {
      const items = DOCS.filter(d => d.group === grp && (
        !q || d.name.toLowerCase().includes(q) || d.content.toLowerCase().includes(q)
      ));
      if (items.length === 0) continue;
      visibleTotal += items.length;
      const collapsed = collapsedGroups.has(grp);
      out += '<div class="folder-block">';
      out += '<div class="folder-header" data-group="' + grp + '">';
      out += '<span class="caret">' + (collapsed ? '▸' : '▾') + '</span>';
      out += '<span>' + escapeHtml(grp) + '</span>';
      out += '</div>';
      out += '<div class="folder-items' + (collapsed ? ' collapsed' : '') + '">';
      for (const doc of items) {
        const cls = doc.path === activePath ? 'tree-row active' : 'tree-row';
        out += '<div class="' + cls + '" data-open="' + escapeHtml(doc.path) + '" title="' + escapeHtml(doc.path) + '">';
        out += '<span class="md-tag">MD</span>';
        out += '<span>' + escapeHtml(doc.name) + '</span>';
        out += '</div>';
      }
      out += '</div></div>';
    }

    container.innerHTML = out;
    document.getElementById('docCountLabel').textContent = String(visibleTotal);
  }

  function renderTabs() {
    const strip = document.getElementById('tabsStrip');
    strip.innerHTML = openTabs.map(p => {
      const doc = docByPath.get(p);
      if (!doc) return '';
      const cls = p === activePath ? 'editor-tab active' : 'editor-tab';
      return (
        '<div class="' + cls + '" data-tab="' + escapeHtml(p) + '">' +
        '<span class="md-tag">MD</span>' +
        '<span>' + escapeHtml(doc.name) + '</span>' +
        '<span class="tab-close" data-close="' + escapeHtml(p) + '">×</span>' +
        '</div>'
      );
    }).join('');
  }

  function renderActiveDocument() {
    const doc = docByPath.get(activePath);
    const mdBody = document.getElementById('mdBody');
    const sourcePane = document.getElementById('sourcePane');
    const previewPane = document.getElementById('previewPane');
    const editorPanes = document.getElementById('editorPanes');
    const outlineList = document.getElementById('outlineList');
    const breadcrumb = document.getElementById('breadcrumbPath');

    editorPanes.classList.toggle('mode-split', viewMode === 'split');
    sourcePane.classList.toggle('hidden', viewMode === 'preview');
    previewPane.classList.toggle('hidden', viewMode === 'source');

    if (!doc) {
      mdBody.innerHTML = '<p style="color:#858585;">Select a Markdown document from the Solution Explorer on the left.</p>';
      sourcePane.innerHTML = '';
      outlineList.innerHTML = '';
      breadcrumb.textContent = '';
      return;
    }

    // Breadcrumb
    const segs = doc.path.split('/');
    breadcrumb.innerHTML = segs.map((s, idx) =>
      idx + 1 === segs.length
        ? '<span class="leaf">' + escapeHtml(s) + '</span>'
        : '<span>' + escapeHtml(s) + '</span><span class="sep">›</span>'
    ).join('');

    // Source lines
    const rawLines = doc.content.replace(/\r\n/g, '\n').split('\n');
    sourcePane.innerHTML = rawLines.map((ln, idx) =>
      '<div class="source-line"><span class="line-num">' + (idx + 1) + '</span><span class="line-code">' +
      highlightQuery(colorizeSourceLine(ln)) + '</span></div>'
    ).join('');

    // Rendered Markdown
    const parsed = parseMarkdown(doc.content, doc.path);
    mdBody.innerHTML = highlightQuery('<div>' + parsed.html + '</div>');

    // Outline
    outlineList.innerHTML = parsed.headings.map(h =>
      '<div class="outline-entry lvl-' + h.level + '" data-jump="' + h.slug + '" title="' + escapeHtml(h.text) + '">' +
      escapeHtml(h.text) + '</div>'
    ).join('');

    // Status bar metrics
    const words = doc.content.trim().split(/\s+/).filter(Boolean).length;
    const kb = (new Blob([doc.content]).size / 1024).toFixed(1);
    document.getElementById('statusPath').textContent = doc.path;
    document.getElementById('statusLines').textContent = rawLines.length + ' lines';
    document.getElementById('statusWords').textContent = words + ' words';
    document.getElementById('statusSize').textContent = kb + ' KB';

    if (searchQuery && searchQuery.length >= 2) {
      const re = new RegExp(searchQuery.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi');
      const count = (doc.content.match(re) || []).length;
      document.getElementById('matchInfo').textContent = count + (count === 1 ? ' match' : ' matches');
    } else {
      document.getElementById('matchInfo').textContent = '';
    }
  }

  function openDocument(path) {
    if (!docByPath.has(path)) return;
    if (!openTabs.includes(path)) openTabs.push(path);
    activePath = path;
    renderTree();
    renderTabs();
    renderActiveDocument();
    document.getElementById('previewPane').scrollTop = 0;
    document.getElementById('sourcePane').scrollTop = 0;
  }

  function closeTab(path) {
    openTabs = openTabs.filter(p => p !== path);
    if (activePath === path) {
      activePath = openTabs[openTabs.length - 1] || '';
    }
    renderTree();
    renderTabs();
    renderActiveDocument();
  }

  window.setViewMode = function(mode, btn) {
    viewMode = mode;
    document.querySelectorAll('.mode-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    renderActiveDocument();
  };

  window.adjustZoom = function(delta) {
    zoomScale = Math.min(1.6, Math.max(0.75, Math.round((zoomScale + delta) * 100) / 100));
    document.documentElement.style.setProperty('--doc-scale', String(zoomScale));
    document.getElementById('zoomLabel').textContent = Math.round(zoomScale * 100) + '%';
  };

  window.resetZoom = function() {
    zoomScale = 1.0;
    document.documentElement.style.setProperty('--doc-scale', '1');
    document.getElementById('zoomLabel').textContent = '100%';
  };

  // Event delegation
  document.getElementById('treeContainer').addEventListener('click', e => {
    const folder = e.target.closest('[data-group]');
    if (folder) {
      const g = folder.getAttribute('data-group');
      if (collapsedGroups.has(g)) collapsedGroups.delete(g);
      else collapsedGroups.add(g);
      renderTree();
      return;
    }
    const item = e.target.closest('[data-open]');
    if (item) openDocument(item.getAttribute('data-open'));
  });

  document.getElementById('tabsStrip').addEventListener('click', e => {
    const closeBtn = e.target.closest('[data-close]');
    if (closeBtn) {
      e.stopPropagation();
      closeTab(closeBtn.getAttribute('data-close'));
      return;
    }
    const tab = e.target.closest('[data-tab]');
    if (tab) openDocument(tab.getAttribute('data-tab'));
  });

  document.getElementById('outlineList').addEventListener('click', e => {
    const entry = e.target.closest('[data-jump]');
    if (!entry) return;
    const target = document.getElementById(entry.getAttribute('data-jump'));
    if (target) target.scrollIntoView({ behavior: 'smooth', block: 'start' });
  });

  document.getElementById('mdBody').addEventListener('click', e => {
    const link = e.target.closest('[data-doclink]');
    if (link) {
      e.preventDefault();
      openDocument(link.getAttribute('data-doclink'));
    }
  });

  document.getElementById('searchInput').addEventListener('input', e => {
    searchQuery = e.target.value.trim();
    renderTree();
    renderActiveDocument();
  });

  document.getElementById('btnToggleSidebar').addEventListener('click', function() {
    const wb = document.getElementById('workbench');
    const hidden = wb.classList.toggle('hide-sidebar');
    this.classList.toggle('active', !hidden);
  });

  document.getElementById('btnToggleOutline').addEventListener('click', function() {
    const wb = document.getElementById('workbench');
    const hidden = wb.classList.toggle('hide-outline');
    this.classList.toggle('active', !hidden);
  });

  renderTree();
  renderTabs();
  renderActiveDocument();
})();
</script>
</body>
</html>
"""


def main() -> None:
    docs = collect_markdown_documents()
    # Prevent accidental </script> sequence termination inside embedded JSON
    payload = json.dumps(docs, ensure_ascii=False).replace("</", "<\\/")
    html_out = HTML_TEMPLATE.replace("__DOCUMENTS_JSON__", payload)
    OUTPUT_HTML.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_HTML.write_text(html_out, encoding="utf-8")
    print(f"[BuildMarkdownViewer] Embedded {len(docs)} .md documents -> {OUTPUT_HTML.relative_to(REPO_ROOT)}")


if __name__ == "__main__":
    main()
