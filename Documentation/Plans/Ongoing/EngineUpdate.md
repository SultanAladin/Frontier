# EngineUpdate

## Objective

Establish agent rules and folder ownership, then audit and repair inherited violations with evidence.
Approved source: `SultanAladin/Frontier-` commit `338293863d7df4e585dec0c4acae45fe9c524ed5`.
The session checkout is a different repository. Only the selected UI foundation files have been imported.

## Current authorised scope

The user authorised selective engine/UI source porting after the documentation phase.
Include existing ImGui GPU UI drawing, all six patches, SVG artwork, and a lifecycle shared by UI and scene code.
Do not import or implement the scene renderer. Leave `Engine/Render/` blank apart from its Git retention marker.
Proof image generation and local checks are authorised. The earlier batch was committed/pushed;
no further commit, push or GitHub Actions is authorised for the current UI and format-review work.
Windows remains the delivery target; local host verification tools are not Linux product deliverables.

First batch: interface foundations, lifecycle contract, UI-only Windows exhibit, dependency setup, and CPU evidence.
Full engine migration, whole-editor integration, compliance repair, and Windows runtime verification remain ongoing.

## Sequence

| Step | Work                                                     | Completion condition                  |
|------|----------------------------------------------------------|---------------------------------------|
| 1    | Establish documentation and agent rules                  | Consistency checks and review.        |
| 2    | Inventory linked baseline and map safe reconciliation     | Explicit path/build dependency map.   |
| 3    | Audit naming, formatting, ownership, duplication, evidence| Findings in the single tracker.       |
| 4    | Review exact source moves and build/launcher changes     | Approved migration map.               |
| 5    | Repair small, reviewable batches                         | Relevant tests and retained evidence. |
| 6    | Deliver verified Windows binaries when in scope          | Build and runtime status reported.    |

Do not replace the checkout wholesale or rewrite the entire engine without a reviewed scope.
No Linux binary delivery unless requested. No macOS setup now. No GitHub Actions without permission.
Keep this plan ongoing until work and evidence support completion, then move it to `../Completed/`.

## Control Centre batch

Ported the theme/typography layouts and font discovery, excluding renderer and GI settings.
Added the quick panel, stable font catalogue, licensed font recovery, and persistent applied preferences.
Nine Debug and sanitizer tests pass. A Windows x64 Release binary cross-builds; runtime/GPU checks remain pending.
See `Exhibits/ControlCentre/README.md` for commands and evidence. No Actions are authorised.

Corrected the drawer to the archived trapezoid/gesture reference, migrated content into EngineContent,
and integrated GLFW. Native font discovery reads TOML metadata and uses Unicode-capable file access.

## Content Browser batch

Extracted PanelExpansionTab for shared top/bottom geometry, drawing and gestures, preserving top-drawer tests.
Extended ContentDirectory with root-confined read-only enumeration, navigation/history, selection and filtering.
Ported the pinned React browser interaction concepts to native C++, using supported real files and shared controls.
See `Exhibits/ContentBrowser/README.md` for scope differences, reproduction and native evidence.
Inherited Applications/Executables/Internal stay pending dependency review; UI/Editor retains editor ownership.

## Shared panel hosting — approved ongoing sequence

Reuse the existing patched ImGui reference docking logic, not the inherited competing custom partition model.
Keep one content instance and its edits while moving between docked and detached hosts.
Normal OS windows are optional and default disabled, with the preference in Control Centre settings.

| Phase | Scope                                            | Current status                              |
|-------|--------------------------------------------------|---------------------------------------------|
| 1     | Shared content/hosting contract                   | Implemented; original browser/stream reused. |
| 2     | Real docking tabs, reorder and splits             | Native tabs/pointer reorder; builder split tested. |
| 3     | Optional native OS windows and dock-back          | GLFW/DX11 exhibit wired; Windows execution pending. |
| 4     | Preference and embedded layout persistence        | Personal preference and bounded TOML layout saved. |
| 5     | Monitor/DPI/offscreen/shutdown recovery            | Recovery geometry/callbacks checked; physical runtime pending. |

