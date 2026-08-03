/*==============================================================================================================================================
                                                                    WORKPLANE.CPP
==============================================================================================================================================*/
// 🧩 The construction-plane authoring model's implementation: per-panel plane stores, the parametric frame solve (Origin + U/V/Normal from a
//    construction method), the Append / Resolve / Detach verbs, the edit-log + undo/redo/jump history, and the sheet+grid render assembly.
//    Device-independent CPU — the viewport / outliner / Properties / History layers read this. See Workplane.h for the model + the Z-up world
//    convention. Mirrors ParametricSketchShapeStore.cpp's store / bridge / history idioms, pared to the plane concern.

#include "Workplane.h"

#include <cmath>    // 📝 std::sqrt / std::cos / std::sin — normalize the solved axes + rotate the Angle-method frame.
#include <cstdio>   // 📝 std::snprintf — seed a plane's title + a log entry's label + detail.
#include <cstring>  // 📝 std::memcpy — fold the sheet's float bits into the body change-revision hash.

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STORE + REGISTRY
//------------------------------------------------------------------------------------------------------------------------

WorkplaneStore& ResolveWorkplaneStore(WorkplaneRegistry& Registry, uint32_t OwnerDocument)
{
    for (WorkplaneStore& Entry : Registry.Stores)
        if (Entry.OwnerDocument == OwnerDocument)
            return Entry;
    WorkplaneStore Fresh;
    Fresh.OwnerDocument = OwnerDocument;
    Registry.Stores.push_back(Fresh);
    return Registry.Stores.back();
}

