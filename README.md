# Frontier

Cross-platform native C++20 engine, general-purpose editor, and interactive rendering/physics exhibit suite.

## Overview

Frontier is organised as a clean-sheet layered C++20 / Vulkan / Slang engine (`Engine/`, `UI/`, `Projects/`)
paired with self-contained validation & reference exhibits under `Exhibits/` — starting with `Exhibits/FrontierEngine/`
(WebGPU ReSTIR path-traced & SDF-GI viewport, Project-Zero editor chrome, Studio Lobby & Proving Grounds, Jolt + tyre
carcass dynamics, `GizmoPRO` 3D scene authoring, thin-lens camera optics, socket debug, and 3D array modifiers).

## Repository Layout

| Directory                  | Purpose                                                                                 |
|----------------------------|-----------------------------------------------------------------------------------------|
| `Engine/`                  | Reusable native C++20 engine modules (`<Subject><Role>` naming across L0–L6 layers)     |
| `UI/`                      | Reusable cards, controls, menus (`CardPanel/`, `ControlPanel/`, `MenuPanel/`) & editor  |
| `Projects/`                | Standalone native applications built on `Engine/` (`Project-Zero/`, etc.)               |
| `EngineContent/`           | Shared runtime icons (`Icons/`), OFL fonts (`FontArchives/`), materials, and defaults   |
| `Exhibits/FrontierEngine/`   | Baseline WebGPU engine & Project-Zero editor exhibit (Studio Lobby & Proving Grounds)   |
| `Exhibits/FrontierEngineV2/` | Same scene upgraded to **ReSTIR PT Enhanced** (Lin, Kettunen & Wyman 2026, `10.1145/3804494`) |
| `Experimental/ProjectZeroEditor/` | Standalone **Project-Zero Editor UI** prototype (HTML/React bundle, port `8083`)   |
| `Experimental/FrontierEditor/`    | Standalone **FrontierEditor UI** prototype (Vite + React, port `8084`)             |
| `Experimental/`            | Isolated research & UI prototypes (never linked into `Engine/`)                         |
| `Automation/`              | Build, shader compilation, font preparation, and dependency bootstrap scripts           |
| `ExternalPackages/`        | Pinned third-party dependencies (`Dependencies.lock.json`, git-ignored source trees)    |
| `Documentation/`           | `FolderStructure.md`, engineering rules, and design plans                               |
| `AgenticInstructions/`     | Authoritative naming, formatting, emoji, compact-output, and symbol-index skills        |

## Quick Start — `Exhibits/FrontierEngine`

Serve `Exhibits/FrontierEngine/` over HTTP (requires **Node.js 20+** for dependencies and Dawn WebGPU headless tests):

```sh
npm --prefix Exhibits/FrontierEngine install
python3 -m http.server 8080 --directory Exhibits/FrontierEngine
```

Open `http://localhost:8080/` for **Proving Grounds** or `http://localhost:8080/?lobby=1` for **Lobby · Studio**.

Run the automated test and Dawn headless validation suite:

```sh
npm --prefix Exhibits/FrontierEngine/Tools install
npm --prefix Exhibits/FrontierEngine/Tools test
npm --prefix Exhibits/FrontierEngine/Tools run validate
```

## Native Engine & Editor Architecture

- **Graphics & Compute:** Vulkan + Slang (`.slang`) shaders
- **Windowing & Vector UI:** GLFW + Dear ImGui (multi-band viewport/chrome composition) + ThorVG SVG rasterization
- **Serialization:** Native TOML (`toml++`) descriptors and workspace configurations
- **Conventions:** Strict `<Subject><Role>` module taxonomy — see `CLAUDE.md` and `Documentation/FolderStructure.md`
