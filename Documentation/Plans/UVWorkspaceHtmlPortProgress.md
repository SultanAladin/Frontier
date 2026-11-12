# UV Editor HTML-to-C++ Port: Rendering Fixes and Parity Follow-up

**Branch:** `arena/4c9015d5-frontier`  
**Reference:** `Experimental/UVEditor/`  
**State:** ImGui black-screen fixes integrated and exercised locally; the broader UI port remains in progress.

## Implemented in this pass

- Built the Dear ImGui Vulkan backend from the repository-pinned ImGui checkout and configured it to use Frontier's existing volk loader.
- Routed draw-data rendering through `ImGui_ImplVulkan_RenderDrawData` while retaining Frontier's swapchain/offscreen targets, render pass, synchronization, and PNG readback path. The upstream backend now owns ImGui command decoding, clipping, index offsets, pipeline state, and font-atlas texture lifecycle.
- Added the backend's frame preparation call before `ImGui::NewFrame()` and texture-request servicing before presentation.
- Fixed application-owned `ImTextureData` uploads. Viewport and content-thumbnail textures are referenced from draw commands but are not necessarily members of ImGui's platform texture list, so the presenter explicitly services texture records referenced by draw commands. It also recovers the legacy invalid-ID/`OK` state by issuing a create request before use.
- Changed viewport framebuffer copies to request full-region updates for existing textures instead of falsely marking newly copied CPU pixels as GPU-resident. Thumbnail atlas writes now accumulate a dirty rectangle and request a backend update.
- Kept upstream backend shutdown ahead of render-pass/device-resource teardown.

## Porting guidance for the shared-code repository

1. Compile `ExternalPackages/imgui/backends/imgui_impl_vulkan.cpp` against the same pinned ImGui revision as `imgui.cpp`; enable `IMGUI_IMPL_VULKAN_USE_VOLK` for that translation unit and provide the pinned Vulkan-Headers and volk include paths.
2. Create the ImGui Vulkan renderer only after the target render pass exists. Call `ImGui_ImplVulkan_NewFrame()` immediately before `ImGui::NewFrame()`, and shut the renderer down before destroying its render pass or Vulkan device.
3. For `ImTextureData`, `SetStatus(ImTextureStatus_OK)` means a GPU texture and valid `TexID` already exist. CPU-side pixel writes must remain `WantCreate` or become `WantUpdates` (with `UpdateRect` populated); do not mark pixels `OK` just because they were copied into the CPU buffer.
4. Process application-owned texture records referenced by `ImDrawCmd::TexRef._TexData` as well as `ImDrawData::Textures`. The latter is not guaranteed to enumerate arbitrary textures owned by workspace panels.
5. Validate by rendering a real workspace and checking its image pixels/content. Shader compilation and successful swapchain creation alone do not establish that the ImGui UI is visible.

## Validation performed

- `node Experimental/UVEditor/UVValidation.mjs` — passed.
- `cmake -S . -B build/Development -DFRONTIER_PROFILE=Development -DBUILD_TESTING=ON` — configured successfully.
- `cmake --build build/Development --target FrontierUV -j2` — passed, including compilation/linking of `imgui_impl_vulkan.cpp`.
- `ctest --test-dir build/Development --output-on-failure -R '^(UVRasterPickingGizmoValidation|UVWorkspaceUIValidation|UVParameterizationValidation|UVInteractiveAuthoringValidation|UVBoundaryValidation)$'` — 5/5 passed.
- Headless SwiftShader run: `env -u DISPLAY -u WAYLAND_DISPLAY ./build/Development/FrontierUV --frames=3 --capture-png=Exhibits/Images/UV/05-SwiftShader_LiveWorkspace.png` — exited successfully and captured a 1840×980 live workspace with populated UV and 3D viewports, Inspector, and Outliner. SwiftShader emitted unsupported-capability 4427 warnings, but the process completed and the captured image was visibly rendered.

## Inspector, Outliner, and viewport control reuse

The UV Inspector and Outliner have content-aware scrolling, reusable control tooltips, layer tint choices, UDIM-filtered face counts and remainder rows, Escape dismissal, and selection-aware Inspector headings. Inspector dropdown sheets use their live rendered button bounds. The Outliner UDIM filter now uses the shared `DropdownSelector`; the viewport projection selector uses it as well and applies the chosen `SpatialProjectionPreset`. The viewport split continues to use the shared `ViewportProjectionPane`. Duplicate cube and pack-grid drawings were removed in favor of existing `VectorIconRepository` icons. The configured pack rotation steps, including 15 degrees, are honored. The Auto-Spill scope is routed through the session's common Pack directive so Inspector and viewport Pack actions share behavior.

**Rendered exhibit:** `Exhibits/Images/UV/06-SharedControls_InspectorOutlinerViewport.png` (1840×980). Captured from the built `FrontierUV` executable with SwiftShader; it shows the Outliner filter dropdown, split UV/3D viewport, shared projection dropdown, and Inspector. SwiftShader prints unsupported-capability 4427 warnings, but rendering exits successfully.

## Remaining phases

1. **Packing-scope behavior (next):** implement Active UDIM as an actual target tile, Per Assigned UDIM routing, and Selected Islands Only. Auto-Spill is wired; these other scope choices are still persisted in preferences but not honored by the Pack operation yet.
2. **Selection and menus:** finish HTML selection shortcut/feedback parity and context-menu interactions; add focused interaction validation for Outliner filtering, Inspector dropdowns, and viewport projection selection.
3. **Shading and camera parity:** match the HTML shading-mode catalog and mode-dependent Inspector content; align camera damping and full-state undo/redo.
4. **Resize-safe ImGui textures:** retire old application-owned GPU texture records before size-changing replacements; same-size framebuffer updates already use the update path.
5. **Final verification:** run the complete UV validation set, inspect a current-state capture, and update exhibits only for intentional visual changes.

Keep comparing against `Experimental/UVEditor/` and reuse existing controls/icons wherever available; do not invent duplicate icon assets or controls.
