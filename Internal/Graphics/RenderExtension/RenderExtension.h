/*==============================================================================================================================================
                                                              RENDEREXTENSION.H
==============================================================================================================================================*/
// 🧩 The modular render coordinator. Stands on the shared WindowSubstrate (window + Vulkan host + swapchain + present) and adds the two things
//    a from-scratch visibility-buffer renderer needs from step one: a HardwareFeatureProfile probe (does this GPU meet the Pascal / GTX-1060
//    floor and expose the int64 image atomics the software micro-raster will need) and the single Initialize / Synthesize / Finalize seam any
//    host application drives it through. Phase 0.1 only clears and presents; later phases record their passes into SynthesizeOutputSequence.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDEREXTENSION_RENDEREXTENSION_H
#define FRONTIER_GRAPHICS_RENDEREXTENSION_RENDEREXTENSION_H

#include "Graphics/RenderExtension/Device/WindowSubstrate.h"
#include "Graphics/RenderSchedule/RenderSchedule.h"
#include "Graphics/Visibility/VisibilityDepth.h"
#include "Graphics/Visibility/VisibilityImage.h"
#include "Graphics/Visibility/VisibilityRasterization.h"
#include "Graphics/Visibility/VisibilityInscription.h"
#include "Graphics/Render/Radiance/RadianceResolveInscription.h"
#include "Graphics/Render/Radiance/RadianceTarget.h"
#include "Graphics/Visibility/SurfaceShadeInscription.h"
#ifdef FRONTIER_POLYGON_AUTHORING
#include "Graphics/Visibility/ComponentOverlayInscription.h"
#include "Graphics/Visibility/ObjectPickReadback.h"
#include "Graphics/Visibility/SelectionOutlineInscription.h"
#endif
#include "Graphics/Visibility/InstanceCullSubmission.h"
#include "Graphics/Visibility/SoftwareRasterization.h"
#include "Graphics/Scene/SuzanneScene.h"
#include "Graphics/Scene/WorkspaceDocumentDecoder.h"
#include "Graphics/Render/Resources/BufferAllocation.h"
#include "EngineContext/Scene/SceneExtension.h"
#include "EngineContext/Scene/WorkspaceDocumentRegister.h"
#include "Graphics/HierarchicalDepth/HierarchicalDepthPyramid.h"
#include "Graphics/Grid/GroundGridPass.h"
#include "Graphics/Clipmap/ClipmapFieldInspection.h"
#include "EngineContext/SpatialAcceleration/ToroidalClipmapField.h"
#include "EngineContext/SpatialAcceleration/TriangleCellOverlap.h"
#include "Graphics/Atmosphere/SkyAtmosphere.h"
#include "Graphics/Shadow/SunShadowClipmap.h"
#include "Graphics/Shadow/ShadowPageAtlas.h"
#include "Graphics/Shadow/ShadowTileStore.h"
#include "Graphics/Shadow/ShadowTileMarkingSubmission.h"
#include "Graphics/Shadow/ShadowPageClearSubmission.h"
#include "Graphics/Shadow/ShadowDepthRasterSubmission.h"
#include "EngineContext/Navigation/Camera/CameraConfiguration.h"
#include "EngineContext/Navigation/Camera/CameraNavigation/CameraNavigation.h"
#include "EngineContext/Navigation/Camera/CameraProjection/CameraViewMatrixSolver.h"
#include "EngineContext/Navigation/Camera/CameraProjection/ProjectionEvaluator.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The look-jitter mitigation strategy, cycled at run time (F1) so the three can be compared live against the same hand
//    motion. See the RenderExtension fields for what each one does and the state it keeps.
enum class LookFilterMode : uint8_t
{
    Raw = 0,           // No filter — the quantized count→angle baseline
    AnchorAccumulate,  // Blender-style: rotate toward the total drag offset from the press anchor
    VelocityLowpass,   // UE5-style: wall-clock exponential low-pass on look velocity

