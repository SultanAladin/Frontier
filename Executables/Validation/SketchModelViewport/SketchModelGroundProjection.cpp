/*==============================================================================================================================================
                                                    SKETCHMODELGROUNDPROJECTION.CPP
==============================================================================================================================================*/
// 🧩 The shared camera ↔ ground projection body. The forward map (world mm → canvas pixel) and the inverse map (cursor pixel → ground world mm at
//    Z = 0) both derive their matrices from the ONE viewport camera via the exact chain AssembleSketchModelGridConstants uses, so every overlay
//    that places CAD geometry (workplanes, primitives) agrees on where a world point lands. See the header for the mm↔m unit contract.

#include "SketchModelGroundProjection.h"

#include "SketchModelViewportPanel.h"

#include "EngineContext/Navigation/Camera/CameraProjection/CameraViewMatrixSolver.h"
#include "EngineContext/Navigation/Camera/CameraProjection/ProjectionEvaluator.h"

#include <cmath>

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        FORWARD MAP
//------------------------------------------------------------------------------------------------------------------------

ProjectedPoint ProjectWorldPoint(const Frontier::Matrix4f& ViewProjection, ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                                 float WorldX, float WorldY, float WorldZ)
{
    // Convert the authored mm coordinate to the viewport's metres before projecting.
    WorldX *= MillimetresToMetres;
    WorldY *= MillimetresToMetres;
    WorldZ *= MillimetresToMetres;

    // clip[r] = Σ_c M.Column[c][r] * v[c], v = (x, y, z, 1). Column-major Column[c][r].
    const float ClipX = ViewProjection.Column[0][0] * WorldX + ViewProjection.Column[1][0] * WorldY + ViewProjection.Column[2][0] * WorldZ + ViewProjection.Column[3][0];
    const float ClipY = ViewProjection.Column[0][1] * WorldX + ViewProjection.Column[1][1] * WorldY + ViewProjection.Column[2][1] * WorldZ + ViewProjection.Column[3][1];
    const float ClipW = ViewProjection.Column[0][3] * WorldX + ViewProjection.Column[1][3] * WorldY + ViewProjection.Column[2][3] * WorldZ + ViewProjection.Column[3][3];

    ProjectedPoint Out;
    Out.InFront = (ClipW > 1e-5f);
    if (!Out.InFront)
    {
        Out.Pixel = ImVec2(0.0f, 0.0f);
        return Out;
    }

    // NDC in [-1, 1]; the projection already negated Column[1][1] (Vulkan Y-down), so NDC-y grows DOWNWARD like the screen.
    const float NdcX = ClipX / ClipW;
    const float NdcY = ClipY / ClipW;
    Out.Pixel.x = CanvasOrigin.x + (NdcX * 0.5f + 0.5f) * CanvasSize.x;
    Out.Pixel.y = CanvasOrigin.y + (NdcY * 0.5f + 0.5f) * CanvasSize.y;
    return Out;
}


Frontier::Matrix4f AssembleGroundViewProjection(const SketchModelViewportState& State)
{
    const Frontier::FocalOrientation Frame      = Frontier::SolveOrbitOrientation(State.Viewport.Camera);
    const Frontier::Matrix4f         Projection = Frontier::EvaluateProjectionFrame(State.Viewport.Camera);
    return Frontier::MultiplyMatrix(Projection, Frame.ViewMatrix);
}


ProjectedPoint ProjectGroundMillimetres(const SketchModelViewportState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSize, float MmX, float MmY)
{
    const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);
    return ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, MmX, MmY, 0.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        INVERSE MAP
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Unproject a canvas pixel to a world point (metres) at a given NDC depth through the inverse view-projection. The forward chain maps
    //    world(m)→clip; the inverse maps a clip point back to world. Pixel→NDC is the exact inverse of ProjectWorldPoint's NDC→pixel (Y already
    //    grows downward from the projection's negated Column[1][1], matching the screen), so a pixel round-trips to the ray it came from.
    Frontier::Vector3f UnprojectToWorldMetres(const Frontier::Matrix4f& InverseViewProjection, ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                                              ImVec2 Pixel, float NdcDepth)
    {
        const float NdcX = ((Pixel.x - CanvasOrigin.x) / CanvasSize.x) * 2.0f - 1.0f;
        const float NdcY = ((Pixel.y - CanvasOrigin.y) / CanvasSize.y) * 2.0f - 1.0f;

        // clip = (NdcX, NdcY, NdcDepth, 1) then world = Inverse * clip, dehomogenised. Column-major Column[c][r].
        const float ClipX = NdcX, ClipY = NdcY, ClipZ = NdcDepth, ClipW = 1.0f;
        const Frontier::Matrix4f& M = InverseViewProjection;
        const float WorldX = M.Column[0][0]*ClipX + M.Column[1][0]*ClipY + M.Column[2][0]*ClipZ + M.Column[3][0]*ClipW;
        const float WorldY = M.Column[0][1]*ClipX + M.Column[1][1]*ClipY + M.Column[2][1]*ClipZ + M.Column[3][1]*ClipW;
        const float WorldZ = M.Column[0][2]*ClipX + M.Column[1][2]*ClipY + M.Column[2][2]*ClipZ + M.Column[3][2]*ClipW;
        const float WorldW = M.Column[0][3]*ClipX + M.Column[1][3]*ClipY + M.Column[2][3]*ClipZ + M.Column[3][3]*ClipW;

        const float Inv = (std::fabs(WorldW) > 1e-8f) ? (1.0f / WorldW) : 0.0f;
        return Frontier::Vector3f{ WorldX * Inv, WorldY * Inv, WorldZ * Inv };
    }
}


bool CastCursorToGroundMillimetres(const SketchModelViewportState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSize, ImVec2 Pixel,
                                   float& OutMmX, float& OutMmY)
{
    const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);
    const Frontier::Matrix4f Inverse        = Frontier::InvertMatrix(ViewProjection);

    // Two points along the cursor ray (near + far), both in metres; the ray direction is their difference.
    const Frontier::Vector3f Near = UnprojectToWorldMetres(Inverse, CanvasOrigin, CanvasSize, Pixel, 0.0f);
    const Frontier::Vector3f Far  = UnprojectToWorldMetres(Inverse, CanvasOrigin, CanvasSize, Pixel, 1.0f);

    const float DirX = Far.XCoord - Near.XCoord;
    const float DirY = Far.YCoord - Near.YCoord;
    const float DirZ = Far.ZCoord - Near.ZCoord;

    // Intersect the ray Near + t*Dir with the plane Z = 0: t = -Near.z / Dir.z. Reject a near-parallel ray, and one that only meets the
    // ground behind the eye (t < 0).
    if (std::fabs(DirZ) < 1e-6f)
        return false;
    const float T = -Near.ZCoord / DirZ;
    if (T < 0.0f)
        return false;

    const float HitMetresX = Near.XCoord + DirX * T;
    const float HitMetresY = Near.YCoord + DirY * T;
    OutMmX = HitMetresX * 1000.0f;   // world m → authored mm
    OutMmY = HitMetresY * 1000.0f;
    return true;
}

}   // namespace SketchModelViewportValidation
