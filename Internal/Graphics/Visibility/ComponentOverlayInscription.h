/*==============================================================================================================================================
                                                      COMPONENTOVERLAYINSCRIPTION.H
==============================================================================================================================================*/
// 🧩 The component (sub-object) overlay: a fullscreen composite that draws the authoring handles a modeller selects by — VERTEX dots, EDGE lines,
//    and FACE tints — over the shaded frame, plus the hover / selected highlight on whichever component is under the cursor. Where
//    SelectionOutlineInscription answers "which OBJECT is this", this unit answers "which vertex / edge / face of it is this", so the two are
//    siblings: same borrowed id buffer, same fullscreen-triangle idiom, own pipeline so either can be toggled without the other.
//
// 💡 WHY THIS IS A COMPOSITE AND NOT A POINT / LINE DRAW. The conventional component overlay re-draws the mesh as GL_POINTS and GL_LINES with its
//    own vertex fetch, which means a second geometry pass per mode, a vertex-buffer walk that grows with the model, and a depth-bias fight against
//    the surface it sits on. This pass instead reconstructs the component set from the SAME identity the raster already resolved: the id at a pixel
//    names the partition and the primitive, and the primitive names three indices whose positions are already resident in the vertex SSBO the shade
//    pass binds. So the overlay costs one fullscreen pass regardless of polygon count, and it is depth-correct by construction — a dot only appears
//    where its own triangle won the depth test, so vertices behind the surface are culled without a bias tweak.
//
// 📝 SCREEN-SPACE, FIXED-SIZE HANDLES. Vertex dots are a fixed pixel size (not depth-scaled) because a handle is a click target first and a spatial
//    cue second: dots that shrink with distance become unclickable exactly when the user has zoomed out to see the whole model. Edge lines are a
//    fixed pixel width for the same reason. The size is measured in the projected screen positions of the triangle's corners, so it is independent
//    of world scale.
//
// ⚠️ HEADS ONLY — the floor is deliberately absent, and this is a data limitation, not an oversight. The floor mesh rasterizes into the SHARED
//    visibility buffer from its own vertex / index buffers (partition ordinals >= FloorPartitionBase), and those buffers are NOT bound here — the
//    same reason SurfaceShade.frag skips them. Reconstructing a floor component would index the heads' vertex SSBO with a floor index and read an
//    unrelated position, so floor partitions are rejected outright and stay object-selectable only. Binding a second geometry source is the
//    follow-up that lifts this.
//
// 📝 Colour convention, matching the request: HOVER is orange (the transient "you are pointing at this" cue) and SELECTED is blue (the committed
//    state). Object-mode selection keeps its own amber ring in SelectionOutlineInscription and is untouched by this unit, so the two modes stay
//    visually distinguishable rather than both being amber.

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_COMPONENTOVERLAYINSCRIPTION_H
#define FRONTIER_GRAPHICS_VISIBILITY_COMPONENTOVERLAYINSCRIPTION_H

// 📝 Authoring-only unit, gated exactly like ObjectPickReadback / SelectionOutlineInscription (which supply NoSelectionSentinel), so a
//    runtime-only renderer carries none of it and the three gate identically.
#ifdef FRONTIER_POLYGON_AUTHORING

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Visibility/VisibilityImage.h"
#include "Graphics/Visibility/VisibilityDepth.h"
#include "Graphics/Visibility/ObjectPickReadback.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Descriptor bindings in set 0: 0 = id image, 1 = depth image, 2 = vertices, 3 = indices, 4 = instances, 5 = authored source faces,
//    6 = authored corner vertices, 7 = authored side edges. Named so the layout, the pool size, and the write array cannot drift apart.
// ⚠️ Must match the binding numbers declared in ComponentOverlay.frag. A mismatch is not a compile error on either side — it surfaces as the shader
//    reading a different buffer than intended, so the two lists are only ever edited together.
constexpr uint32_t ComponentOverlayBindingCount = 8u;

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which component class the authoring tools are currently addressing (Numpad-7 cycles). Object is the default and means this overlay draws
//    nothing at all — the object outline handles that mode, so the two never draw handles over each other.
// ⚠️ The numeric values are pushed to the shader as ComponentMode, so the order must match the constants in ComponentOverlay.frag.
enum class ComponentSelectionMode : uint32_t
{
    Object = 0,   // No handles; SelectionOutlineInscription owns this mode
    Vertex,       // Square dots at every projected corner
    Edge,         // Lines along every triangle / authored edge
    Face,         // Tinted fill over the whole face