    ModeCount
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the selected GPU can do, inspected once after the Vulkan host is up. The two booleans gate the software micro-raster
//    (Phase 3): without int64 image atomics the atomicMax depth+id packing has no hardware path, and the baseline condition is
//    the overall "this GPU is at or above the GTX-1060 / Pascal floor" verdict the renderer refuses to fall below.
struct HardwareFeatureProfile
{
    char        DeviceName[256]      = { 0 };      // [-] - Reported physical-device name
    uint32_t    DriverApiVersion     = 0;          // [-] - Vulkan API version the device advertises
    bool        Int64AtomicsEnabled  = false;      // [-] - Shader int64 image/buffer atomics present (micro-raster path)
    bool        ArchitectureBaselineCondition = false; // [-] - Device meets the Pascal / GTX-1060 minimum tier
};

// 📝 The render coordinator itself. Owns the shared present machinery by reference-of-composition (the WindowSubstrate member)
//    plus the capability verdict. Deliberately thin at 0.1 — it holds nothing that identifies a single host, so any application
//    can construct one. Input is read straight off the window's InputPacket each frame (no separate input pipeline); the camera
//    is driven Unreal-style (RMB + WASD free-fly, scroll = fly speed) with a coexisting Alt-drag orbit / pan for DCC work.
struct RenderExtension
{
    WindowSubstrate         Substrate;             // [-] - Window + Vulkan host + swapchain + present loop
    HardwareFeatureProfile  FeatureProfile;        // [-] - GPU capability verdict from InspectHardwareFeatures

    GroundGridPass          GridPass;              // [-] - GPU ground-grid draw (lines + dots), built once
    SkyAtmospherePass       SkyPass;               // [-] - Hillaire 2020 sky/atmosphere, drawn behind the grid
    RenderSchedule          Schedule;              // [-] - Ordered spine of record steps; default-OFF (legacy inline path until flipped)
    VisibilityDepth         DepthTarget;           // [-] - Renderer-owned D32 scene depth (offscreen; written by the visibility raster, reduced by HiZ)
    VisibilityImage         VisibilityTarget;      // [-] - Renderer-owned R32_UINT visibility buffer (offscreen; the raster's id write target)
    VisibilityRasterization VisibilityRaster;      // [-] - Hardware visibility raster of the Suzanne scene into VisibilityTarget + DepthTarget
    InstanceCullSubmission  InstanceCull;          // [-] - GPU-driven per-instance two-pass cull; its survivor list + indirect arg drive the raster (P3)
    SoftwareRasterization   SoftwareRaster;        // [-] - Compute micro-raster (P4): atomicMax packed (depth|id) into the R64 target, then resolve into the same id+depth (Numpad-3 A/B against the hardware raster)
    VisibilityInscription   VisibilityResolve;     // [-] - Fullscreen composite of the visibility buffer to the swapchain (the on-screen A/B)

    // 📝 Linear HDR scene target + its resolve (P5.9b). The frame is TWO colour scopes, not one. The SCENE (sky + surface shade) renders into
    //    RadianceScene in unbounded linear light; the resolve tone maps that ONCE into the _SRGB swapchain; the display-referred overlays (grid,
    //    outline, component handles, clipmap lattice, id-hash A/B) then draw straight onto the swapchain so their authored colours survive.
    //    This closes three defects at once — the sky and the shade no longer tone map independently (the horizon can finally match), alpha-over
    //    now blends linear light rather than encoded output, and exposure becomes a real knob instead of a side effect of light intensity.
    //    It is also hard prerequisite B3 for the sun-shadow work: a shadowed surface needs somewhere to put >1.0 radiance.
    RadianceTarget             RadianceScene;                  // [-] - R16G16B16A16_SFLOAT linear scene target (offscreen; sky + shade write it)
    RadianceResolveInscription RadianceResolve;                // [-] - Tone maps RadianceScene into the swapchain; FIRST draw of the swapchain scope
    float                      SceneExposure       = 1.0f;     // [-] - Linear multiplier applied before the tone curve
    uint32_t                   RadianceOperator    = 0u;       // [-] - RadianceOperator* selector (0 = PBR Neutral, 1 = bypass); F5 A/B
    bool                       RadianceOperatorKeyLatch = false; // [-] - Edge latch so one F5 press flips the operator once

