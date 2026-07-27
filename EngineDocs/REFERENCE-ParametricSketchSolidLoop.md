# Reference — Wiring the ParametricSketch Solid Sequence Into a Frame Loop

🧩 How a host application drives the ported chrome-matcap solid preview (`ParametricSketchSolidSequence`
+ the `Render/Surface/ParametricSketch*` trio) each frame. This is a **reference**, not a copyable
`main()` — the retired standalone entry point (`CadMain.cpp` → `FrontierCad.exe`) is described here so a
host can reproduce its four-call integration against the destination `VulkanHost` API. The retired
`CadMain.cpp` itself is deliberately **not** ported (it wired ~40 editor subsystems + a
`FRONTIER_DRAUGHTING_ONLY` build split that no longer applies).

Source of truth for the loop: `RetiredProject/Engine/Internal/EngineInfrastructure/Platform/Application/CadMain.cpp`
(read-only reference; do not compile).

---

## What the sequence owns (already ported)

| Component | Home | Role |
|---|---|---|
| `ParametricSketchMatcapTexture` | `Internal/Graphics/Render/Surface/` | chrome matcap disc image + sampler |
| `ParametricSketchViewTarget` | `Internal/Graphics/Render/Surface/` | offscreen colour+depth, sampled by ImGui |
| `ParametricSketchSurfaceInscription` | `Internal/Graphics/Render/Surface/` | two-set matcap pipeline (set 0 camera UBO, set 1 matcap sampler) |
| `ParametricSketchSolidSequence` | `Internal/Graphics/Render/Surface/` | the consumer binding the three above + resident geometry |
| matcap shaders | `Internal/Graphics/Render/Surface/Shaders/ParametricSketchMatcap.{vert,frag}` | + committed `.spv` beside the sources |

The scene bridges the sequence reads/publishes live on the shape store:
`ParametricSketchSceneView`, `ParametricSketchShapeBody`, `ParametricSketchLoftBody`, and
`Register/RetrieveParametricSketchSceneView`, `Register/RetrieveParametricSketchSolidImage`,
`RetrieveParametricSketchShapeSource`, `RetrieveParametricSketchShapeBodies`
(`Internal/Authoring/ParametricAuthoring/ParametricSketchShapeStore.h`).

---

## Name translation from the retired reference

The retired loop was written against `DeviceAssembly` + `DraughtSolidScene`. The ported API rewires
onto `VulkanHost`. When reading `CadMain.cpp`, apply:

| Retired (`CadMain.cpp`) | Ported destination |
|---|---|
| `DeviceAssembly Device` | `VulkanHost Host` |
| `Device.LogicalDevice` | `Host.Device` |
| `Device.QueueFamilyIndex` | `Host.GraphicsQueueFamily` |
| `DraughtSolidScene Solid` | `ParametricSketchSolidSequence Solid` |
| `DraughtSceneView SceneView` | `ParametricSketchSceneView SceneView` |
| `InitializeDraughtSolidScene(Solid, Device, …)` | `InitializeParametricSketchSolidSequence(Solid, Host, …)` |
| `SynchronizeDraughtSolidScene(Solid, SceneView)` | `SynchronizeParametricSketchSolidSequence(Solid, SceneView)` |
| `RecordDraughtSolidScenePrePass(Solid, Cmd)` | `RecordParametricSketchSolidSequenceInto(Solid, Cmd)` |
| `PublishDraughtSolidImage(Solid)` | `PublishParametricSketchSolidImage(Solid)` |
| `FinalizeDraughtSolidScene(Solid)` | `FinalizeParametricSketchSolidSequence(Solid)` |
| `RegisterDraughtSceneView(…)` | `RegisterParametricSketchSceneView(…)` |
| `RetrieveDraughtSceneView()` | `RetrieveParametricSketchSceneView()` |

Matcap `.spv` names also change: `DraughtMatcap.{vert,frag}.spv` → `ParametricSketchMatcap.{vert,frag}.spv`.

---

## The four integration points

The host owns the frame loop and threads exactly four sequence calls. Their ordering is the whole
contract — everything else is ordinary window/Vulkan/ImGui bring-up the host already has.

### 1. Bring-up (once, after the ImGui Vulkan backend is live)

```
InitializeParametricSketchSolidSequence(Solid, Host,
                                        MatcapChromePath,      // asset-root-relative PNG
                                        MatcapVertSpvPath,     // ParametricSketchMatcap.vert.spv, beside the exe
                                        MatcapFragSpvPath,     // ParametricSketchMatcap.frag.spv, beside the exe
                                        PreviewTargetExtent);  // e.g. 768 — initial square target edge
```