void ReleaseWorkplaneStore(WorkplaneRegistry& Registry, uint32_t OwnerDocument)
{
    for (size_t Index = 0; Index < Registry.Stores.size(); ++Index)
        if (Registry.Stores[Index].OwnerDocument == OwnerDocument)
        {
            Registry.Stores.erase(Registry.Stores.begin() + Index);
            return;
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SOURCE BRIDGE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The active plane store the runtime publishes each frame so the separate outliner / Properties / History boxes read + edit the same
//    planes without threading a pointer through the dock-host chain. Null outside a live plane-owning view. Mirrors the shape store's
//    RegisterParametricSketchShapeSource bridge exactly.
namespace
{
    WorkplaneStore* PublishedWorkplaneSource = nullptr;
}

void RegisterWorkplaneSource(WorkplaneStore* Store)
{
    PublishedWorkplaneSource = Store;
}

WorkplaneStore* RetrieveWorkplaneSource()
{
    return PublishedWorkplaneSource;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        FRAME SOLVE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Normalize a raw (x, y, z) direction in place, leaving it untouched when it is degenerate (near zero) so a solve never writes NaNs.
namespace
{
    void NormalizeDirection(float& DirectionX, float& DirectionY, float& DirectionZ)
    {
        const float LengthSquared = DirectionX * DirectionX + DirectionY * DirectionY + DirectionZ * DirectionZ;
        if (LengthSquared <= 1e-12f)
            return;
        const float InverseLength = 1.0f / std::sqrt(LengthSquared);
        DirectionX *= InverseLength;
        DirectionY *= InverseLength;
        DirectionZ *= InverseLength;
    }

    // 📝 Seat the fixed Z-up world frame for a principal method (or the neutral XY fallback the geometry-referencing methods use this phase).
    //    The viewport is Z-up (ground grid on world XY at Z=0, world up +Z), so a default XY plane lies FLAT on the grid.
    //    XY: U=+X, V=+Y, N=+Z (ground). XZ: U=+X, V=+Z, N=+Y (front wall). YZ: U=+Y, V=+Z, N=+X (side wall).
    void SeatPrincipalFrame(Workplane& Plane, WorkplaneConstructionCategory Method)
    {
        Plane.AxisUX = 1.0f; Plane.AxisUY = 0.0f; Plane.AxisUZ = 0.0f;
        Plane.AxisVX = 0.0f; Plane.AxisVY = 1.0f; Plane.AxisVZ = 0.0f;
        Plane.NormalX = 0.0f; Plane.NormalY = 0.0f; Plane.NormalZ = 1.0f;
        if (Method == WorkplaneConstructionCategory::PrincipalXZ)
        {
            Plane.AxisVX = 0.0f; Plane.AxisVY = 0.0f; Plane.AxisVZ = 1.0f;
            Plane.NormalX = 0.0f; Plane.NormalY = 1.0f; Plane.NormalZ = 0.0f;
        }
        else if (Method == WorkplaneConstructionCategory::PrincipalYZ)
        {
            Plane.AxisUX = 0.0f; Plane.AxisUY = 1.0f; Plane.AxisUZ = 0.0f;
            Plane.AxisVX = 0.0f; Plane.AxisVY = 0.0f; Plane.AxisVZ = 1.0f;
            Plane.NormalX = 1.0f; Plane.NormalY = 0.0f; Plane.NormalZ = 0.0f;
        }
    }
}

void SolveWorkplaneFrame(Workplane& Plane)
{
    // 📝 Seat the base frame. The principal methods keep their fixed world frame; Offset / Angle start from XY and transform it; the
    //    geometry-referencing methods (ThreePoint / Midplane / Tangent / PointNormal / OnFace) seat the neutral XY frame until the
    //    reference-resolve layer lands (they carry their references + intent regardless, so no authored data is lost).
    switch (Plane.Method)
    {
        case WorkplaneConstructionCategory::PrincipalXZ:
        case WorkplaneConstructionCategory::PrincipalYZ:
            SeatPrincipalFrame(Plane, Plane.Method);
            break;
        default:
            SeatPrincipalFrame(Plane, WorkplaneConstructionCategory::PrincipalXY);
            break;
    }

    // 📝 Origin starts at the AUTHORED base origin (world zero for every default / added plane; the drag midpoint for an interactively PLACED
    //    plane), so a plane drawn away from the world centre stays where it was swept. Offset then shifts it further along the seeded normal.
    Plane.OriginX = Plane.AuthoredOriginX;
    Plane.OriginY = Plane.AuthoredOriginY;
    Plane.OriginZ = Plane.AuthoredOriginZ;

    if (Plane.Method == WorkplaneConstructionCategory::Offset)
    {
        Plane.OriginX += Plane.NormalX * Plane.OffsetDistance;
        Plane.OriginY += Plane.NormalY * Plane.OffsetDistance;
        Plane.OriginZ += Plane.NormalZ * Plane.OffsetDistance;
    }
    else if (Plane.Method == WorkplaneConstructionCategory::Angle)
    {
        // 📝 Rotate the seeded frame by AngleDegrees about whichever OWN axis AnglePivot names, so a tilt is not locked to one direction:
        //    UAxis hinges V + Normal about U (tip forward / back, the drawing-board case); VAxis hinges U + Normal about V (tip left / right);
        //    NormalAxis hinges U + V about the Normal (an in-plane roll). Rodrigues about a UNIT axis (the seeded principal axes already are):
        //    v' = v·cos + (k × v)·sin + k·(k·v)·(1 - cos). Rotating each spanning vector keeps the frame orthonormal.
        const float AngleRadians = Plane.AngleDegrees * 3.14159265358979323846f / 180.0f;
        const float CosAngle = std::cos(AngleRadians);
        const float SinAngle = std::sin(AngleRadians);

        float Kx, Ky, Kz;   // the hinge axis
        switch (Plane.AnglePivot)
        {
            case WorkplaneAnglePivot::VAxis:      Kx = Plane.AxisVX;  Ky = Plane.AxisVY;  Kz = Plane.AxisVZ;  break;
            case WorkplaneAnglePivot::NormalAxis: Kx = Plane.NormalX; Ky = Plane.NormalY; Kz = Plane.NormalZ; break;
            case WorkplaneAnglePivot::UAxis:
            default:                              Kx = Plane.AxisUX;  Ky = Plane.AxisUY;  Kz = Plane.AxisUZ;  break;
        }

        // 📝 Rotate one vector (Vx,Vy,Vz) about the unit hinge (Kx,Ky,Kz) in place by the Rodrigues form above.
        auto RotateAboutHinge = [&](float& Vx, float& Vy, float& Vz)
        {
            const float Dot   = Kx * Vx + Ky * Vy + Kz * Vz;                       // k·v
            const float CrossX = Ky * Vz - Kz * Vy;                                // (k × v)
            const float CrossY = Kz * Vx - Kx * Vz;
            const float CrossZ = Kx * Vy - Ky * Vx;
            const float OneMinusCos = 1.0f - CosAngle;
            const float Rx = Vx * CosAngle + CrossX * SinAngle + Kx * Dot * OneMinusCos;
            const float Ry = Vy * CosAngle + CrossY * SinAngle + Ky * Dot * OneMinusCos;
            const float Rz = Vz * CosAngle + CrossZ * SinAngle + Kz * Dot * OneMinusCos;
            Vx = Rx; Vy = Ry; Vz = Rz;
        };

        // Rotate the two axes that are NOT the hinge (the hinge itself is invariant under its own rotation).
        if (Plane.AnglePivot != WorkplaneAnglePivot::UAxis)      RotateAboutHinge(Plane.AxisUX,  Plane.AxisUY,  Plane.AxisUZ);
        if (Plane.AnglePivot != WorkplaneAnglePivot::VAxis)      RotateAboutHinge(Plane.AxisVX,  Plane.AxisVY,  Plane.AxisVZ);
        if (Plane.AnglePivot != WorkplaneAnglePivot::NormalAxis) RotateAboutHinge(Plane.NormalX, Plane.NormalY, Plane.NormalZ);
    }

    // FlipNormalEnabled reverses which side is "up" — negate the normal and swap V's sense so the (u, v) frame stays right-handed.
    if (Plane.FlipNormalEnabled)
    {
        Plane.NormalX = -Plane.NormalX; Plane.NormalY = -Plane.NormalY; Plane.NormalZ = -Plane.NormalZ;
        Plane.AxisVX = -Plane.AxisVX; Plane.AxisVY = -Plane.AxisVY; Plane.AxisVZ = -Plane.AxisVZ;
    }

    NormalizeDirection(Plane.AxisUX, Plane.AxisUY, Plane.AxisUZ);
    NormalizeDirection(Plane.AxisVX, Plane.AxisVY, Plane.AxisVZ);
    NormalizeDirection(Plane.NormalX, Plane.NormalY, Plane.NormalZ);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTRUCT + APPEND
//------------------------------------------------------------------------------------------------------------------------

// 📝 A short reader label for a method — the seed for a plane's title + the history line ("Added Offset plane").
namespace
{
    const char* ResolveMethodLabel(WorkplaneConstructionCategory Method)
    {
        switch (Method)
        {
            case WorkplaneConstructionCategory::PrincipalXY: return "XY";
            case WorkplaneConstructionCategory::PrincipalXZ: return "XZ";
            case WorkplaneConstructionCategory::PrincipalYZ: return "YZ";
            case WorkplaneConstructionCategory::Offset:      return "Offset";
            case WorkplaneConstructionCategory::Angle:       return "Angle";
            case WorkplaneConstructionCategory::ThreePoint:  return "Three-Point";
            case WorkplaneConstructionCategory::Midplane:    return "Midplane";
            case WorkplaneConstructionCategory::Tangent:     return "Tangent";
            case WorkplaneConstructionCategory::PointNormal: return "Point-Normal";
            case WorkplaneConstructionCategory::OnFace:      return "On-Face";
        }
        return "Workplane";
    }
}

Workplane ConstructWorkplane(WorkplaneConstructionCategory Method)
{
    Workplane Plane;
    Plane.Method = Method;
    SolveWorkplaneFrame(Plane);
    return Plane;
}

uint32_t AppendWorkplane(WorkplaneStore& Store, WorkplaneConstructionCategory Method)
{
    Workplane Plane = ConstructWorkplane(Method);
    Plane.Identifier = Store.NextIdentifier++;
    std::snprintf(Plane.Title, sizeof(Plane.Title), "Workplane %u", Plane.Identifier);

    Store.Planes.push_back(Plane);
    Store.Selected = Plane.Identifier;

    char Label[48];
    std::snprintf(Label, sizeof(Label), "Added %s plane", ResolveMethodLabel(Method));
    AppendWorkplaneEdit(Store, Plane.Identifier, Label, "square-dashed-bottom", nullptr);
    return Plane.Identifier;
}

Workplane* ResolveWorkplane(WorkplaneStore& Store, uint32_t Identifier)
{
    if (Identifier == 0)
        return nullptr;
    for (Workplane& Plane : Store.Planes)
        if (Plane.Identifier == Identifier)
            return &Plane;
    return nullptr;
}

void DetachWorkplane(WorkplaneStore& Store, uint32_t Identifier)
{
    for (size_t Index = 0; Index < Store.Planes.size(); ++Index)
        if (Store.Planes[Index].Identifier == Identifier)
        {
            char Label[48];
            std::snprintf(Label, sizeof(Label), "Dropped %s", Store.Planes[Index].Title);
            Store.Planes.erase(Store.Planes.begin() + Index);
            if (Store.Selected == Identifier) Store.Selected = 0;
            if (Store.Hovered == Identifier)  Store.Hovered = 0;
            AppendWorkplaneEdit(Store, 0, Label, "trash-2", nullptr);
            return;
        }
}

void EnforceWorkplaneDefinition(WorkplaneStore& Store, uint32_t Identifier, const char* Label, const char* Glyph, const char* Detail)
{
    Workplane* Plane = ResolveWorkplane(Store, Identifier);
    if (Plane == nullptr)
        return;
    SolveWorkplaneFrame(*Plane);
    AppendWorkplaneEdit(Store, Identifier, Label, Glyph, Detail);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       EDIT LOG + HISTORY
//------------------------------------------------------------------------------------------------------------------------

void CaptureWorkplaneRevision(const WorkplaneStore& Store, WorkplaneRevision& OutRevision)
{
    OutRevision.Planes         = Store.Planes;
    OutRevision.NextIdentifier = Store.NextIdentifier;
    OutRevision.Selected       = Store.Selected;
}

void AppendWorkplaneEdit(WorkplaneStore& Store, uint32_t PlaneId, const char* Label, const char* Glyph, const char* Detail)
{
    // 📝 Discard any redoable tail so a fresh edit after an undo forks cleanly (LogCursor is the count of applied entries).
    if (Store.LogCursor >= 0 && (size_t)Store.LogCursor < Store.EditLog.size())
        Store.EditLog.erase(Store.EditLog.begin() + Store.LogCursor, Store.EditLog.end());

    WorkplaneHistoryEntry Entry;
    if (Label != nullptr)  std::snprintf(Entry.Label, sizeof(Entry.Label), "%s", Label);
    if (Glyph != nullptr)  std::snprintf(Entry.Glyph, sizeof(Entry.Glyph), "%s", Glyph);
    if (Detail != nullptr) std::snprintf(Entry.Detail, sizeof(Entry.Detail), "%s", Detail);
    Entry.PlaneId    = PlaneId;
    Entry.FlashLevel = 1.0f;
    CaptureWorkplaneRevision(Store, Entry.Revision);

    Store.EditLog.push_back(std::move(Entry));
    Store.LogCursor = (int)Store.EditLog.size();
}

void RestoreWorkplaneRevisionAt(WorkplaneStore& Store, int Step)
{
    if (Step < 0 || Step > (int)Store.EditLog.size() || Step == Store.LogCursor)
        return;

    if (Step == 0)
    {
        Store.Planes.clear();
        Store.NextIdentifier = 1;
        Store.Selected = 0;
    }
    else
    {
        const WorkplaneRevision& Revision = Store.EditLog[(size_t)Step - 1].Revision;
        Store.Planes         = Revision.Planes;
        Store.NextIdentifier = Revision.NextIdentifier;
        Store.Selected       = Revision.Selected;
    }

    Store.Hovered = 0;
    Store.LogCursor = Step;

    // Re-solve every restored plane's frame (snapshots carry the definition, not the transient solve — Workplane keeps its default frame).
    for (Workplane& Plane : Store.Planes)
        SolveWorkplaneFrame(Plane);

    // Clamp the selection to a surviving plane.
    if (Store.Selected != 0 && ResolveWorkplane(Store, Store.Selected) == nullptr)
        Store.Selected = 0;
}

bool UndoWorkplaneEdit(WorkplaneStore& Store)
{
    if (Store.LogCursor <= 0)
        return false;
    RestoreWorkplaneRevisionAt(Store, Store.LogCursor - 1);
    return true;
}

bool RedoWorkplaneEdit(WorkplaneStore& Store)
{
    if (Store.LogCursor >= (int)Store.EditLog.size())
        return false;
    RestoreWorkplaneRevisionAt(Store, Store.LogCursor + 1);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        RENDER ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fold a float's raw bits into a rolling FNV-1a hash so the body's Revision changes exactly when its geometry / tint does (the consumer
//    caches device buffers by revision and re-uploads only on change), mirroring the stroke-body change hash in ParametricSketchShapeStore.
namespace
{
    void FoldFloatBits(uint32_t& Hash, float Value)
    {
        uint32_t Bits = 0;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        for (int ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
        {
            Hash ^= (Bits >> (ByteIndex * 8)) & 0xFFu;
            Hash *= 16777619u;
        }
    }
}

void AssembleWorkplaneBodies(const WorkplaneStore& Store, std::vector<WorkplaneBody>& OutBodies)
{
    OutBodies.clear();

    for (const Workplane& Plane : Store.Planes)
    {
        if (!Plane.Displayed)
            continue;

        WorkplaneBody Body;
        Body.Identifier = Plane.Identifier;
        Body.NormalX = Plane.NormalX; Body.NormalY = Plane.NormalY; Body.NormalZ = Plane.NormalZ;
        for (int Channel = 0; Channel < 4; ++Channel)
            Body.ColourRGBA[Channel] = Plane.ColourRGBA[Channel];

        const float Extent = Plane.Extent;

        // 📝 The four sheet corners (world mm, CCW): Origin ± Extent along U and V. Corner c = Origin + su*Extent*U + sv*Extent*V.
        const float CornerSigns[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
        for (int CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
        {
            const float SignU = CornerSigns[CornerIndex][0] * Extent;
            const float SignV = CornerSigns[CornerIndex][1] * Extent;
            Body.Quad[CornerIndex * 3 + 0] = Plane.OriginX + Plane.AxisUX * SignU + Plane.AxisVX * SignV;
            Body.Quad[CornerIndex * 3 + 1] = Plane.OriginY + Plane.AxisUY * SignU + Plane.AxisVY * SignV;
            Body.Quad[CornerIndex * 3 + 2] = Plane.OriginZ + Plane.AxisUZ * SignU + Plane.AxisVZ * SignV;
        }

        // 📝 The on-plane minor-cell lattice: lines parallel to V stepped along U (and vice-versa) at GridSpacing, spanning ±Extent. Each
        //    segment appends two endpoints (6 floats). GridAxis picks which families draw; GridSpacing <= 0 is treated as "no grid".
        if (Plane.GridEnabled && Plane.GridSpacing > 1e-4f && Plane.GridAxis != WorkplaneGridAxis::None)
        {
            const int LineCount = (int)(Extent / Plane.GridSpacing);
            const bool DrawUFamily = (Plane.GridAxis == WorkplaneGridAxis::Cross || Plane.GridAxis == WorkplaneGridAxis::UAxis);
            const bool DrawVFamily = (Plane.GridAxis == WorkplaneGridAxis::Cross || Plane.GridAxis == WorkplaneGridAxis::VAxis);

            for (int Step = -LineCount; Step <= LineCount; ++Step)
            {
                const float Offset = (float)Step * Plane.GridSpacing;

                if (DrawUFamily) // lines parallel to V, stepped along U
                {
                    const float BaseX = Plane.OriginX + Plane.AxisUX * Offset;
                    const float BaseY = Plane.OriginY + Plane.AxisUY * Offset;
                    const float BaseZ = Plane.OriginZ + Plane.AxisUZ * Offset;
                    Body.GridLines.push_back(BaseX + Plane.AxisVX * -Extent);
                    Body.GridLines.push_back(BaseY + Plane.AxisVY * -Extent);
                    Body.GridLines.push_back(BaseZ + Plane.AxisVZ * -Extent);
                    Body.GridLines.push_back(BaseX + Plane.AxisVX * Extent);
                    Body.GridLines.push_back(BaseY + Plane.AxisVY * Extent);
                    Body.GridLines.push_back(BaseZ + Plane.AxisVZ * Extent);
                }

                if (DrawVFamily) // lines parallel to U, stepped along V
                {
                    const float BaseX = Plane.OriginX + Plane.AxisVX * Offset;
                    const float BaseY = Plane.OriginY + Plane.AxisVY * Offset;
                    const float BaseZ = Plane.OriginZ + Plane.AxisVZ * Offset;
                    Body.GridLines.push_back(BaseX + Plane.AxisUX * -Extent);
                    Body.GridLines.push_back(BaseY + Plane.AxisUY * -Extent);
                    Body.GridLines.push_back(BaseZ + Plane.AxisUZ * -Extent);
                    Body.GridLines.push_back(BaseX + Plane.AxisUX * Extent);
                    Body.GridLines.push_back(BaseY + Plane.AxisUY * Extent);
                    Body.GridLines.push_back(BaseZ + Plane.AxisUZ * Extent);
                }
            }
        }

        // Revision = a change hash over the sheet corners + tint + grid extent so the consumer re-uploads only when the sheet moves.
        uint32_t Revision = 2166136261u;
        for (int Index = 0; Index < 12; ++Index) FoldFloatBits(Revision, Body.Quad[Index]);
        for (int Index = 0; Index < 4;  ++Index) FoldFloatBits(Revision, Body.ColourRGBA[Index]);
        FoldFloatBits(Revision, (float)Body.GridLines.size());
        Body.Revision = Revision;

        OutBodies.push_back(std::move(Body));
    }
}

} // namespace Frontier