    // 📝 Deferred surface shade (the material slice). Reads the SAME id buffer the raster already wrote and reconstructs every shading input from it
    //    (partition -> instance -> MaterialId -> preset; primitive -> triangle -> barycentrics by ray intersection), so genuinely lit materials cost
    //    no extra geometry pass and no fat G-buffer. F4 toggles it; Numpad-5/6 walk the Composite record's lobe mask.
    SurfaceShadeInscription SurfaceShade;                     // [-] - The shade pass (fullscreen composite over the forward view)
    bool      SurfaceShadeEnabled     = true;                 // [-] - When true, the shade composites over sky+grid (F4 toggles); default ON = the materials are the point

    // 📝 P6.5: whether the shade traces the sun shadow atlas. Separate from SurfaceShadeEnabled so shadows can be toggled against an otherwise identical
    //    image — which is the only practical way to tell a shadow bug from a shading one, and what keeps the Phase-0 pixel-identity comparison reachable.
    // ⚠️ Necessary but NOT sufficient: the record path also requires ShadowAtlasBound, because b8 is aliased onto the visibility image when no atlas
    //    exists. This flag alone must never reach SunShadowEnabled.
    bool      SunShadowTraceEnabled   = true;                 // [-] - When true (and the atlas is really bound), sun-driven lobes are shadowed

    // 📝 P6.5 diagnostic view cycle (F6): Disabled -> ResolvedLevel -> DepthMargin -> Occlusion -> Disabled. Exists because a fully lit scene has three
    //    different causes that the shaded image cannot distinguish — see SunShadowDebugView for what each view proves.
    // ⚠️ Held as the enum's underlying type rather than the enum so the cycle is plain modular arithmetic; the cast happens once, at the call site.
    uint32_t  SunShadowDebugMode      = 0u;                   // [-] - SunShadowDebugView; 0 shades normally

    // 🧩 P6.6 SMRT (soft shadows), cycled at run time so the hard tap and the soft march can be compared against one identical view.
    // 🔴 THE ANGLE IS THE SUN'S ANGULAR RADIUS (HALF-ANGLE), NOT ITS DIAMETER. The physical sun subtends 0.526 deg ACROSS, so the radius is
    //    0.263 deg = 4.59e-3 rad — the value EEVEE's `shadow_angle` carries after eevee_light.cc halves the user-facing angle. Passing the diameter
    //    doubles every penumbra and reads as "SMRT is too soft" rather than as a unit error.
    // ⚠️ AT THE PHYSICAL ANGLE THE PENUMBRA IS SUB-TEXEL FOR NEAR CONTACT, and that is correct rather than a failed port: 4.59e-3 rad at 0.1 m of
    //    occluder distance is 0.92 mm against L0's 3.9 mm texel. Visible softening starts around 1 m (2.35 texels). To SEE the effect while porting,
    //    raise the angle 10-50x — that is a diagnostic, not a tuning default.
    // 📝 Ray/step defaults are EEVEE's own (1 ray, 6 steps). The ray count is what costs; the step count is nearly free by comparison because the
    //    quadratic distribution puts the samples where they matter.
    float     SoftShadowAngleRadians  = 4.59e-3f;             // [rad] - sun angular RADIUS; 0 disables SMRT
    uint32_t  SoftShadowRayCount      = 1u;                   // [-] - rays through the cone, host-clamped to 4
    uint32_t  SoftShadowStepCount     = 6u;                   // [-] - steps per ray, host-clamped to 16
    bool      SunShadowDebugKeyLatch  = false;                // [-] - Edge latch so one F6 press advances the view once
    bool      SoftShadowKeyLatch      = false;                // [-] - Edge latch so one F7 press advances the SMRT rung once
    uint32_t  SoftShadowRungIndex     = 2u;                   // [-] - index into the F7 ladder; 2 == PHYSICAL, matching the field defaults below

    // 🧩 P6.6 — Ctrl + drag moves the sun, so a penumbra can be watched sweeping instead of inferred from one still frame. A static image cannot
    //    distinguish "the soft path is running" from "the soft path is a no-op": both look like a hard shadow. Motion can.
    // ⚠️ CTRL, not Alt or Shift: DriveViewportCamera already claims Alt (orbit) and Shift (boost) and consumes PointerDelta unconditionally, so
    //    either of those would move the sun and the camera on one drag. Ctrl was unclaimed across all of Internal/Graphics.
    // 📝 Seeded from AtmosphereProfile's own 45 deg / 0 deg default so the first drag continues from the lit scene rather than snapping the sun.
    bool      SunDragKeyLatch         = false;                // [-] - Was Ctrl down last frame (edge detect for the notice only)
    float     SunElevationRadians     = 0.7853982f;           // [rad] - 45 deg, matching AssignFloat4's seeded SolarDirection
    float     SunAzimuthRadians       = 0.0f;                 // [rad] - 0 deg, ditto

