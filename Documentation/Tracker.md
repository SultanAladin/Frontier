# Frontier Engineering Tracker

Single authoritative roadmap and milestone status board for **Frontier**.
Deferred items live in [Backlog.md](Backlog.md); architectural invariants live in
[ImportantNotes.md](ImportantNotes.md).

---

## Milestone Overview

| Phase | Milestone | Status | Primary Domains |
| ----- | --------- | ------ | --------------- |
| **Phase 0** | Governance, Pinned Dependencies, ImGui Patch Stack & CMake Profiles | 🟢 Complete | `Documentation/` · `Automation/` · `ExternalPackages/` · `Engine/Configuration/` |
| **Phase 1** | **Input Foundation & Cross-Platform Windowing** | 🟢 Groundwork Active | `Engine/Input/` · `Engine/Windowing/` · `Exhibits/` |
| **Phase 2** | **Editor & Multi-Document Workspace Groundwork** | 🚩 Next | `Engine/Workspaces/` · `Engine/Editor/` · `Engine/Components/` |
| **Phase 3** | **`.slang` Shader Compiler, GPU Device Interface & GPU Viewport Overlays** | 🏳️ Planned | `Engine/GeometricRaster/` · `Engine/Shaders/` · `Engine/Annotation/` · `Engine/Editor/Viewports/` |
| **Phase 4** | **Geometry Processing, UV Space, Color Space & Asset Interchange** | 🏳️ Planned | `Engine/MetricSpace/` · `Engine/GeometryProcessing/` · `Engine/UVSpace/` · `Engine/ColorSpace/` · `Engine/Interchange/` |
| **Phase 5** | **Simulation, Audio & Networking Spaces** | 🏳️ Planned | `Engine/SimulationSpace/` · `Engine/AudioSpace/` · `Engine/NetworkingSpace/` |

---

## Phase 0 — Governance, Dependencies & Build Architecture (🟢 Complete)

- ✔️ Canonical layout established in [FolderStructure.md](FolderStructure.md).
- ✔️ Enforced naming blacklist & rulings established in [BannedWordsVocabulary.md](BannedWordsVocabulary.md).
- ✔️ Agentic skill protocols imported and harmonized in `Documentation/AgenticInstructions/`.
- ✔️ Modern C++20 modular coding standards established in [CodingStandards.md](CodingStandards.md).
- ✔️ Third-party packages (`imgui`, `thorvg`, `tomlpp`, `glfw`, `vulkan-headers`, `volk`, `stb`, `JoltPhysics`), `.slang` compiler (`slangc`), `SwiftShader`, and OFL font archives pinned and verified via `Automation/`.
- ✔️ All 8 ImGui patches (`PatchA`–`PatchH`) applied and verified against `ExternalPackages/imgui`.
- ✔️ Three-profile build architecture (`Editor` / `Development` / `Shipping`) wired in `CMakeLists.txt` and `Engine/Configuration/BuildConfiguration.h`.

---

## Phase 1 — Input Foundation & Cross-Platform Windowing (🟢 Groundwork Active)

Starting focus agreed with user: **`Input/` + `Editor/` groundwork**.

1. **`Engine/Configuration/BuildConfiguration.h`**:
   - Compile-time mutually exclusive `FRONTIER_PROFILE_EDITOR`, `FRONTIER_PROFILE_DEVELOPMENT`, and `FRONTIER_PROFILE_SHIPPING` policy with `static_assert` validation and `constexpr` booleans.
2. **`Engine/Input/InputExchange.{h,cpp}` & `Engine/Input/InputExtension.{h,cpp}`**:
   - Platform-neutral C++20 input signal packets covering keyboard scancodes, pointer motion/buttons/scroll, gamepad axes/buttons, and multi-touch/pen contact points (supporting Windows, Linux, and Android).
   - Bi-directional fixed-capacity deterministic exchange (`InputExchange`) covering both OS→Engine signal intake (`InputSignalSink`) and Engine→OS cursor confinement & haptic rumble directives (`InputFeedbackTarget`) with zero heap churn per tick.
   - Lifecycle coordination via `InputExtension` (`Initialize` → `TickExchange` / `FlushOutboundDirectives` → `Finalize`).
3. **`Engine/Windowing/PlatformWindow.h`, `PlatformWindowDesktop.cpp` & `PlatformWindowStub.cpp`**:
   - Composable `WindowStyleFlags` (`DecoratedFrame`, `ResizableBorder`, `CenteredOnMonitor`, `FloatingTopmost`, `AutoMinimizeFocus`, `HighDpiScale`, `TransparentClient`, `AcceptFileDrops`, `StartVisible`, `FocusOnShow`), `WindowPresentationMode` (`SplashBorderless`, `Windowed`, `MaximizedWindowed`, `BorderlessFullscreen`, `ExclusiveFullscreen`), application presets (`MakeSplashWindow`, `MakeApplicationWindow`, `MakeGameWindow`), and dynamic runtime transitions restoring saved windowed bounds across GLFW 3.4 (`Win32`, `X11`, `Wayland`, `GlfwNull`) and `HeadlessPlatformWindow`.
4. **Validation (`Exhibits/InputValidation.cpp` & `Exhibits/WindowValidation.cpp`)**:
   - Deterministic CTest verification across `Editor`, `Development`, and `Shipping` profiles covering inbound peripheral/multi-touch intake, outbound cursor-lock / haptic directive delivery, splash/windowed/maximized creation, and dynamic game fullscreen transitions.
5. **Reference Editor UIs & Studio Prototypes (`Experimental/`)**:
   - Staged `FrontierEditor` (`c0aaab6`), `ProjectZeroEditor` (`186a7b8`), and 8 domain studios (`FluidEditor`, `FractureEditor`, `ParticleEditor`, `PhotometricEditor`, `ForceFieldEditor`, `VehicleLab`, `UVEditor`, `TextureStudio`) under `Experimental/` as UI component & solver specifications for `Engine/Components/`, `Engine/Workspaces/`, and `Engine/Editor/`.

---

## Phase 2 — Editor & Multi-Document Workspace Groundwork (🚩 Next)

1. **`Engine/Workspaces/`**:
   - `WorkspaceToken.h`, `WorkspaceDocument.{h,cpp}`, `WorkspaceRegistry.{h,cpp}`, `WorkspaceExtension.{h,cpp}`, and `Revision/` DAG history (`ContentRegion`, `DeltaIntake`, `NonLinearHistory`).
2. **`Engine/Editor/` (Compiled & Linked ONLY When `FRONTIER_EDITOR_ENABLED == 1`)**:
   - `FrontierEditor` static library target strictly excluded from `Shipping` builds.
   - Patched ImGui (`PatchA`–`PatchH`) workspace tab bar & docking host integration (`ProjectBrowser`, `SceneOutliner`, `PropertyInspectorHost`, `DebugConsole`).
   - Viewport host shell ready for 100% GPU `.slang` grid, gizmo, and debug-line rasterization (`ProjectionViewport`, `TransformGizmo`, `NavigationOverlay`).
