/*==============================================================================================================================================
                                                        PARAMETRICSKETCHSOLIDSEQUENCE.CPP
==============================================================================================================================================*/
// 🧩 The shared GPU-solid consumer both sketch builds drive (see ParametricSketchSolidSequence.h). Brings up the matcap chain, re-stages the lofts +
//    2D-fill shapes the active view publishes each frame, records the offscreen matcap pass, and hands the result back for the canvas composite.

#include "Graphics/Render/Surface/ParametricSketchSolidSequence.h"

#include "Authoring/Geometry/Modeling/PolygonCluster.h"   // 📝 RenderVertex / RenderVertexStream — the stride-32 upload contract the surface inscription draws

#include <algorithm>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The sketch is authored in world MM (outline XY + Z = Elevation mm); the sketch camera's ViewProjection expects world CM (the CPU sketch applies
//    the SAME 0.1 factor). Every uploaded position scales by this so the GPU solid registers 1:1 with the sketch instead of 10× too large.
namespace
{
    constexpr float MmToCentimetreFactor = 0.1f;   // [cm/mm] - matches the sketch view's shared ground-plane scale
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            BRING-UP
//------------------------------------------------------------------------------------------------------------------------

bool InitializeParametricSketchSolidSequence(ParametricSketchSolidSequence& Sequence,
                                             VulkanHost&                    Host,
                                             const char*                    MatcapPngPath,
                                             const char*                    MatcapVertSpvPath,
                                             const char*                    MatcapFragSpvPath,
                                             uint32_t                       InitialExtent)
{
    Sequence.Host = &Host;

    // 📝 The lean chain: chrome matcap → offscreen target → matcap pipeline → upload pool. Each failure disables the sequence only and never gates the
    //    caller's bring-up — the build falls back to the pure-2D sketch it was.
    bool Ready = InitializeParametricSketchMatcapTexture(Sequence.Matcap, Host, MatcapPngPath);
    if (Ready)
        Ready = InitializeParametricSketchViewTarget(Sequence.Target, Host, InitialExtent, InitialExtent);
    if (Ready)
        Ready = InitializeParametricSketchSurfaceInscription(Sequence.SurfaceInscription,
                                                             Host,
                                                             MatcapVertSpvPath,
                                                             MatcapFragSpvPath,
                                                             Sequence.Target.RenderPass,
                                                             Sequence.Matcap.View,
                                                             Sequence.Matcap.Sampler);

    // 📝 A transient pool on the graphics family; ConstructPolygonBufferAllocation submits its one-shot staging transfer through it.
    if (Ready)
    {
        VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        PoolInformation.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        PoolInformation.queueFamilyIndex = Host.GraphicsQueueFamily;
        if (vkCreateCommandPool(Host.Device, &PoolInformation, nullptr, &Sequence.UploadPool) != VK_SUCCESS)
        {
            Sequence.UploadPool = VK_NULL_HANDLE;
            Ready = false;
        }
    }

    Sequence.Enabled = Ready;
    if (!Sequence.Enabled)
        fprintf(stderr, "[sketch-solid] GPU solid path unavailable — the chrome loft / fill render is disabled (2D sketch unaffected)\n");
    return Sequence.Enabled;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            GEOMETRY SYNC
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Drop resident entries whose source body vanished this frame, matching the resident vector against the live body id list.
    template <typename BodyList, typename IdOf>
    void DropVanishedResidents(ParametricSketchSolidSequence&               Sequence,
                               std::vector<ResidentParametricSketchBody>&   Residents,
                               const BodyList&                              Bodies,
                               IdOf                                         IdentifierOf)
    {
        for (size_t Index = 0; Index < Residents.size();)
        {
            bool StillPresent = false;
            for (const auto& Body : Bodies)
                if (IdentifierOf(Body) == Residents[Index].Identifier) { StillPresent = true; break; }

            if (!StillPresent)
            {
                ReleasePolygonBufferAllocation(*Sequence.Host, Residents[Index].Buffers);
                Residents.erase(Residents.begin() + Index);
            }
            else
            {
                ++Index;
            }
        }
    }

    // 📝 Find (or append) the resident row mirroring a body id.
    ResidentParametricSketchBody& ResolveResident(std::vector<ResidentParametricSketchBody>& Residents, uint32_t Identifier)
    {
        for (ResidentParametricSketchBody& Candidate : Residents)
            if (Candidate.Identifier == Identifier)
                return Candidate;
        Residents.push_back(ResidentParametricSketchBody{});
        Residents.back().Identifier = Identifier;
        return Residents.back();
    }

    // 📝 Re-stage one body into device-local buffers when its revision advanced. Drains the device FIRST: a body being dragged re-tessellates every
    //    frame, so this destructive re-stage fires continuously; the previous frame's pre-pass draw may STILL be reading Resident.Buffers on the GPU,
    //    and freeing them under it is a use-after-free the driver escalates to DEVICE_LOST. A re-upload only happens on a real edit, so idle frames
    //    pay nothing. Returns true when a (re)upload happened (the caller advances UploadedRevision to Revision on success).
    bool RestageResident(ParametricSketchSolidSequence& Sequence,
                         ResidentParametricSketchBody&  Resident,
                         const RenderVertexStream&      Stream,
                         uint32_t                       Revision,
                         const char*                    Tag)
    {
        if (Resident.UploadedRevision == Revision && Resident.Buffers.IndexCount != 0)
            return false;

        vkDeviceWaitIdle(Sequence.Host->Device);
        ReleasePolygonBufferAllocation(*Sequence.Host, Resident.Buffers);
        if (ConstructPolygonBufferAllocation(*Sequence.Host, Sequence.UploadPool, Stream, Resident.Buffers))
        {
            Resident.UploadedRevision = Revision;
            fprintf(stderr, "[%s] preview upload: body %u rev %u -> %zu verts / %zu tris\n",
                    Tag, Resident.Identifier, Revision, Stream.Vertices.size(), Stream.Indices.size() / 3);
        }
        else
        {
            fprintf(stderr, "[%s] preview upload FAILED for body %u\n", Tag, Resident.Identifier);
        }
        return true;
    }

    // 📝 Re-stage the resident LOFT buffers from the store: upload a body the first time it appears or whenever its TessellationRevision advances (a
    //    source edit re-solved it), drop a resident whose body vanished, and leave unchanged bodies alone. The loft body carries authored normals.
    void SynchronizeResidentLofts(ParametricSketchSolidSequence& Sequence, ParametricSketchShapeStore& Store)
    {
        DropVanishedResidents(Sequence, Sequence.ResidentLofts, Store.LoftBodies,
                              [](const ParametricSketchLoftBody& Body) { return Body.Identifier; });

        for (const ParametricSketchLoftBody& Body : Store.LoftBodies)
        {
            ResidentParametricSketchBody& Resident = ResolveResident(Sequence.ResidentLofts, Body.Identifier);
            if (Resident.UploadedRevision == Body.TessellationRevision && Resident.Buffers.IndexCount != 0)
                continue;

            RenderVertexStream Stream;
            const size_t VertexCount = Body.Positions.size() / 3;
            Stream.Vertices.resize(VertexCount);
            for (size_t VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
            {
                RenderVertex& Vertex = Stream.Vertices[VertexIndex];
                Vertex.Position[0] = Body.Positions[VertexIndex * 3 + 0] * MmToCentimetreFactor;
                Vertex.Position[1] = Body.Positions[VertexIndex * 3 + 1] * MmToCentimetreFactor;
                Vertex.Position[2] = Body.Positions[VertexIndex * 3 + 2] * MmToCentimetreFactor;
                if (VertexIndex * 3 + 2 < Body.Normals.size())
                {
                    Vertex.Normal[0] = Body.Normals[VertexIndex * 3 + 0];
                    Vertex.Normal[1] = Body.Normals[VertexIndex * 3 + 1];
                    Vertex.Normal[2] = Body.Normals[VertexIndex * 3 + 2];
                }
            }
            Stream.Indices = Body.Indices;
            RestageResident(Sequence, Resident, Stream, Body.TessellationRevision, "loft");
        }
    }

    // 📝 Re-stage the resident SHAPE buffers (2D fills / solids) the active view tessellated + published this frame. A flat fill has no authored
    //    normals, so every vertex takes the sketch plane normal (0, 0, 1) — the matcap then lights it as a flat facet instead of black.
    void SynchronizeResidentShapes(ParametricSketchSolidSequence& Sequence)
    {
        const std::vector<ParametricSketchShapeBody>& Bodies = RetrieveParametricSketchShapeBodies();
        DropVanishedResidents(Sequence, Sequence.ResidentShapes, Bodies,
                              [](const ParametricSketchShapeBody& Body) { return Body.Identifier; });

        for (const ParametricSketchShapeBody& Body : Bodies)
        {
            ResidentParametricSketchBody& Resident = ResolveResident(Sequence.ResidentShapes, Body.Identifier);
            if (Resident.UploadedRevision == Body.Revision && Resident.Buffers.IndexCount != 0)
                continue;

            RenderVertexStream Stream;
            const size_t VertexCount = Body.Positions.size() / 3;
            const bool   NormalsSupplied = Body.Normals.size() == Body.Positions.size();   // a prism carries per-vertex normals; a flat fill leaves them empty
            Stream.Vertices.resize(VertexCount);
            for (size_t VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
            {
                RenderVertex& Vertex = Stream.Vertices[VertexIndex];
                Vertex.Position[0] = Body.Positions[VertexIndex * 3 + 0] * MmToCentimetreFactor;
                Vertex.Position[1] = Body.Positions[VertexIndex * 3 + 1] * MmToCentimetreFactor;
                Vertex.Position[2] = Body.Positions[VertexIndex * 3 + 2] * MmToCentimetreFactor;
                if (NormalsSupplied)
                {
                    // The extruded prism supplies real per-vertex normals (cap ±Z + wall lateral). Their variation is what makes the chrome
                    //    matcap read the body as a solid instead of a flat swatch, so pass them through unchanged (already unit-length).
                    Vertex.Normal[0] = Body.Normals[VertexIndex * 3 + 0];
                    Vertex.Normal[1] = Body.Normals[VertexIndex * 3 + 1];
                    Vertex.Normal[2] = Body.Normals[VertexIndex * 3 + 2];
                }
                else
                {
                    Vertex.Normal[0] = 0.0f;   // [-] - the sketch plane normal (Z-up world); a flat fill faces +Z so the matcap lights it
                    Vertex.Normal[1] = 0.0f;
                    Vertex.Normal[2] = 1.0f;
                }
            }
            Stream.Indices = Body.Indices;
            RestageResident(Sequence, Resident, Stream, Body.Revision, "shape");
        }
    }
}

void SynchronizeParametricSketchSolidSequence(ParametricSketchSolidSequence& Sequence, const ParametricSketchSceneView& SceneView)
{
    if (!Sequence.Enabled || !SceneView.ReadyStatus)
        return;

    // 📝 Size the target to the canvas rect (integer px). A no-op when the extent already matches; on a genuine change the previous frame's ImGui
    //    pass may still be sampling the old colour image, so drain the device first — a resize is rare (only on a window / canvas resize).
    const uint32_t CanvasWidth  = (uint32_t)(std::max)(1.0f, SceneView.CanvasMaximum.x - SceneView.CanvasMinimum.x);
    const uint32_t CanvasHeight = (uint32_t)(std::max)(1.0f, SceneView.CanvasMaximum.y - SceneView.CanvasMinimum.y);
    if (Sequence.Target.Width != CanvasWidth || Sequence.Target.Height != CanvasHeight)
    {
        vkDeviceWaitIdle(Sequence.Host->Device);
        ReconfigureParametricSketchViewTarget(Sequence.Target, CanvasWidth, CanvasHeight);
    }

    // 📝 Drive the matcap camera straight from the published block — the SETTLED ViewProjection the CPU sketch uses, so the GPU solid lines up
    //    pixel-for-pixel with the outlines / grid the overlay draws on top.
    ParametricSketchSurfaceCameraBlock CameraBlock;
    for (int Index = 0; Index < 16; ++Index)
    {
        CameraBlock.ViewProjection[Index] = SceneView.ViewProjection[Index];
        CameraBlock.ViewMatrix[Index]     = SceneView.ViewMatrix[Index];
    }
    RefreshParametricSketchSurfaceCamera(Sequence.SurfaceInscription, CameraBlock);

    if (ParametricSketchShapeStore* Store = RetrieveParametricSketchShapeSource())
        SynchronizeResidentLofts(Sequence, *Store);
    SynchronizeResidentShapes(Sequence);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            RECORD + PUBLISH
//------------------------------------------------------------------------------------------------------------------------

void RecordParametricSketchSolidSequenceInto(ParametricSketchSolidSequence& Sequence, VkCommandBuffer CommandBuffer)
{
    if (!Sequence.Enabled || !Sequence.Target.ReadyStatus ||
        (Sequence.ResidentLofts.empty() && Sequence.ResidentShapes.empty()))
        return;

    VkClearValue ClearValues[2] = {};
    ClearValues[0].color        = { { 0.0f, 0.0f, 0.0f, 0.0f } };   // fully TRANSPARENT — the sketch grid / outlines show through where no solid covers
    ClearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo PassInformation = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    PassInformation.renderPass        = Sequence.Target.RenderPass;
    PassInformation.framebuffer       = Sequence.Target.Framebuffer;
    PassInformation.renderArea.extent = { Sequence.Target.Width, Sequence.Target.Height };
    PassInformation.clearValueCount   = 2;
    PassInformation.pClearValues      = ClearValues;
    vkCmdBeginRenderPass(CommandBuffer, &PassInformation, VK_SUBPASS_CONTENTS_INLINE);

    for (const ResidentParametricSketchBody& Resident : Sequence.ResidentLofts)
        RecordParametricSketchSurfaceInto(Sequence.SurfaceInscription, CommandBuffer, Resident.Buffers, Sequence.Target.Width, Sequence.Target.Height);

    // 📝 The 2D-fill / solid shape bodies draw through the SAME matcap pipeline into the SAME target, so they self-occlude against the lofts by the
    //    shared depth buffer and composite into the canvas identically. Their normals are the flat plane normal.
    for (const ResidentParametricSketchBody& Resident : Sequence.ResidentShapes)
        RecordParametricSketchSurfaceInto(Sequence.SurfaceInscription, CommandBuffer, Resident.Buffers, Sequence.Target.Width, Sequence.Target.Height);

    vkCmdEndRenderPass(CommandBuffer);
}

void PublishParametricSketchSolidImage(const ParametricSketchSolidSequence& Sequence)
{
    if (Sequence.Enabled && Sequence.Target.Descriptor != VK_NULL_HANDLE &&
        (!Sequence.ResidentLofts.empty() || !Sequence.ResidentShapes.empty()))
        RegisterParametricSketchSolidImage((ImTextureID)Sequence.Target.Descriptor, Sequence.Target.Width, Sequence.Target.Height);
    else
        RegisterParametricSketchSolidImage((ImTextureID)0, 0, 0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            TEARDOWN
//------------------------------------------------------------------------------------------------------------------------

void FinalizeParametricSketchSolidSequence(ParametricSketchSolidSequence& Sequence)
{
    if (Sequence.Host == nullptr)
        return;

    for (ResidentParametricSketchBody& Resident : Sequence.ResidentLofts)
        ReleasePolygonBufferAllocation(*Sequence.Host, Resident.Buffers);
    Sequence.ResidentLofts.clear();
    for (ResidentParametricSketchBody& Resident : Sequence.ResidentShapes)
        ReleasePolygonBufferAllocation(*Sequence.Host, Resident.Buffers);
    Sequence.ResidentShapes.clear();

    FinalizeParametricSketchSurfaceInscription(Sequence.SurfaceInscription);
    FinalizeParametricSketchViewTarget(Sequence.Target);
    FinalizeParametricSketchMatcapTexture(Sequence.Matcap);

    if (Sequence.UploadPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(Sequence.Host->Device, Sequence.UploadPool, nullptr);
        Sequence.UploadPool = VK_NULL_HANDLE;
    }
    Sequence.Enabled = false;
}

} // namespace Frontier