    bool      SurfaceShadeKeyLatch    = false;                // [-] - Edge latch so one F4 press toggles the shade once
    uint32_t  CompositeFeatureMask    = 0u;                    // [-] - Live lobe mask for the Composite record only (0 = use the record's own); Numpad-5/6 cycle
    uint32_t  CompositeLobeCursor     = 0u;                    // [-] - Which lobe Numpad-6 toggles; Numpad-5 advances the cursor
    bool      CompositeLobeKeyLatch   = false;                // [-] - Edge latch so one Numpad-5 press advances the cursor once
    bool      CompositeToggleKeyLatch = false;                // [-] - Edge latch so one Numpad-6 press flips the selected lobe once

    // 📝 Object selection (P3a). Both units read the SAME id buffer the raster already writes, so selection costs no extra geometry pass: the readback
    //    copies one texel at the cursor, the outline composites a ring where the selected partition ordinal borders a different one. Because that
    //    buffer was written under hardware depth test, an occluder in front of the selected object owns those pixels and the ring is clipped by it
    //    automatically — the depth-correctness the "re-draw the silhouette" approach cannot achieve. Left-click selects, Escape clears, F3 toggles.
    //    Compiled out unless FRONTIER_POLYGON_AUTHORING is set: selection serves the modelling tools, so a runtime-only renderer carries none of it.
#ifdef FRONTIER_POLYGON_AUTHORING
    ObjectPickReadback          ObjectPick;                   // [-] - One-texel id copy-back ring (cursor -> partition ordinal)
    SelectionOutlineInscription SelectionOutline;             // [-] - Fullscreen ring composite over the selected partition's silhouette
    uint32_t  SelectedPartition       = NoSelectionSentinel;  // [-] - Committed selection (the outlined object); sentinel = nothing selected
    uint32_t  HoveredPartition        = NoSelectionSentinel;  // [-] - Partition under the cursor this frame; sentinel = empty pixel
    uint32_t  ReportedHoverPartition  = NoSelectionSentinel;  // [-] - Last hover ordinal actually logged, so the trace fires on CHANGE not per frame
    bool      ObjectSelectionEnabled  = true;                 // [-] - When true, picking runs and the outline composites (F3 toggles); default ON
    bool      ObjectSelectionKeyLatch = false;                // [-] - Edge latch so one F3 press toggles selection once
    bool      SelectPointerLatch      = false;                // [-] - Edge latch so one left-click commits one selection
    bool      SelectClearKeyLatch     = false;                // [-] - Edge latch so one Escape press clears the selection once
    int32_t   PickCursorX             = -1;                   // [px] - Cursor the pick copy is recorded at this frame (framebuffer space)
    int32_t   PickCursorY             = -1;                   // [px] - Paired cursor Y