The shared registrations live in Engine; editor content adaptation lives in UI/Editor; evidence lives in Exhibits.
Content Browser and Notification Stream are real existing content, not invented tools.
Content Browser defaults to a draggable drawer; Workspace selects its optional dockable presentation.
Control Centre remains a drawer. Switching browser hosting preserves the same instance and browsing choices.
Correction G fixes adjacent/centred native `+` geometry; all trapezoidal tabs use opaque darkest theme surfaces.
Vulkan UI display integration is now authorised; scene renderer migration remains excluded. DX11 stays exhibit-only.
Do not equate preference persistence with layout persistence, headless builder operations with drag/split gestures,
or cross-compilation with Windows/multi-monitor execution. See `../../../Exhibits/Docking/README.md`.

## Workspace layout continuation — verified locally

Frontier owns `workspace.layout` format 1 inside personal `.projectspace`; native DockBuilder restores the snapshot.
Open/closed panels, tab order/selection, split ratios, floating groups/geometry and browser drawer visibility survive
fresh ImGui contexts. No raw INI is loaded and `IniFilename` stays null. Existing content instances are reused.
Autosave waits for stable geometry and released pointer/modal interaction; shutdown flush occurs before teardown.
Optional malformed layouts leave preferences usable, fall back to defaults and block replacement until explicit Reset.
Latest-container merging preserves arbitrary sections, unapplied drafts and override flags. Failed atomic writes retain
previous bytes and can retry. Layout-only personal containers are supported; legacy paths remain session-only.
Debug and ASan/UBSan each pass 13/13 tests; Windows cross-build and package inspection pass. Fresh-context native CPU
captures are in `Exhibits/Docking/Images/RestoredTabs.png` and `RestoredSplit.png`.
Basic main-area clamping/unsupported-native reattachment is not full monitor/DPI/hotplug recovery. Real Windows
OS-window/GPU/multi-monitor runtime remains pending. Vulkan display integration follows in the continuation below.

## Approved editor continuation — 2026-10-17

The user authorised these milestones in order, excluding Content Browser preview work:

| Milestone | Scope                                         | Status                                      |
|-----------|-----------------------------------------------|---------------------------------------------|
| 1         | Vulkan UI and detached-window display         | Implemented; cross-build, not GPU execution. |
| 2         | Monitor/work-area/DPI recovery                 | Implemented; geometry/callback checks.       |
| 3         | Named workspace presets in personal container | Pending implementation and native controls.  |
| 4         | Genuine notification action callbacks         | Pending operation routing and controls.      |
| 5         | General severe-error presentation             | Native fallback started; reference UI pending.|
| 6         | Configurable keyboard workspace navigation    | Pending input schema/commands and UI.         |
| 7         | Remaining Space decisions/next approved codec | Review decisions first; no bulk migration.   |
| 8         | Project Browser                               | After the preceding Space-format milestone.  |

VulkanPresentation uses the pinned ImGui Vulkan adapter and its secondary-window lifecycle with GLFW-owned OS windows.
FrontierUI defaults to Vulkan; FrontierUIDX11 preserves the isolated DX11 comparison. No scene renderer or authored
shader migration is included. The inherited adapter supplies its own embedded UI shaders; Slang remains primary for
future authored rendering work. Vulkan-Headers is checksummed; no Vulkan SDK or loader binary is bundled.

MonitorPlacement is renderer/platform-independent. Native layout rectangles carry optional work-area, density and
absolute-window-origin metadata; old format-1 layouts without it still load. Restore reconciles density and clamps to
current work areas. Before NewFrame, live topology/work-area changes invoke existing GLFW platform callbacks and
native move/resize request markers; ImGui performs live DPI scaling, avoiding a second density multiplication.
Minimized secondary windows defer recovery until restored. No destructive layout migration or new INI sidecar exists.

The native error-dialog fallback covers caught startup/display failures regardless of API. It is not completion of the
supplied full-screen severe-error design or a guarantee after an OS/driver hard hang. Real GPU/Windows/multi-monitor
execution remains unverified. Presets, notification actions, full severe-error UI, workspace shortcuts, Space codec
implementation and Project Browser must not be reported as completed by this first continuation batch.
