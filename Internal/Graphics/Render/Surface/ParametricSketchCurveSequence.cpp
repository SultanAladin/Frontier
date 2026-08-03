/*==============================================================================================================================================
                                                        PARAMETRICSKETCHCURVESEQUENCE.CPP
==============================================================================================================================================*/
// 🧩 The shared GPU-outline consumer both sketch builds drive (see ParametricSketchCurveSequence.h). Brings up the thick-line chain, re-stages the
//    shape OUTLINES the active view publishes each frame — expanding each polyline into ribbon segment quads — records the offscreen curve pass, and
//    hands the result back for the canvas composite. The outline peer of ParametricSketchSolidSequence; the ONE new piece here is the polyline ->
//    ribbon builder (AssembleStrokeRibbon), everything else mirrors the solid sequence's drop / resolve / restage / record / publish shape.

#include "Graphics/Render/Surface/ParametricSketchCurveSequence.h"

#include "Authoring/Geometry/Modeling/PolygonCluster.h"   // 📝 RenderVertex / RenderVertexStream — the stride-32 upload contract the curve rasterization draws

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The sketch is authored in world MM (outline XY + Z = Elevation mm); the sketch camera's ViewProjection expects world CM (the CPU sketch applies
//    the SAME 0.1 factor). Every uploaded position scales by this so the GPU outline registers 1:1 with the sketch + solid instead of 10× too large.
namespace
{
    constexpr float MmToCentimetreFactor = 0.1f;   // [cm/mm] - matches the sketch view's shared ground-plane scale
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      POLYLINE -> RIBBON BUILDER
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Expand one shape OUTLINE (world-mm polyline) into a stride-32 RenderVertexStream of ribbon segment quads the thick-line shaders consume. The
    //    packing REUSES the engine's one vertex contract with a bespoke thick-line meaning (no new upload path): for each segment (A, B),
    //        Position = THIS corner's own endpoint (cm)   Normal = the segment's OTHER endpoint (cm, the direction partner)
    //        TextureCoordinate = (SideSign ±1, ArcLength mm)
    //    The two A-corners carry Normal = B (so the shader's tangent = Normal - Position points A->B); the two B-corners carry Normal = A (tangent
    //    B->A) so the SideSign must FLIP at the B end to keep the ribbon on one consistent side. ArcLength is the running mm distance along the
    //    polyline (fed to the Phase-4 fragment linetype). Four verts + six indices per segment; ClosedLoop rejoins the last sample to the first.
    void AssembleStrokeRibbon(const ParametricSketchStrokeBody& Body, RenderVertexStream& Stream)
    {
        Stream.Vertices.clear();
        Stream.Indices.clear();

        const size_t PointCount = Body.Polyline.size();
        if (PointCount < 2)
            return;   // a single point has no segment; the sequence's restage skips a zero-index stream

        const size_t SegmentCount = Body.ClosedLoop ? PointCount : (PointCount - 1);
        Stream.Vertices.reserve(SegmentCount * 4);
        Stream.Indices.reserve(SegmentCount * 6);

        float ArcLengthMm = 0.0f;   // [mm] - running distance along the polyline (world mm, pre-scale) for the fragment linetype
        for (size_t SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
        {
            const ParametricSketchStrokeVertex& StartPoint = Body.Polyline[SegmentIndex];
            const ParametricSketchStrokeVertex& EndPoint   = Body.Polyline[(SegmentIndex + 1) % PointCount];

            // Endpoints in camera CM. Both corners at an end share the endpoint; the partner endpoint rides Normal so the shader builds the tangent.
            const float StartX = StartPoint.PositionX * MmToCentimetreFactor;
            const float StartY = StartPoint.PositionY * MmToCentimetreFactor;
            const float StartZ = StartPoint.PositionZ * MmToCentimetreFactor;
            const float EndX   = EndPoint.PositionX   * MmToCentimetreFactor;
            const float EndY   = EndPoint.PositionY   * MmToCentimetreFactor;
            const float EndZ   = EndPoint.PositionZ   * MmToCentimetreFactor;

            // Arc length in world mm (unscaled) so a Phase-4 dash period expressed in mm reads true regardless of the cm render scale.
            const float DeltaX = EndPoint.PositionX - StartPoint.PositionX;
            const float DeltaY = EndPoint.PositionY - StartPoint.PositionY;
            const float DeltaZ = EndPoint.PositionZ - StartPoint.PositionZ;
            const float SegmentLengthMm = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
            const float ArcAtStart = ArcLengthMm;
            const float ArcAtEnd   = ArcLengthMm + SegmentLengthMm;
            ArcLengthMm = ArcAtEnd;

            const uint32_t BaseIndex = (uint32_t)Stream.Vertices.size();

            // 📝 Four corners of the segment quad. A-corners: Position = A, Normal = B, so tangent = B - A (A->B) and SideSign as written. B-corners:
            //    Position = B, Normal = A, so tangent = A - B (B->A) is REVERSED, so the SideSign is flipped to keep the ribbon on the same visual side.
            RenderVertex StartLeft;
            StartLeft.Position[0] = StartX; StartLeft.Position[1] = StartY; StartLeft.Position[2] = StartZ;
            StartLeft.Normal[0]   = EndX;   StartLeft.Normal[1]   = EndY;   StartLeft.Normal[2]   = EndZ;
            StartLeft.TextureCoordinate[0] = +1.0f;  StartLeft.TextureCoordinate[1] = ArcAtStart;

            RenderVertex StartRight = StartLeft;
            StartRight.TextureCoordinate[0] = -1.0f;

            RenderVertex EndLeft;
            EndLeft.Position[0] = EndX;   EndLeft.Position[1] = EndY;   EndLeft.Position[2] = EndZ;
            EndLeft.Normal[0]   = StartX; EndLeft.Normal[1]   = StartY; EndLeft.Normal[2]   = StartZ;
            EndLeft.TextureCoordinate[0] = -1.0f;  EndLeft.TextureCoordinate[1] = ArcAtEnd;   // flipped: partner is A, so B->A tangent reverses the side

            RenderVertex EndRight = EndLeft;
            EndRight.TextureCoordinate[0] = +1.0f;

            Stream.Vertices.push_back(StartLeft);   // BaseIndex + 0
            Stream.Vertices.push_back(StartRight);  // BaseIndex + 1
            Stream.Vertices.push_back(EndLeft);     // BaseIndex + 2
            Stream.Vertices.push_back(EndRight);    // BaseIndex + 3

            // Two triangles (CCW): (SL, SR, EL) + (SR, ER, EL). Cull is disabled on the pipeline, so winding is cosmetic — kept consistent anyway.
            Stream.Indices.push_back(BaseIndex + 0);
            Stream.Indices.push_back(BaseIndex + 1);
            Stream.Indices.push_back(BaseIndex + 2);
            Stream.Indices.push_back(BaseIndex + 1);
            Stream.Indices.push_back(BaseIndex + 3);
            Stream.Indices.push_back(BaseIndex + 2);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            BRING-UP
//------------------------------------------------------------------------------------------------------------------------

bool InitializeParametricSketchCurveSequence(ParametricSketchCurveSequence& Sequence,
                                            VulkanHost&                    Host,
                                            const char*                    CurveVertSpvPath,
                                            const char*                    CurveFragSpvPath,
                                            uint32_t                       InitialExtent)
{
    Sequence.Host = &Host;

    // 📝 The lean chain: offscreen target → thick-line pipeline (against that target's render pass) → upload pool. Each failure disables the sequence
    //    only and never gates the caller's bring-up — the build falls back to the pure-2D sketch it was (no outline overlay).
    bool Ready = InitializeParametricSketchViewTarget(Sequence.Target, Host, InitialExtent, InitialExtent);
    if (Ready)
        Ready = InitializeParametricSketchCurveRasterization(Sequence.CurveRasterization,
                                                             Host,
                                                             CurveVertSpvPath,
                                                             CurveFragSpvPath,
                                                             Sequence.Target.RenderPass);

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
        fprintf(stderr, "[sketch-curve] GPU outline path unavailable — the thick-line stroke render is disabled (2D sketch unaffected)\n");
    return Sequence.Enabled;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            GEOMETRY SYNC
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Drop resident entries whose source stroke body vanished this frame, matching the resident vector against the live body id list.
    void DropVanishedStrokes(ParametricSketchCurveSequence&                    Sequence,
                             const std::vector<ParametricSketchStrokeBody>&    Bodies)
    {
        for (size_t Index = 0; Index < Sequence.ResidentStrokes.size();)
        {
            bool StillPresent = false;
            for (const ParametricSketchStrokeBody& Body : Bodies)
                if (Body.Identifier == Sequence.ResidentStrokes[Index].Identifier) { StillPresent = true; break; }

            if (!StillPresent)
            {
                ReleasePolygonBufferAllocation(*Sequence.Host, Sequence.ResidentStrokes[Index].Buffers);
                Sequence.ResidentStrokes.erase(Sequence.ResidentStrokes.begin() + Index);
            }
            else
            {
                ++Index;
            }
        }
    }

    // 📝 Find (or append) the resident row mirroring a stroke body id.
    ResidentParametricSketchStroke& ResolveResidentStroke(std::vector<ResidentParametricSketchStroke>& Residents, uint32_t Identifier)
    {
        for (ResidentParametricSketchStroke& Candidate : Residents)
            if (Candidate.Identifier == Identifier)
                return Candidate;
        Residents.push_back(ResidentParametricSketchStroke{});
        Residents.back().Identifier = Identifier;
        return Residents.back();
    }

    // 📝 The per-linetype dash geometry (world mm, matching ArcLength's unit) the fragment folds via fract(). The source body carries only a
    //    LineStyle selector (0 solid / 1 construction-dashed / 2 centerline), so the sequence maps it to a concrete period + duty here — one place,
    //    so a linetype tweak never touches the shader. Solid leaves the period 0 (the fragment then skips the discard entirely).
    struct StrokeLineStylePattern { float PeriodMm; float DutyCycle; };
    StrokeLineStylePattern ResolveLineStylePattern(uint8_t LineStyle)
    {
        switch (LineStyle)
        {
            case 1:  return { 6.0f, 0.5f };    // construction-dashed: 6 mm cycle, half lit — even dash / gap
            case 2:  return { 12.0f, 0.75f };  // centerline: 12 mm cycle, mostly lit (the long stroke of a dash-dot reads as a centerline here)
            default: return { 0.0f, 0.5f };    // solid: period 0 disables the discard
        }
    }

    // 📝 Fill the per-body draw constants from the source body: the resolved swatch colour, the linetype selector, and the dash period + duty the
    //    fragment linetype needs. HalfWidthPixels rides the sequence (uniform on-screen thickness), so the caller stamps it at record time.
    ParametricSketchStrokeConstants ResolveStrokeConstants(const ParametricSketchStrokeBody& Body, float HalfWidthPixels)
    {
        ParametricSketchStrokeConstants Constants;
        Constants.HalfWidthPixels = HalfWidthPixels;
        Constants.StrokeColour[0] = Body.ColourRGBA[0];
        Constants.StrokeColour[1] = Body.ColourRGBA[1];
        Constants.StrokeColour[2] = Body.ColourRGBA[2];
        Constants.StrokeColour[3] = Body.ColourRGBA[3];

        const StrokeLineStylePattern Pattern = ResolveLineStylePattern(Body.LineStyle);
        Constants.LineStyle     = (float)Body.LineStyle;
        Constants.DashPeriodMm  = Pattern.PeriodMm;
        Constants.DashDutyCycle = Pattern.DutyCycle;
        return Constants;
    }

    // 📝 Re-stage one stroke body into device-local ribbon buffers when its revision advanced. Drains the device FIRST (same DEVICE_LOST guard as the
    //    solid sequence: a dragged outline re-tessellates every frame, so freeing buffers the previous frame's pre-pass may still be reading is a
    //    use-after-free). The colour + linetype constants refresh every re-stage so a pure recolour (which bumps Revision) re-pushes them too.
    void RestageStroke(ParametricSketchCurveSequence&    Sequence,
                       ResidentParametricSketchStroke&   Resident,
                       const ParametricSketchStrokeBody& Body)
    {
        if (Resident.UploadedRevision == Body.Revision && Resident.Buffers.IndexCount != 0)
            return;

        RenderVertexStream Stream;
        AssembleStrokeRibbon(Body, Stream);
        Resident.Constants = ResolveStrokeConstants(Body, Sequence.HalfWidthPixels);

        vkDeviceWaitIdle(Sequence.Host->Device);
        ReleasePolygonBufferAllocation(*Sequence.Host, Resident.Buffers);
        if (Stream.Indices.empty())
        {
            // A degenerate outline (< 2 points) leaves no ribbon; mark it staged so an unchanged degenerate body stops re-draining every frame.
            Resident.UploadedRevision = Body.Revision;
            return;
        }
        if (ConstructPolygonBufferAllocation(*Sequence.Host, Sequence.UploadPool, Stream, Resident.Buffers))
        {
            Resident.UploadedRevision = Body.Revision;
            fprintf(stderr, "[curve] preview upload: body %u rev %u -> %zu verts / %zu tris\n",
                    Resident.Identifier, Body.Revision, Stream.Vertices.size(), Stream.Indices.size() / 3);
        }
        else
        {
            fprintf(stderr, "[curve] preview upload FAILED for body %u\n", Resident.Identifier);
        }
    }
}

void SynchronizeParametricSketchCurveSequence(ParametricSketchCurveSequence& Sequence, const ParametricSketchSceneView& SceneView)
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

    // 📝 Drive the curve camera straight from the published block — the SETTLED ViewProjection the CPU sketch uses, so the GPU outlines line up
    //    pixel-for-pixel with the solid + grid the composite stacks them over. ViewMatrix rides along for block parity though the curve shaders ignore it.
    ParametricSketchCurveCameraBlock CameraBlock;
    for (int Index = 0; Index < 16; ++Index)
    {
        CameraBlock.ViewProjection[Index] = SceneView.ViewProjection[Index];
        CameraBlock.ViewMatrix[Index]     = SceneView.ViewMatrix[Index];
    }
    RefreshParametricSketchCurveCamera(Sequence.CurveRasterization, CameraBlock);

    // 📝 Re-stage the shape OUTLINES the active view published this frame: upload a body the first time it appears or whenever its Revision advanced,
    //    drop a resident whose body vanished, and leave unchanged bodies alone (the revision gate makes idle frames free).
    const std::vector<ParametricSketchStrokeBody>& Bodies = RetrieveParametricSketchStrokeBodies();
    DropVanishedStrokes(Sequence, Bodies);
    for (const ParametricSketchStrokeBody& Body : Bodies)
    {
        ResidentParametricSketchStroke& Resident = ResolveResidentStroke(Sequence.ResidentStrokes, Body.Identifier);
        RestageStroke(Sequence, Resident, Body);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            RECORD + PUBLISH
//------------------------------------------------------------------------------------------------------------------------

void RecordParametricSketchCurveSequenceInto(ParametricSketchCurveSequence& Sequence, VkCommandBuffer CommandBuffer)
{
    if (!Sequence.Enabled || !Sequence.Target.ReadyStatus || Sequence.ResidentStrokes.empty())
        return;

    VkClearValue ClearValues[2] = {};
    ClearValues[0].color        = { { 0.0f, 0.0f, 0.0f, 0.0f } };   // fully TRANSPARENT — the solid + grid show through everywhere no stroke covers
    ClearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo PassInformation = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    PassInformation.renderPass        = Sequence.Target.RenderPass;
    PassInformation.framebuffer       = Sequence.Target.Framebuffer;
    PassInformation.renderArea.extent = { Sequence.Target.Width, Sequence.Target.Height };
    PassInformation.clearValueCount   = 2;
    PassInformation.pClearValues      = ClearValues;
    vkCmdBeginRenderPass(CommandBuffer, &PassInformation, VK_SUBPASS_CONTENTS_INLINE);

    // 📝 One draw per resident stroke through the thick-line pipeline, each pushing its own colour + linetype. A skipped (degenerate) resident holds a
    //    null buffer with IndexCount 0; RecordParametricSketchCurveInto no-ops on it, so the loop stays branch-free here.
    for (const ResidentParametricSketchStroke& Resident : Sequence.ResidentStrokes)
        RecordParametricSketchCurveInto(Sequence.CurveRasterization, CommandBuffer, Resident.Buffers, Resident.Constants,
                                        Sequence.Target.Width, Sequence.Target.Height);

    vkCmdEndRenderPass(CommandBuffer);
}

void PublishParametricSketchStrokeImage(const ParametricSketchCurveSequence& Sequence)
{
    if (Sequence.Enabled && Sequence.Target.Descriptor != VK_NULL_HANDLE && !Sequence.ResidentStrokes.empty())
        RegisterParametricSketchStrokeImage((ImTextureID)Sequence.Target.Descriptor, Sequence.Target.Width, Sequence.Target.Height);
    else
        RegisterParametricSketchStrokeImage((ImTextureID)0, 0, 0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            TEARDOWN
//------------------------------------------------------------------------------------------------------------------------

void FinalizeParametricSketchCurveSequence(ParametricSketchCurveSequence& Sequence)
{
    if (Sequence.Host == nullptr)
        return;

    for (ResidentParametricSketchStroke& Resident : Sequence.ResidentStrokes)
        ReleasePolygonBufferAllocation(*Sequence.Host, Resident.Buffers);
    Sequence.ResidentStrokes.clear();

    FinalizeParametricSketchCurveRasterization(Sequence.CurveRasterization);
    FinalizeParametricSketchViewTarget(Sequence.Target);

    if (Sequence.UploadPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(Sequence.Host->Device, Sequence.UploadPool, nullptr);
        Sequence.UploadPool = VK_NULL_HANDLE;
    }
    Sequence.Enabled = false;
}

} // namespace Frontier