    ModeCount
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the fragment stage reads, byte-compatible with ComponentOverlay.frag's ComponentConstants. Laid out mat4 first, then vec4s, then
//    the scalar tail, so every std430 member lands on its natural boundary with no implicit padding.
// ⚠️ Verified layout by offsetof probe (offsets in bytes): VP=0, Handle=64, Hover=80, Selected=96, Wire=112, then the tail — Mode=128, SelPartition=132,
//    SelPrimitive=136, SelComponent=140, FloorBase=144, AuthoredTriangleCount=148, CursorX=152, CursorY=156, DotRadius=160, LineWidth=164,
//    TintStrength=168. Total 172, every member on its natural boundary with no implicit padding and no trailing pad required. Any member inserted
//    mid-block must be RE-verified, not reasoned about, or the GPU reads garbage from a silently shifted offset — AuthoredTriangleCount was added mid-tail
//    and shifted every member after it by four bytes, which is exactly the class of change that must never be eyeballed.
// ⚠️ 172 BYTES EXCEEDS THE 128-BYTE VULKAN GUARANTEED MINIMUM for maxPushConstantsSize. This device reports 256 so it is fine here, and no other pass
//    in the tree validates against the limit either (SurfaceShadeConstants is 112 and stays under by luck of having one fewer vec4). If this ever
//    needs to run on a 128-byte device, the colours are the cheap thing to move — pack the four RGBAs into a small UBO, or drop WireColour, which the
//    shader currently derives from the tint anyway. Do NOT solve it by shrinking the matrix.
//
// 📝 A CURSOR POSITION, NOT A RESOLVED HOVER. The obvious design pushes "which component is hovered" here, which means the host must first learn it —
//    and it cannot: the geometry is device-local and the host-side mesh is discarded after upload, so there is nothing on the CPU to run a
//    nearest-corner test against. The next-obvious fix is to have the shader write the hovered key to an SSBO and read it back, but that buys a SECOND
//    frame of latency on top of the pick ring's one, plus a buffer, a binding, and a staging ring. Pushing the raw cursor instead lets the shader
//    re-run its own reconstruction at the cursor texel: same code path, same frame, no readback, no extra state. The cost is one extra texel fetch and
//    one triangle reconstruction per pixel, which is uniform across the draw and negligible against the fullscreen pass itself.
//
// 📝 FORWARD view-projection, not the inverse. An earlier draft passed the inverse to match SurfaceShadeConstants, which forced the fragment stage to
//    call inverse() on a mat4 for EVERY pixel just to project three corners — the overlay only ever transforms world -> screen, never the reverse.
//    Passing the forward matrix deletes that per-pixel inverse outright.
struct ComponentOverlayConstants
{
    float    ViewProjection[16] = {};                           // [-] - world -> clip, for projecting reconstructed corners to screen
    float    HandleColour[4]   = { 0.15f, 0.15f, 0.18f, 0.85f }; // [-] - unhighlighted handle RGBA (dark, so the highlights read against it)
    float    HoverColour[4]    = { 1.00f, 0.55f, 0.10f, 1.0f };  // [-] - HOVERED component RGBA (orange, the transient cue)
    float    SelectedColour[4] = { 0.20f, 0.55f, 1.00f, 1.0f };  // [-] - SELECTED component RGBA (blue, the committed state)
    float    WireColour[4]     = { 0.05f, 0.05f, 0.07f, 0.55f }; // [-] - face-mode boundary wire RGBA