    // 📝 Component (sub-object) selection. Same id buffer again, one step finer: the overlay reconstructs the triangle the id names and draws the
    //    vertex / edge / face handles of it, so the modes cost no geometry pass either. Numpad-7 cycles Object -> Vertex -> Edge -> Face; in Object mode
    //    the overlay records nothing and the outline above owns the frame. Hover is resolved INSIDE the shader from PickCursorX/Y (see the push-block
    //    note in ComponentOverlayInscription.h) — there is deliberately no HoveredComponent member here, because the host cannot compute one.
    ComponentOverlayInscription ComponentOverlay;             // [-] - Fullscreen handle composite (vertex dots / edge lines / face tints)
    ComponentSelectionMode ComponentMode = ComponentSelectionMode::Object; // [-] - Which component class the authoring tools address; Object = overlay off
    uint32_t  SelectedPrimitive        = NoSelectionSentinel; // [-] - Committed primitive (triangle) ordinal, for the component modes
    uint32_t  SelectedComponent        = NoSelectionSentinel; // [-] - Committed component key (mesh vertex index / edge key); sentinel = whole primitive
    bool      ComponentModeKeyLatch    = false;               // [-] - Edge latch so one Numpad-7 press advances the mode once
#endif // FRONTIER_POLYGON_AUTHORING
    PolygonBufferAllocation SceneGeometry;         // [-] - The uploaded Suzanne shared geometry (from the .wsdoc block) the raster instances (device-local)
    VisibilityRasterization FloorRaster;           // [-] - Second raster (own pipeline + instance set) for the checkered floor mesh, drawn into the SHARED visibility buffer
    PolygonBufferAllocation FloorGeometry;         // [-] - The uploaded floor slab geometry (from CheckerFloor.wsdoc block 0), device-local
    VkCommandPool           UploadPool = VK_NULL_HANDLE; // [-] - One-shot transfer pool for the geometry upload (freed at finalize)
    SuzanneSceneChoice      SceneChoice = SuzanneSceneChoice::MaterialRings; // [-] - Which saved .wsdoc scene the raster loads (MaterialRings = the 13 shaded heads the shade pass exists for)
    SceneExtension          SceneRegistry;         // [-] - The scene directory the loaded WorkspaceDocument registers into (one outliner row per head)
    bool                    VisibilityResolveEnabled = false; // [-] - When true, the resolve composites the id buffer over sky+grid (F2 toggles); default OFF = plain forward view
    bool                    VisibilityResolveKeyLatch = false; // [-] - Edge latch so one F2 press toggles the resolve once
    bool                    TopologyWireframeEnabled = false; // [-] - When true, the resolve wireframe follows authored ngon/quad/tri boundaries (Numpad-0 toggles); default OFF = per-triangle
    bool                    WireframeModeKeyLatch = false;    // [-] - Edge latch so one Numpad-0 press toggles the wireframe mode once
    bool                    VisibilityScalingEnabled = true;  // [-] - When true, the preamble runs the GPU-driven cull -> indirect raster (Numpad-2 toggles); default ON. OFF = plain instanced draw
    bool                    VisibilityScalingKeyLatch = false; // [-] - Edge latch so one Numpad-2 press toggles the cull path once
    bool                    SoftwareRasterEnabled    = false; // [-] - When true (and int64 atomics are available), the heads are rasterized by the compute micro-raster instead of the hardware raster (Numpad-3 toggles); default OFF
    bool                    SoftwareRasterKeyLatch   = false; // [-] - Edge latch so one Numpad-3 press toggles the software-raster path once
    uint32_t                InstanceCullRecordCount  = 0;     // [-] - live per-instance cull-record count (the early-pass lane bound), set at scene upload
    uint32_t                VisibilityResolveExtentWidth  = 0; // [px] - Extent the resolve descriptor was last refreshed against (re-Refresh on change)
    uint32_t                VisibilityResolveExtentHeight = 0; // [px] - Paired height for the refresh-on-resize guard
    HierarchicalDepthPyramid DepthPyramid;         // [-] - HiZ mip chain (max-reduce of DepthTarget); produced in the preamble, no cull consumer yet

    // 📝 Clipmap spine (P5c). The field itself is unconditional — it is the shared addressing primitive the sun-shadow clipmap (P6) and the GI
    //    irradiance probes (P7b) will both stand on, advanced from the camera every frame so its scroll/residency path runs live. Today nothing
    //    consumes its payload (the per-cell ramp is a STUB), so the only observer is the development-only GPU visualization below.
    ToroidalClipmapField    ClipmapField;          // [-] - camera-tracked 3D voxel clipmap; scrolled each frame, residency filled around the camera

    // 📝 Sun shadows (P6). Two units, deliberately separate: SunWindow is pure CPU tile math in LIGHT space (it rotates with the sun, which is why
    //    it is NOT another ClipmapField level), ShadowAtlas is the physical R32_UINT page pool the depth raster will write into.
    // ⚠️ The atlas is 24:1 over-subscribed by construction (256 pages against 6144 addressable tiles), so eviction is the steady state rather than
    //    an error path — the census is how a genuine shortfall is distinguished from healthy churn.
    SunShadowClipmap        SunWindow;             // [-] - light-space tile window, advanced from the cached observer at the top of the preamble
    ShadowPageAtlas         ShadowAtlas;           // [-] - 4096² page pool backing the sun window's resident tiles
    bool                    ReportedPageShortfall = false; // [-] - latch so the over-subscription warning states its onset once, not per frame