The target's colour image registers with the ImGui Vulkan backend, so this MUST run after that backend
is up. A failure disables the sequence only (`Enabled == false`); every call below then no-ops and the
host runs as the pure-2D sketch.

### 2. Publish LAST frame's image, before the panel paint

```
RegisterParametricSketchSceneView(ParametricSketchSceneView{});  // reset to not-ready
PublishParametricSketchSolidImage(Solid);                        // hand last frame's target to the view
… paint the dock host / panels …                                // the active view republishes its SceneView here
```

`PublishParametricSketchSolidImage` is **one frame late by construction** — this frame's pass records
*after* the paint (step 4). Invisible as the camera eases. The active view republishes its
`ParametricSketchSceneView` (camera matrices + canvas rect) during the paint; resetting it to
not-ready first means a frame where no view painted leaves the pass idle rather than stale.

### 3. Synchronize, AFTER the paint, OUTSIDE any render pass (device idle-safe)

```
SceneView = RetrieveParametricSketchSceneView();
SynchronizeParametricSketchSolidSequence(Solid, SceneView);
```

Sizes the target to the canvas rect (draining the device on a genuine resize), refreshes the matcap
camera from the settled matrices, and re-stages any loft/shape body whose revision advanced. The upload
submits its own one-shot transfer + fence, so it must sit outside the command buffer. A body being
dragged re-tessellates every frame — the re-stage drains the device first to guard the previous frame's
in-flight pre-pass draw against a use-after-free (`DEVICE_LOST`).

### 4. Record the pre-pass INSIDE encode, before the swapchain pass

```
// pre-pass recorder — after vkBeginCommandBuffer, BEFORE the swapchain pass:
RecordParametricSketchSolidSequenceInto(Solid, CommandBuffer);
```

Begins the target's render pass (clear transparent), draws every resident loft then every resident shape
through the matcap pipeline (shared depth → self-occlusion), and ends it — leaving the colour image
`SHADER_READ_ONLY` so this frame's ImGui pass samples it. Thread it as the encoder's pre-pass recorder
(pass `nullptr` when `Solid.Enabled` is false).

### Teardown (once, at exit)

```
vkDeviceWaitIdle(Host.Device);                 // drain first
… FinalizeIconTextureCache / glyph caches …    // other ImGui backend textures
FinalizeParametricSketchSolidSequence(Solid);  // target descriptor is an ImGui backend texture — free before the backend shuts down
… FinalizeInterfaceDriver / swapchain / host … 
```

The target's colour descriptor is an ImGui backend texture, so finalize the sequence **before** the
ImGui Vulkan backend shuts down, and after the device is drained.

---

## Frame-order summary

```
loop:
    poll / resize swapchain
    begin ImGui frame
    RegisterParametricSketchSceneView({})          # reset bridge
    PublishParametricSketchSolidImage(Solid)       # (2) last frame's image → view
    paint dock host / panels                        #     active view republishes its SceneView
    SceneView = RetrieveParametricSketchSceneView()
    SynchronizeParametricSketchSolidSequence(...)  # (3) size target, refresh camera, stage geometry
    encode:
        RecordParametricSketchSolidSequenceInto(...)   # (4) offscreen matcap pass (pre-pass)
        record ImGui overlay                            #     samples the target from (4) via ImGui::Image
    present
```

---

## Assets the host must supply

- **Chrome matcap PNG** — asset-root-relative (retired path:
  `EngineContent/ReferenceMaterials/Matcaps/Matcap-Chrome.png`). Decoded by
  `ParametricSketchMatcapTexture` via stb_image.
- **Compiled matcap `.spv`** — `ParametricSketchMatcap.vert.spv` / `.frag.spv`. The sources live at
  `Internal/Graphics/Render/Surface/Shaders/`; compile with the Vulkan SDK's `glslangValidator -V`
  (the committed `.spv` were produced that way) and place the `.spv` where the sequence's init paths
  resolve (the retired build emitted them beside the exe). Note: `Automation/ShaderCompile.ps1` is
  listed in `FolderStructure.md` as planned but does not exist yet — shader compilation is currently a
  manual `glslangValidator` step.

## Units note

`ParametricSketchLoftBody` / `ParametricSketchShapeBody` positions are **mm**; `RenderVertex.Position`
is **cm**. The sequence applies the shared `0.1` mm→cm factor on upload so the GPU solid registers 1:1
with the CPU sketch (which applies the same factor). Do not double-apply it in the host.