    uint32_t ComponentMode     = 0;                        // [-] - ComponentSelectionMode as a uint (0 = Object = draw nothing)
    uint32_t SelectedPartition = NoSelectionSentinel;      // [-] - committed partition ordinal
    uint32_t SelectedPrimitive = NoSelectionSentinel;      // [-] - committed primitive ordinal
    uint32_t SelectedComponent = NoSelectionSentinel;      // [-] - committed component key; sentinel means "the whole primitive" (Face mode)
    uint32_t FloorPartitionBase = 2048u;                   // [-] - ordinals >= this are the floor, whose geometry is NOT bound (see header)

    // 📝 How many triangles the authored provenance actually covers; 0 means the authored view is unavailable and the shader draws no handles.
    // ⚠️ This is the AUTHORITATIVE gate, and the shader must not substitute a buffer length() for it. When the tables are absent, Refresh keeps bindings
    //    5-7 legally bound by aliasing them onto the index buffer (an unbound descriptor is undefined memory), so those buffers report a perfectly
    //    healthy non-zero length while containing index data. A length-based gate would therefore sail straight through and reinterpret indices as face
    //    ordinals — drawing confident garbage. Only this count distinguishes "real tables" from "stand-ins".
    uint32_t AuthoredTriangleCount = 0;                    // [-] - triangles covered by the authored tables; 0 = draw nothing

    // 📝 The cursor, NOT a resolved hover. See the note above the struct — the shader derives which component is hovered from this position itself.
    //    Negative means the pointer is outside the window, which the shader reads as "nothing hovered".
    int32_t  CursorX           = -1;                       // [px] - cursor position in the id buffer's texel space
    int32_t  CursorY           = -1;                       // [px] - cursor position in the id buffer's texel space

    float    VertexDotRadius   = 3.0f;                     // [px] - half-width of a vertex dot (square, screen-space)
    float    EdgeLineWidth     = 1.6f;                     // [px] - half-width of an edge line
    float    FaceTintStrength  = 0.35f;                    // [-] - alpha of the face fill in Face mode
};

// 📝 The overlay's owned device resources. One descriptor set carries everything the reconstruction needs: the id image, the scene depth, and the
//    three geometry SSBOs (vertices / indices / instances) that the shade pass already uploads. The buffers are BORROWED — Refresh re-points the set
//    at them, and must be re-called after a resize (the image views are rebuilt) or a scene reload (the buffers are reallocated).
struct ComponentOverlayInscription
{
    VulkanHost*           Host           = nullptr;          // [-] - not owned; supplies device / allocator
    VkPipeline            Pipeline       = VK_NULL_HANDLE;   // [-] - fullscreen-triangle composite pipeline (dynamic rendering)
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;   // [-] - the one set + the ComponentConstants push range
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - set 0: 0=id, 1=depth, 2=vertices, 3=indices, 4=instances
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - pool sized for one set
    VkDescriptorSet       ResourceSet    = VK_NULL_HANDLE;   // [-] - the bound set
    VkSampler             PointSampler   = VK_NULL_HANDLE;   // [-] - nearest / clamp; neither the id nor the depth may be filtered
    VkImageView           BoundIdView    = VK_NULL_HANDLE;   // [-] - id view the set points at; Refresh writes only on change
    VkImageView           BoundDepthView = VK_NULL_HANDLE;   // [-] - depth view the set points at
    VkBuffer              BoundVertexBuffer   = VK_NULL_HANDLE; // [-] - vertex SSBO the set points at
    VkBuffer              BoundIndexBuffer    = VK_NULL_HANDLE; // [-] - index SSBO the set points at
    VkBuffer              BoundInstanceBuffer = VK_NULL_HANDLE; // [-] - instance SSBO the set points at