    // 📝 The marking chain (P6.3c): the virtual tile table plus the three GPU passes that raise bits in it. TileStore is the SSBO; TileMarking owns
    //    the S1/S2/S3 pipelines and the shared descriptor set. Split because the table is also read by the CPU-side S4/S5 mirrors and every probe,
    //    while the pipelines are purely a recording concern.
    // 🔴 The chain writes DEMAND, S5 CONSUMES it on the CPU (DriveShadowPageAllocation) to claim pages and upload the tile->page mapping, S6 PRIMES those
    //    pages to the atomic-min identity, and S7 now RASTERIZES caster depth into them. The atlas therefore holds real depth as of this step — but
    //    NOTHING SAMPLES IT YET (the tracer is P6.5), so the presented image is still unchanged and the Phase-0 pixel-identity gate continues to hold.
    //    ⚠️ The demand S5 reads is ONE IMAGE STALE by construction (see the preamble).
    ShadowTileStore              TileStore;        // [-] - 32² x 6 word table the marking passes atomicOr
    ShadowTileMarkingSubmission  TileMarking;      // [-] - S1/S2/S3 pipelines + the shared set; recorded in the preamble
    ShadowPageClearSubmission    ShadowPageClear;  // [-] - S6: clears the wanted-and-stale pages to the identity, recorded in the preamble
    ShadowDepthRasterSubmission  ShadowDepthRaster; // [-] - S7: rasterizes caster depth into the primed pages, recorded in the preamble

    // 📝 S7's per-caster-mesh descriptor sets, acquired once the scene geometry loads. 🔴 BOTH meshes must cast: the heads are the casters the shadow is
    //    of, and the floor slab is a caster as well as the receiver (a slab that only receives loses its own contact shadow). One set each, because
    //    re-pointing a single set between the two draws would mutate a descriptor a queued draw still references.
    VkDescriptorSet              ShadowCasterSceneSet = VK_NULL_HANDLE; // [-] - b2 -> VisibilityRaster.InstanceBuffer (the heads)
    VkDescriptorSet              ShadowCasterFloorSet = VK_NULL_HANDLE; // [-] - b2 -> FloorRaster.InstanceBuffer (the slab)
#ifdef FRONTIER_DEVELOPMENT_PROFILE
    uint32_t                ReportedTileUsedCount = UINT32_MAX; // [tile] - last traced Used count, so the P6.3c gate fires on CHANGE not per frame
    uint32_t                ReportedPageUsedCount = UINT32_MAX; // [page] - last traced Used PAGE count, so the P6.4 gate fires on CHANGE not per frame
    // 🔴 S5's RETURN VALUE, which the call site used to discard. Without it the log cannot tell "the allocator returned early on a ready condition" from
    //    "it requested pages and every request failed" — both leave the census at zero, and every neighbouring counter keeps reading healthy.
    uint32_t                ReportedAllocationRequestCount = UINT32_MAX; // [page] - last S5 request count, traced on CHANGE including change INTO zero
    // 🔴 TRI-STATE, not a bool, and the initial value is what makes it work. The interesting report is "trace OFF at boot and never on"; a bool seeded
    //    false would compare equal on the first image and suppress exactly that line, leaving the read side as silent as it was before it had a trace.
    int32_t                 ReportedShadowTraceable = -1;   // [-] - last traced gate state; -1 unreported, so image one always prints whichever way it went
    uint32_t                ReportedPageClearCount = UINT32_MAX; // [page] - last S6 clear count, so the S6 gate fires on CHANGE not per frame
    uint32_t                ReportedDepthRasterSignature = UINT32_MAX; // [-] - last S7 (heads<<8|floor) level counts, so the S7 gate fires on CHANGE
    uint32_t                ReportedPageMarkedCount = UINT32_MAX; // [page] - last count declared rendered; ⚠️ must track ReportedPageClearCount exactly
#endif
#ifdef FRONTIER_DEVELOPMENT_PROFILE
    int32_t                 ReportedOriginX = INT32_MIN;   // [tile] - last L0 toroidal origin traced, so the P6.3b gate fires on CHANGE not per frame
    int32_t                 ReportedOriginY = INT32_MIN;   // [tile] - paired Y of the traced origin
#endif

