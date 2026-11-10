# Frontier — Agent Instructions

Frontier is a cross-platform native C++20 engine and general-purpose editor built on Vulkan and Slang,
accompanied by retained validation exhibits under `Exhibits/`.

---

## 🔴 Mandatory Skill Reading — Before Writing Code or Engine Docs

Read these authoritative skill files before producing any C, C++, shader, UI, or engine Markdown in a session.
Do not load them for chat-only questions or file searches; once loaded, they override all agent defaults:

- `AgenticInstructions/SKILL-Naming.md` — `<Subject><Role>` module naming (19 closed roles) and banned words.
- `AgenticInstructions/SKILL-Formatting.md` — 142/122-column banners, `[unit]` alignment, Unicode math, Allman style.
- `AgenticInstructions/SKILL-Emoji.md` — closed emoji whitelist and decision-table rating glyphs.
- `AgenticInstructions/SKILL-CompactOutputProtocol.md` — skimmer-first output; mandatory comparison table first.
- `AgenticInstructions/SKILL-SymbolIndex.md` — read `<Folder>/<Folder>.symbolindex` first; inspect narrow line spans.

Report any missing skill file before affected work. Never invent silent exceptions.

---

## Folder Ownership

Follow `Documentation/FolderStructure.md`. Never invent unapproved top-level directories or empty placeholder trees.

| Location               | Responsibility                                                                              |
|------------------------|---------------------------------------------------------------------------------------------|
| `Engine/`              | Reusable native C++20 engine subsystems (`<Subject><Role>` units, L0–L6 layered).           |
| `UI/`                  | Reusable presentation (`CardPanel/`, `ControlPanel/`, `MenuPanel/`) and `EditorPanel/`.     |
| `Projects/`            | Standalone executable targets (`Project-Zero/`, etc.) consuming `Engine/` and `UI/`.        |
| `EngineContent/`       | Runtime content (`Icons/`, `FontArchives/`, `MaterialArchives/`, `EnvironmentArchives/`).   |
| `Exhibits/`            | Retained validation exhibits (`Exhibits/FrontierEngine/`), benchmarks, and proof images.    |
| `Experimental/`        | Isolated prototypes and research experiments. Never linked into `Engine/`.                  |
| `Automation/`          | PowerShell and Python build, shader, font, and dependency orchestration scripts.            |
| `ExternalPackages/`    | Git-ignored third-party trees pinned via `Dependencies.lock.json` (never edit directly).    |
| `Documentation/`       | `FolderStructure.md`, `Tracker.md`, `Rules/`, and `Plans/{Ongoing,Completed}/`.             |
| `AgenticInstructions/` | Authoritative `SKILL-*.md` files only — never place plans, reports, or notes here.          |
| `_AgentScratch/`       | Git-ignored disposable workspace (`logs/`, `build/`, `tmp/`). Never commit deliverables.    |

- Write plans, summaries, and reports in chat unless explicitly instructed to create a file.
- Maintain a single `Documentation/Tracker.md` when tracking milestones — never create competing per-agent logs.

---

## C++20, Rendering & Runtime Architecture Rules

- **Language & ABI:** C++20, `/MD` across Windows configurations, `FRONTIER_DEBUG` for debug builds (`_DEBUG` never).
- **Rendering & Windowing:** **Vulkan** is the primary rendering API; **Slang (`.slang`)** is the primary shader
  language; **GLFW** (no-OpenGL context) owns platform windows and input; **ThorVG** rasterizes vector icons.
- **ImGui Multi-Band Composition:** Preserve `ImGui_ImplVulkan_RenderDrawDataBand(draw_data, cmd, band_mode, pipeline)`
  (`0` = all, `1` = beneath/window grounds, `2` = above/drawers, popups, context menus) so 3D viewport CAD/gizmo passes
  composite cleanly between window grounds and foreground chrome without vertex reallocation.
- **Typed Identities:** Every identity uses `Identity<Subject>` with distinct subject tags — no implicit conversions.
- **Fallible Calls:** Absence or rejection carries a reason via `Deliver<Content>` with a `Refusal` — never
  `std::optional` where a failure reason exists, and never exceptions across unit seams.
- **Iterative Solvers:** Convergent computations return `ConvergentResult<T>`, never a bare iterate masking failure.
- **Precision & Compile-Time Gates:** Prefer `constexpr` and `static_assert` wherever a contract is static. Exported
  numerical kernels declare their precision guarantees explicitly (`FRONTIER_DECLARES_PRECISION`).
- **Memory Ownership:** No raw `new`/`delete` outside dedicated extent allocators (`ByteSpace` / `Slot` spaces).
- **Content & Serialization:** Use `ContentDirectory` and `EngineContent/` (never `AssetDirectory` or `UI/Assets/`).
  Resolve bundled content relative to the executable path first. All engine-authored configuration and asset descriptors
  use **TOML (`toml++`)**; JSON is restricted to dependency lockfiles and automation interchange.
- **Vendor Spellings:** External SDK identifiers remain verbatim (`VkBuffer`, `VkPipeline`, `ImDrawData`); internal code
  enforces `SKILL-Naming.md` strictly.

---

## Platforms, Tooling & Dependencies

- **Primary Deliverable:** Windows x64 binaries are the default binary target while preserving Windows, Linux, and macOS
  source portability. Never substitute a Linux binary when a Windows binary is requested.
- **Shell Discipline:** Execute `.bat` and `.ps1` build/shader drivers through **PowerShell** on Windows (`$env:VAR`,
  `$null`), never Bash path translation. Never hard-code developer machine paths.
- **Dependency Integrity:** Restore third-party libraries into `ExternalPackages/` from SHA-256-pinned archives. Never
  vendor duplicate copies, never delete all of `ExternalPackages/` to fix one package, and **never disable TLS**.
- **GitHub Actions & Remote Operations:** Never trigger GitHub Actions workflows, pushes, or pull requests without
  explicit user approval for that operation.

---

## Verification & Honest Evidence

Never present untested code as complete. Compilation alone is not runtime verification.

| Work Scope             | Minimum Required Verification                                                         |
|------------------------|---------------------------------------------------------------------------------------|
| Native C++ / Shaders   | Clean build + unit/validation execution exercising the changed path.                  |
| Editor UI / Exhibits   | Automated layout/interaction checks + actual rendered capture when visuals change.    |
| Defect Repairs         | Reproduce failure first, then prove the repaired behaviour passes.                    |
| Performance Claims     | Reproducible before/after measurements with hardware, resolution, and scene stated.   |

- Retain permanent validation exhibits and actual captures together under `Exhibits/<ExhibitName>/`.
- Never use mockups or fabricated numbers as proof. Report unrun or environment-blocked checks explicitly as
  **blocked / not run**, never passing.