    // 📝 The authored-topology tables, OWNED here rather than borrowed. The other three SSBOs already exist for the raster and the shade pass, so this
    //    unit only re-points at them; the provenance exists solely for component selection, so nothing else would allocate or free it.
    VkBuffer              SourceFaceBuffer     = VK_NULL_HANDLE; // [-] - per-triangle authored face ordinals
    VkDeviceMemory        SourceFaceMemory     = VK_NULL_HANDLE; // [-] - backing allocation
    VkDeviceSize          SourceFaceCapacity   = 0;              // [B] - allocated bytes (grown, never shrunk)
    VkBuffer              CornerVertexBuffer   = VK_NULL_HANDLE; // [-] - per-triangle-corner cluster vertex indices (3 per triangle)
    VkDeviceMemory        CornerVertexMemory   = VK_NULL_HANDLE; // [-] - backing allocation
    VkDeviceSize          CornerVertexCapacity = 0;              // [B] - allocated bytes
    VkBuffer              SideEdgeBuffer       = VK_NULL_HANDLE; // [-] - per-triangle-side authored edge ordinals (3 per triangle)
    VkDeviceMemory        SideEdgeMemory       = VK_NULL_HANDLE; // [-] - backing allocation
    VkDeviceSize          SideEdgeCapacity     = 0;              // [B] - allocated bytes
    uint32_t              AuthoredTriangleCount = 0;             // [-] - triangles the uploaded tables cover; 0 = authored modes unavailable

    bool                  ReadyCondition = false;            // [-] - true once pipeline + layout + descriptors are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the pipeline, layout, descriptor plumbing, and point sampler. ColourFormat is the swapchain colour format the dynamic-rendering pipeline is
// created against. Reads ComponentOverlay.vert.spv / .frag.spv from ShaderDirectory. Returns false (ReadyCondition stays false) if dynamic rendering
// is unavailable, a shader is missing, or any Vulkan step fails. Pair with FinalizeComponentOverlayInscription.
bool InitializeComponentOverlayInscription(ComponentOverlayInscription& Inscription,
                                           VulkanHost&                  Host,
                                           VkFormat                     ColourFormat,
                                           const char*                  ShaderDirectory);

// Point the descriptor set at the current id + depth views and the geometry SSBOs. A no-op when every handle already matches (cheap to call every
// frame) or when any side is not ready. Must not run while a frame referencing ResourceSet is in flight — call at build time, after a resize, and
// after a scene reload, with the device idle.
void RefreshComponentOverlayInscription(ComponentOverlayInscription& Inscription,
                                        const VisibilityImage&       Image,
                                        const VisibilityDepth&       Depth,
                                        VkBuffer                     VertexBuffer,
                                        VkDeviceSize                 VertexByteCapacity,
                                        VkBuffer                     IndexBuffer,
                                        VkDeviceSize                 IndexByteCapacity,
                                        VkBuffer                     InstanceBuffer,
                                        VkDeviceSize                 InstanceByteCapacity);

// Upload the authored-topology provenance the component modes are keyed on (see AuthoredTopologyMap). Allocates / grows the three owned SSBOs, copies
// through a host-visible mapping, and re-points the descriptor set at them. Call once per scene load, with the device idle. An empty or inconsistent map
// leaves AuthoredTriangleCount at 0, which the shader observes through its length() guards and answers by drawing no handles — the deliberate
// degradation, since falling back to triangle keys would silently reproduce the triangulated-selection bug this data exists to fix. Returns false when
// the upload could not be completed.
bool UploadComponentOverlayTopology(ComponentOverlayInscription& Inscription,
                                    const std::vector<uint32_t>& SourceFace,
                                    const std::vector<uint32_t>& CornerVertex,
                                    const std::vector<uint32_t>& SideEdge);

// Record the overlay composite into the caller's ALREADY-OPEN colour scope. A no-op when not ready or when Constants.ComponentMode is Object, so
// object mode records zero work. CommandBuffer must be inside an active dynamic-rendering scope whose colour format matches the pipeline's.
void RecordComponentOverlayInscription(const ComponentOverlayInscription& Inscription,
                                       VkExtent2D                         Extent,
                                       const ComponentOverlayConstants&   Constants,
                                       VkCommandBuffer                    CommandBuffer);

// Destroy the pipeline, layout, descriptor plumbing, and sampler, then reset to empty. The device must be idle. Safe on a never-initialized value.
// Does NOT touch the borrowed image or buffers.
void FinalizeComponentOverlayInscription(ComponentOverlayInscription& Inscription);

} // namespace Frontier

#endif // FRONTIER_POLYGON_AUTHORING

#endif
