/*==============================================================================================================================================
                                                             SUZANNESCENE.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the two authored Suzanne test scenes. Each instance's world transform is scale -> rotate about +Z (the engine up-axis) ->
//    translate, composed straight into a column-major float[16] (no matrix library dependency — the transforms are simple enough to write out).
//    The layouts and constants mirror SuzanneRadialArray.html / SuzannePyramidStress.html so the rasterized result is checkable against the
//    browser reference: radial ring radius 6.5 / head scale 1.9 / lift; pyramid head scale 1.6 with shoulder-to-shoulder spacing and per-layer Z
//    rise, the C++ target being a 6-high pyramid (6x6..1x1 = 91 heads). The palette is the prototypes' bright-tint cycle.
//
//    The third scene, MATERIAL RINGS, has no browser reference: it exists to show the SurfacePresetTable, so its 13 heads are pinned to the 13 head
//    presets rather than to a prototype's layout. Tints are still assigned there, but the shade pass reads MaterialId instead — the tint only shows
//    if that scene is viewed through one of the debug-hash paths.

#include "Graphics/Scene/SuzanneScene.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// The prototypes' bright-tint cycle (red, orange, yellow, green, blue, purple, pink, cyan). Linear RGB, cycled per placed head.
const float PaletteTints[8][3] =
{
    { 0.95f, 0.25f, 0.22f },   // red
    { 0.98f, 0.55f, 0.15f },   // orange
    { 0.96f, 0.86f, 0.20f },   // yellow
    { 0.35f, 0.82f, 0.30f },   // green
    { 0.25f, 0.55f, 0.95f },   // blue
    { 0.62f, 0.35f, 0.90f },   // purple
    { 0.95f, 0.45f, 0.75f },   // pink
    { 0.10f, 0.92f, 0.88f },   // cyan
};

// Compose scale -> rotate(+Z, Angle) -> translate(Position) into a column-major 4x4. World = T * R * S applied to a local point. Column-major
// float[16] layout: element (Column*4 + Row); the translation occupies the last column (indices 12,13,14).
void ComposeModelMatrix(float ScaleFactor, float Angle, float PositionX, float PositionY, float PositionZ, float OutModel[16])
{
    const float CosAngle = std::cos(Angle);
    const float SinAngle = std::sin(Angle);

    // Column 0 (local +X after rotation about Z, scaled).
    OutModel[0] = CosAngle * ScaleFactor;  OutModel[1] = SinAngle * ScaleFactor;  OutModel[2] = 0.0f;         OutModel[3] = 0.0f;
    // Column 1 (local +Y after rotation about Z, scaled).
    OutModel[4] = -SinAngle * ScaleFactor; OutModel[5] = CosAngle * ScaleFactor;  OutModel[6] = 0.0f;         OutModel[7] = 0.0f;
    // Column 2 (local +Z, scaled — rotation about Z leaves it on the axis).
    OutModel[8] = 0.0f;                    OutModel[9] = 0.0f;                    OutModel[10] = ScaleFactor; OutModel[11] = 0.0f;
    // Column 3 (translation).
    OutModel[12] = PositionX;              OutModel[13] = PositionY;              OutModel[14] = PositionZ;   OutModel[15] = 1.0f;
}

// The rotation-only normal basis (no scale) for a +Z rotation by Angle, packed as three column vec3s each padded to vec4 (std140). The upper-left
// 3x3 of a rotation is orthonormal, so its inverse-transpose equals itself — this basis transforms normals directly.
void ComposeNormalBasis(float Angle, float OutBasis[12])
{
    const float CosAngle = std::cos(Angle);
    const float SinAngle = std::sin(Angle);
    // Column 0.
    OutBasis[0] = CosAngle;  OutBasis[1] = SinAngle;  OutBasis[2] = 0.0f;  OutBasis[3] = 0.0f;
    // Column 1.
    OutBasis[4] = -SinAngle; OutBasis[5] = CosAngle;  OutBasis[6] = 0.0f;  OutBasis[7] = 0.0f;
    // Column 2.
    OutBasis[8] = 0.0f;      OutBasis[9] = 0.0f;      OutBasis[10] = 1.0f; OutBasis[11] = 0.0f;
}

// Populate one instance's transform + tint + identity. PartitionId is the running placement ordinal. MaterialId defaults to 0 (the flat Standard
// record) so the two debug-hash scenes stay exactly as they were — only MaterialRings passes a real preset.
SuzanneSceneInstance MakeInstance(float ScaleFactor, float Angle, float PositionX, float PositionY, float PositionZ, uint32_t PartitionId,
                                 uint32_t MaterialId = 0u)
{
    SuzanneSceneInstance Instance;
    ComposeModelMatrix(ScaleFactor, Angle, PositionX, PositionY, PositionZ, Instance.Model);
    ComposeNormalBasis(Angle, Instance.NormalBasis);
    const float* Tint = PaletteTints[PartitionId % 8];
    Instance.Tint[0] = Tint[0];
    Instance.Tint[1] = Tint[1];
    Instance.Tint[2] = Tint[2];
    Instance.Tint[3] = 1.0f;
    Instance.PartitionId = PartitionId;
    Instance.MaterialId  = MaterialId;
    return Instance;
}

// Build the radial ring: RingCount heads on a ring of radius 6.5, each facing the centre (local +Y points inward), lifted above the floor.
void BuildRadialArray(uint32_t RingCount, std::vector<SuzanneSceneInstance>& Result)
{
    const float RingRadius = 6.5f;
    const float HeadScale  = 1.9f;
    const float HeadLift   = HeadScale * 0.9f;
    const float Pi         = 3.14159265358979323846f;

    for (uint32_t HeadIterator = 0; HeadIterator < RingCount; ++HeadIterator)
    {
        const float Theta   = ((float)HeadIterator / (float)RingCount) * Pi * 2.0f;
        const float PosX    = std::cos(Theta) * RingRadius;
        const float PosY    = std::sin(Theta) * RingRadius;
        // Local +Y points radially outward. Outward direction is Theta and the head's local +Y is +90 deg from +X, so Facing = Theta - 90 deg.
        const float Facing  = Theta - (Pi * 0.5f);
        Result.push_back(MakeInstance(HeadScale, Facing, PosX, PosY, HeadLift, HeadIterator));
    }
}

// Build the material showcase: two concentric rings of heads, each carrying one SurfacePresetTable entry. Fixed at 13 heads because the table has
// exactly 13 head presets (MaterialId 1..13; 0 is the floor) — the count is a property of the material set, not a caller choice, so a mismatch
// would silently leave a preset unrepresented.
//
// The split is inner 5 / outer 8 rather than one ring of 13: at a single radius, 13 heads either crowd shoulder-to-shoulder or push the ring so
// wide that each head is only a few pixels of highlight — and a highlight a few pixels across cannot show the two-lobe clearcoat structure or the
// iridescent hue sweep that are the whole point of the scene. Two radii keep every head at a readable size on screen.
//
// MaterialId is the placement ordinal + 1, so the inner ring reads Plastic -> Fabric (1..5) and the outer Glass -> Composite (6..13), matching the
// SurfacePresetTable constant order. Composite lands last on the outer ring, where the runtime lobe toggle is easiest to see.
void BuildMaterialRings(std::vector<SuzanneSceneInstance>& Result)
{
    const float    HeadScale   = 1.6f;
    const float    HeadLift    = HeadScale * 0.9f;
    const float    Pi          = 3.14159265358979323846f;
    const uint32_t InnerCount  = 5;
    const uint32_t OuterCount  = 8;
    const float    InnerRadius = 4.5f;
    const float    OuterRadius = 8.0f;

    for (uint32_t RingIterator = 0; RingIterator < 2; ++RingIterator)
    {
        const uint32_t RingCount  = (RingIterator == 0) ? InnerCount  : OuterCount;
        const float    RingRadius = (RingIterator == 0) ? InnerRadius : OuterRadius;
        // Offset the outer ring by half a step so its heads sit in the gaps between the inner ring's, not directly behind them.
        const float    RingPhase  = (RingIterator == 0) ? 0.0f : (Pi / (float)OuterCount);

        for (uint32_t HeadIterator = 0; HeadIterator < RingCount; ++HeadIterator)
        {
            const float Theta = RingPhase + ((float)HeadIterator / (float)RingCount) * Pi * 2.0f;
            const float PosX  = std::cos(Theta) * RingRadius;
            const float PosY  = std::sin(Theta) * RingRadius;
            // Face radially outward, as in the radial array: local +Y is +90 deg from +X, so Facing = Theta - 90 deg. Outward (not inward) keeps
            // every head's lit side toward the viewer wherever the camera orbits.
            const float Facing = Theta - (Pi * 0.5f);

            const uint32_t Ordinal    = static_cast<uint32_t>(Result.size());
            const uint32_t MaterialId = Ordinal + 1u;   // 0 is the floor; heads take 1..13 in placement order
            Result.push_back(MakeInstance(HeadScale, Facing, PosX, PosY, HeadLift, Ordinal, MaterialId));
        }
    }
}

// Build one 6-high square pyramid centred at (OriginX, OriginY): layers 6x6, 5x5, ... 1x1 (91 heads), each layer stepping up in Z, every head
// facing the pyramid's vertical axis. Appends to Result; ColourCursor advances the running identity/tint ordinal across the whole scene.
void BuildOnePyramid(float OriginX, float OriginY, float HeadScale, uint32_t& ColourCursor, std::vector<SuzanneSceneInstance>& Result)
{
    const uint32_t Layers   = 6;                 // C++ target: 6-high -> 91 heads
    const float    HeadStep  = HeadScale * 2.05f; // XY spacing between adjacent heads
    const float    LayerStep = HeadScale * 2.0f;  // Z rise per layer
    const float    HeadLift  = HeadScale * 0.95f; // base layer above the floor
    const float    Pi        = 3.14159265358979323846f;

    for (uint32_t Layer = 0; Layer < Layers; ++Layer)
    {
        const uint32_t Side = Layers - Layer;    // 6,5,4,3,2,1
        const float    PosZ = HeadLift + (float)Layer * LayerStep;
        for (uint32_t RowIterator = 0; RowIterator < Side; ++RowIterator)
        {
            for (uint32_t ColumnIterator = 0; ColumnIterator < Side; ++ColumnIterator)
            {
                const float PosX   = OriginX + ((float)ColumnIterator - ((float)Side - 1.0f) * 0.5f) * HeadStep;
                const float PosY   = OriginY + ((float)RowIterator - ((float)Side - 1.0f) * 0.5f) * HeadStep;
                // Face the pyramid's axis: aim local +Y at (OriginX, OriginY).
                const float ToAxisX = OriginX - PosX;
                const float ToAxisY = OriginY - PosY;
                float Facing = Pi * 0.5f;   // default (dead-centre head): face +Y arbitrarily
                if (std::fabs(ToAxisX) > 1e-5f || std::fabs(ToAxisY) > 1e-5f)
                    Facing = std::atan2(ToAxisY, ToAxisX) - (Pi * 0.5f);
                Result.push_back(MakeInstance(HeadScale, Facing, PosX, PosY, PosZ, ColourCursor));
                ++ColourCursor;
            }
        }
    }
}

// Build PyramidCount pyramids over a 4-wide grid, spaced so bases do not overlap (mirrors the prototype layout).
void BuildPyramidStress(uint32_t PyramidCount, std::vector<SuzanneSceneInstance>& Result)
{
    const float    HeadScale = 1.6f;
    const float    HeadStep  = HeadScale * 2.05f;
    const float    Spacing   = HeadStep * 6.5f;   // wider than the 6-high base so pyramids do not overlap
    const uint32_t Columns   = 4;
    const uint32_t RowCount  = (PyramidCount + Columns - 1) / Columns;

    uint32_t ColourCursor = 0;
    for (uint32_t PyramidIterator = 0; PyramidIterator < PyramidCount; ++PyramidIterator)
    {
        const uint32_t GridX = PyramidIterator % Columns;
        const uint32_t GridY = PyramidIterator / Columns;
        const float    OriginX = ((float)GridX - ((float)Columns - 1.0f) * 0.5f) * Spacing;
        const float    OriginY = ((float)GridY - ((float)RowCount - 1.0f) * 0.5f) * Spacing;
        BuildOnePyramid(OriginX, OriginY, HeadScale, ColourCursor, Result);
    }
}

uint32_t ClampCount(uint32_t Value, uint32_t Minimum, uint32_t Maximum, uint32_t DefaultValue)
{
    if (Value == 0) return DefaultValue;
    if (Value < Minimum) return Minimum;
    if (Value > Maximum) return Maximum;
    return Value;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const char* SuzanneSceneMeshPath(SuzanneSceneChoice Choice)
{
    switch (Choice)
    {
        case SuzanneSceneChoice::PyramidStress: return "Assets/SuzanneMeshSub2.json";
        case SuzanneSceneChoice::MaterialRings:
        case SuzanneSceneChoice::RadialArray:
        default:                                return "Assets/SuzanneMesh.json";
    }
}

void BuildSuzanneScene(SuzanneSceneChoice Choice, uint32_t Count, std::vector<SuzanneSceneInstance>& Result)
{
    Result.clear();
    switch (Choice)
    {
        case SuzanneSceneChoice::PyramidStress:
            BuildPyramidStress(ClampCount(Count, 1, 10, 4), Result);
            return;
        case SuzanneSceneChoice::MaterialRings:
            BuildMaterialRings(Result);   // Count ignored: the head count IS the preset count
            return;
        case SuzanneSceneChoice::RadialArray:
        default:
            BuildRadialArray(ClampCount(Count, 4, 12, 7), Result);
            return;
    }
}

} // namespace Frontier