    // 📝 Occupancy is PREBAKED, not recomputed per frame. Every loaded instance is voxelized once at load through the exact triangle-cell
    //    overlap predicate (TriangleCellOverlap), so a concave object marks only the cells its surface actually crosses — the reason this is
    //    not a bounding box is that an AABB lights empty cells for an L-shaped object, which a GI / shadow consumer reads as real geometry.
    //    Per-frame the cells are only replayed into the field, never re-derived: per-triangle voxelization measures in milliseconds per ten
    //    thousand triangles, far too slow for a frame budget, and every shipping engine prebakes it for the same reason.
    std::vector<CellCoordinate> OccupiedWorldCells; // [cell] - prebaked cells the loaded scene's surfaces occupy, at ClipmapOccupancyLevel
    uint32_t                ClipmapOccupancyLevel = 0; // [-] - which clipmap level OccupiedWorldCells was voxelized against

    // 📝 The overlay's own copy, reduced to the occupancy set's outer shell (cells buried on all six sides removed). Kept SEPARATE from
    //    OccupiedWorldCells because the reduction is a viewing decision, not a truth about the scene: a solid slab's interior cells hold real
    //    geometry, they are just hidden behind their own outer faces, so drawing them costs a wire cage per cell for no visible difference. A GI
    //    or shadow consumer must read the full set — vacancy in this one would report empty space inside a solid.
    // 📝 How much this removes depends entirely on the occupancy level, because a cell only has an interior once the cells are smaller than the
    //    surface's own thickness. At level 2 (4 m cells) it removed ZERO — the floor was one cell layer thick, so every cell was exposed. At level 0
    //    (1 m cells) it removes ~300, because the heads' curved surfaces are now thick enough in cell terms to bury a few cells. Neither number is
    //    what bounds the overlay's frame cost: that is the camera radius gate applied at refresh time (ClipmapOccupiedDisplayRadius), which withholds
    //    thousands. The reduction is a cheap correctness-preserving trim on top of it, not the load-bearing limit.
    std::vector<CellCoordinate> DisplayedOccupancyCells; // [cell] - outer-shell subset of OccupiedWorldCells, for the debug overlay only
#ifdef FRONTIER_DEVELOPMENT_PROFILE
    ClipmapFieldInspection  ClipmapInspection;     // [-] - instanced wire-cube lattice + probe markers over the field (Numpad-4 toggles)
    bool                    ClipmapInspectionEnabled  = false; // [-] - When true, the clipmap lattice + probes composite over the view; default OFF
    bool                    ClipmapInspectionKeyLatch = false; // [-] - Edge latch so one Numpad-4 press toggles the visualization once
    uint32_t                ReportedInspectionCellCount = 0xFFFFFFFFu; // [-] - Last overlay cell count logged, so the trace fires on CHANGE not per frame
    bool                    ReportedInspectionShortfall = false;       // [-] - Latch for the capacity-truncation caution, so it states the onset once
#endif

    ViewportCamera          ViewCamera;            // [-] - Orbit / fly camera spec the grid is rendered through

    // 🔴 The observer the PREAMBLE advanced the sun window against, cached for RecordSequence to reuse. Both must read the SAME position: the
    //    preamble runs first (WindowSubstrate.cpp:283) and RecordSequence second (:323) in one command buffer, so recomputing it in the sequence
    //    after the camera has been driven again would advance the GI field against a different observer than the shadows used — the two spines
    //    would disagree about where the viewer is, by one frame of camera motion.
    Vector3f                CachedObserverPosition{ 0.0f, 0.0f, 0.0f }; // [m] - observer the preamble used this frame
    bool                    ObserverCacheSeeded = false;                // [-] - false until the first preamble fills the cache

