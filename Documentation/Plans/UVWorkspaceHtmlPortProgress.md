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

## Remaining HTML-reference parity work

The rendering path is no longer black, but this does not complete a 1:1 UI port. Continue comparing against `Experimental/UVEditor/` rather than inventing partial controls. The known gaps are:

- Match the HTML shading-mode catalog and mode-dependent Inspector content; the C++ renderer currently exposes a smaller set of actual surface display modes and should not display unsupported options as if implemented.
- Design the lifecycle for application-owned viewport texture dimension changes. Same-size pixel refreshes now use `WantUpdates`, but the upstream backend requires old GPU texture records to retire before a size-changing replacement is created; avoid resetting `TexID`/`BackendUserData` without releasing their backend resources.
- Bring selection shortcuts and viewport selection feedback in line with the HTML selection-mode semantics.
- Finish Inspector/Outliner scrolling, shared reusable tooltips, and the HTML context-menu interactions.
- Match camera damping and full-state undo/redo behavior, then add focused validation for interactions rather than relying only on screenshots.
