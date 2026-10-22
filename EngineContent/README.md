# EngineContent — shared, referenced, copied only when used

This tree holds the engine-provided fonts and interface art that every
project can use immediately. Projects **reference** it (the host resolves a
content-relative path against the project's own `Content/` first, then here);
nothing is copied into a project when it is created. A project's build
stages only the entries its `ContentManifest.toml` lists.

## Fonts

`FontArchives/` holds vendored typefaces (Inter, Source Sans 3, Source Code
Pro, Euclid Circular A, DM Sans, Space Grotesk, Plus Jakarta Sans and the
nerd-font glyph fallback in the upstream checkout) plus a generated
`FontCatalogue.toml`. The font binaries are intentionally **not** tracked in
git (licence-safe, size-safe) — `EngineContent/FontArchives/` is ignored and
recovered by `Automation/PrepareFonts.py` in the upstream checkout; projects
can equally drop their own typeface into their project `Content/Fonts/` to
shadow the shared one. Until fonts are recovered here, text rendering must
be reported as blocked, never faked.