    // 🔴 THE SUN CLIPMAP'S CENTRE, AND IT IS THE ORBIT TARGET RATHER THAN THE EYE ON PURPOSE. Held separately from CachedObserverPosition because
    //    the two answer different questions: the GI field wants where the VIEWER is, the shadow lattice wants what the viewer is LOOKING AT. The eye
    //    orbits under mere rotation, so centring the lattice on it makes level selection, origin scrolling, and page residency all functions of
    //    camera ORIENTATION — pages churn and levels flash while the world stands still. The target is invariant under orbit and zoom.
    // ⚠️ Anything that centres, scrolls, or levels the sun window reads THIS, never CachedObserverPosition. Mixing the two would put the marking pass
    //    and the scroll solve on different lattices, which presents as shadows offset from their casters rather than as an obvious fault.
    Vector3f                CachedShadowCentre{ 0.0f, 0.0f, 0.0f };     // [m] - sun-window centre the preamble used this frame (orbit target)

    double                  PreviousTimestamp = 0.0; // [s] - Last frame's clock reading, for the per-frame delta
    float                   FlySpeedScale     = 1.0f; // [-] - Scroll-adjusted fly-speed multiplier (Unreal-style)

    // 📝 Look-jitter fix, three switchable strategies (F1 cycles). The jitter is proven (JitterProbe) to be integer-count
    //    quantization of a 1000 Hz mouse sampled into a 60 fps loop: at slow drag speed each frame carries only 0–3 whole
    //    device counts, delivered in uneven bursts (some frames 2, some 0), so a raw count→angle map lurches-and-stalls.
    //      • Raw              — no filter; the count→angle baseline (this IS the jittery signal, kept for A/B).
    //      • AnchorAccumulate — Blender's viewrotate: rotate toward the total drag offset from the press anchor, not the
    //                           per-frame delta. Quantization cannot accumulate into jitter; zero lag, no phantom motion.
    //      • VelocityLowpass  — UE5-style: wall-clock exponential low-pass on the look VELOCITY (counts/s); framerate
    //                           independent, decays to zero when input stops (no phantom motion on the zero-count frames).
    LookFilterMode          LookMode          = LookFilterMode::AnchorAccumulate; // [-] - Active strategy (F1 cycles)

    // AnchorAccumulate state. On the drag's first frame the total offset is seeded; thereafter the summed pointer delta is
    // added and the camera is turned by (total·sens − already-applied), so it always tracks the exact hand offset losslessly.
    bool                    LookDragActive    = false; // [-] - True while an orbit/look drag is in progress
    float                   LookOffsetX       = 0.0f;  // [px] - Total summed drag offset since press (X)
    float                   LookOffsetY       = 0.0f;  // [px] - Total summed drag offset since press (Y)
    float                   LookAppliedX      = 0.0f;  // [rad] - Yaw already applied this drag (so we feed only the increment)
    float                   LookAppliedY      = 0.0f;  // [rad] - Pitch already applied this drag

    // VelocityLowpass state. Smoothed look velocity in device counts / second, low-passed with a wall-clock time constant.
    float                   LookVelocityX     = 0.0f;  // [px/s] - Low-passed pointer X velocity
    float                   LookVelocityY     = 0.0f;  // [px/s] - Low-passed pointer Y velocity

    bool                    LookModeKeyLatch  = false; // [-] - Edge latch so one F1 press cycles the mode once
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Stand up the window + Vulkan host + swapchain, then inspect the GPU's features. Returns false with the extension left safely
// finalizable on any failure (including a device below the architecture baseline).
bool InitializeRenderExtension(RenderExtension& Extension,
                               const char*      TitleText,
                               uint32_t         RequestedWidth,
                               uint32_t         RequestedHeight);

// Drive the frame sequence until the window is closed. A frame is one temporal sequence: at 0.1 this is acquire -> clear ->
// present via the substrate; later phases record the visibility, shade, and lighting passes into the same sequence.
void SynthesizeOutputSequence(RenderExtension& Extension);

// Destroy everything InitializeRenderExtension created. Safe on partially-initialized state.
void FinalizeRenderExtension(RenderExtension& Extension);

// Inspect the selected physical device and fill the HardwareFeatureProfile. Standalone so a host can query the verdict without
// driving a sequence. Requires the Vulkan host inside the substrate to already be initialized.
void InspectHardwareFeatures(const VulkanHost& Host, HardwareFeatureProfile& Profile);

} // namespace Frontier

#endif
