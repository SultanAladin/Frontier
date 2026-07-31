/*==============================================================================================================================================
                                                         TOROIDALCLIPMAPFIELD.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the 3D voxel clipmap spine: radius-doubling configuration, the toroidal wrap, camera-cell resolution, per-level scroll
//    evaluation (new origin + exposed L-slabs), residency invalidation on scroll, and the relight-ramp stub advance. All addressing is integer
//    cell math on the CellCoordinate lattice; the only floating-point step is the camera-position-to-cell floor.

#include "EngineContext/SpatialAcceleration/ToroidalClipmapField.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Positive modulo: result in [0, Modulus) even for a negative Value. Modulus is always > 0 here (Resolution >= 1).
[[nodiscard]] int32_t WrapPositive(int32_t Value, int32_t Modulus)
{
    const int32_t Remainder = Value % Modulus;
    return (Remainder < 0) ? Remainder + Modulus : Remainder;
}

// Signed floor-divide of a metric coordinate by a cell edge → the world cell it lands in (floors toward negative infinity).
[[nodiscard]] int32_t FloorToCell(float Coordinate, float CellMetres)
{
    return (int32_t)std::floor(Coordinate / CellMetres);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConfigureClipmapField(ToroidalClipmapField& Field,
                           uint32_t              LevelCount,
                           uint32_t              Resolution,
                           float                 BaseCellMetres)
{
    const uint32_t ClampedLevels     = std::clamp(LevelCount, 1u, ClipmapMaximumLevelCount);
    const uint32_t ClampedResolution = std::max(Resolution, 1u);

    Field.LevelCount = ClampedLevels;
    Field.Levels.assign(ClampedLevels, ClipmapLevel{});

    const size_t CellsPerLevel = (size_t)ClampedResolution * ClampedResolution * ClampedResolution;
    float LevelCellMetres = BaseCellMetres;
    for (uint32_t LevelIndex = 0; LevelIndex < ClampedLevels; ++LevelIndex)
    {
        ClipmapLevel& Level = Field.Levels[LevelIndex];
        Level.Resolution    = ClampedResolution;
        Level.CellMetres    = LevelCellMetres;
        Level.ToroidalOrigin = CellCoordinate{};
        Level.OriginSeeded  = false;
        Level.ResidencyTable.assign(CellsPerLevel, (uint8_t)0);
        Level.RelightRamp.assign(CellsPerLevel, 0.0f);
        LevelCellMetres *= 2.0f;   // radius-doubling ladder
    }
}

CellCoordinate ResolvePhysicalCell(const ToroidalClipmapField& Field, uint32_t Level, CellCoordinate WorldCell)
{
    const ClipmapLevel& Window = Field.Levels[Level];
    const int32_t Modulus = (int32_t)Window.Resolution;
    return CellCoordinate{
        WrapPositive(WorldCell.XCell + Window.ToroidalOrigin.XCell, Modulus),
        WrapPositive(WorldCell.YCell + Window.ToroidalOrigin.YCell, Modulus),
        WrapPositive(WorldCell.ZCell + Window.ToroidalOrigin.ZCell, Modulus) };
}

uint32_t FlattenPhysicalCell(const ToroidalClipmapField& Field, uint32_t Level, CellCoordinate PhysicalCell)
{
    const int32_t Resolution = (int32_t)Field.Levels[Level].Resolution;
    return (uint32_t)(PhysicalCell.XCell + Resolution * (PhysicalCell.YCell + Resolution * PhysicalCell.ZCell));
}

CellCoordinate ResolveCameraCell(const ToroidalClipmapField& Field, uint32_t Level, Vector3f CameraPosition)
{
    const float CellMetres = Field.Levels[Level].CellMetres;
    return CellCoordinate{
        FloorToCell(CameraPosition.XCoord, CellMetres),
        FloorToCell(CameraPosition.YCoord, CellMetres),
        FloorToCell(CameraPosition.ZCoord, CellMetres) };
}

ClipmapScrollResult EvaluateClipmapScroll(const ToroidalClipmapField& Field, uint32_t Level, Vector3f CameraPosition)
{
    const ClipmapLevel&  Window     = Field.Levels[Level];
    const int32_t        Resolution = (int32_t)Window.Resolution;
    const int32_t        HalfSpan   = Resolution / 2;   // window is centred on the camera cell

    // 📝 The new origin re-centres the window: the physical [0,0,0] corner should map to the camera cell minus half the window, so the
    //    camera sits in the middle. Origin is expressed as the world cell mapped to the physical corner (negated in ResolvePhysicalCell's add).
    const CellCoordinate CameraCell = ResolveCameraCell(Field, Level, CameraPosition);
    const CellCoordinate NewCorner{ CameraCell.XCell - HalfSpan, CameraCell.YCell - HalfSpan, CameraCell.ZCell - HalfSpan };
    // ToroidalOrigin add-form wants -Corner so that (WorldCell + Origin) places the corner at physical 0.
    const CellCoordinate NewOrigin{ -NewCorner.XCell, -NewCorner.YCell, -NewCorner.ZCell };

    ClipmapScrollResult Outcome;
    Outcome.Level        = Level;
    Outcome.OriginBefore = Window.ToroidalOrigin;
    Outcome.OriginAfter  = NewOrigin;
    Outcome.CellShift    = CellCoordinate{
        NewOrigin.XCell - Window.ToroidalOrigin.XCell,
        NewOrigin.YCell - Window.ToroidalOrigin.YCell,
        NewOrigin.ZCell - Window.ToroidalOrigin.ZCell };

    // The window spans world cells [NewCorner, NewCorner + Resolution) per axis after the scroll.
    const CellCoordinate WindowMin = NewCorner;
    const CellCoordinate WindowMax{ NewCorner.XCell + Resolution, NewCorner.YCell + Resolution, NewCorner.ZCell + Resolution };

    // First seeding of the level: the whole window is exposed. One span covers it.
    if (!Window.OriginSeeded)
    {
        Outcome.ExposedSpanCount = 1;
        Outcome.ExposedSpans[0]  = ClipmapScrollSpan{ WindowMin, WindowMax };
        return Outcome;
    }

    // No movement in cell space → nothing exposed.
    if (Outcome.CellShift.XCell == 0 && Outcome.CellShift.YCell == 0 && Outcome.CellShift.ZCell == 0)
    {
        Outcome.ExposedSpanCount = 0;
        return Outcome;
    }

    // 📝 For each moved axis, the exposed slab is the strip of the NEW window that was NOT in the OLD window along that axis. The window's
    //    corner moved by CornerDelta = NewCorner - OldCorner (and OldCorner == -OldOrigin, since the origin is the negated corner). Moving
    //    +N along an axis exposes the top |N| cells of the new window (clamped to the window); moving -N exposes the bottom |N|. The slab is
    //    full-width on the two other axes (using the NEW window bounds), so overlap between per-axis slabs harmlessly re-invalidates corners.
    const CellCoordinate CornerDelta{
        WindowMin.XCell + Window.ToroidalOrigin.XCell,
        WindowMin.YCell + Window.ToroidalOrigin.YCell,
        WindowMin.ZCell + Window.ToroidalOrigin.ZCell };

    uint32_t SpanCursor = 0;
    const int32_t AxisDeltas[3] = { CornerDelta.XCell, CornerDelta.YCell, CornerDelta.ZCell };
    for (int32_t Axis = 0; Axis < 3; ++Axis)
    {
        const int32_t Delta = AxisDeltas[Axis];
        if (Delta == 0)
            continue;

        const int32_t Magnitude = std::min(std::abs(Delta), Resolution);
        ClipmapScrollSpan Span{ WindowMin, WindowMax };
        int32_t* SpanMin[3] = { &Span.MinimumCell.XCell, &Span.MinimumCell.YCell, &Span.MinimumCell.ZCell };
        int32_t* SpanMax[3] = { &Span.MaximumCell.XCell, &Span.MaximumCell.YCell, &Span.MaximumCell.ZCell };
        const int32_t WindowMinAxis[3] = { WindowMin.XCell, WindowMin.YCell, WindowMin.ZCell };
        const int32_t WindowMaxAxis[3] = { WindowMax.XCell, WindowMax.YCell, WindowMax.ZCell };

        if (Delta > 0)
        {
            // Window moved toward +axis: the newly-exposed strip is the top Magnitude cells of the new window.
            *SpanMin[Axis] = WindowMaxAxis[Axis] - Magnitude;
            *SpanMax[Axis] = WindowMaxAxis[Axis];
        }
        else
        {
            // Window moved toward -axis: the newly-exposed strip is the bottom Magnitude cells.
            *SpanMin[Axis] = WindowMinAxis[Axis];
            *SpanMax[Axis] = WindowMinAxis[Axis] + Magnitude;
        }

        Outcome.ExposedSpans[SpanCursor] = Span;
        ++SpanCursor;
    }
    Outcome.ExposedSpanCount = SpanCursor;
    return Outcome;
}

void IntegrateScrollResidency(ToroidalClipmapField& Field, const ClipmapScrollResult& ScrollOutcome)
{
    ClipmapLevel& Window = Field.Levels[ScrollOutcome.Level];
    Window.ToroidalOrigin = ScrollOutcome.OriginAfter;
    Window.OriginSeeded   = true;

    for (uint32_t SpanIndex = 0; SpanIndex < ScrollOutcome.ExposedSpanCount; ++SpanIndex)
    {
        const ClipmapScrollSpan& Span = ScrollOutcome.ExposedSpans[SpanIndex];
        for (int32_t ZCell = Span.MinimumCell.ZCell; ZCell < Span.MaximumCell.ZCell; ++ZCell)
            for (int32_t YCell = Span.MinimumCell.YCell; YCell < Span.MaximumCell.YCell; ++YCell)
                for (int32_t XCell = Span.MinimumCell.XCell; XCell < Span.MaximumCell.XCell; ++XCell)
                {
                    const CellCoordinate Physical = ResolvePhysicalCell(Field, ScrollOutcome.Level, CellCoordinate{ XCell, YCell, ZCell });
                    const uint32_t FlatIndex = FlattenPhysicalCell(Field, ScrollOutcome.Level, Physical);
                    Window.ResidencyTable[FlatIndex] = 0;
                    Window.RelightRamp[FlatIndex]    = 0.0f;
                }
    }
}

void AdvanceRelightRamp(ToroidalClipmapField& Field, float RampRate)
{
    for (uint32_t LevelIndex = 0; LevelIndex < Field.LevelCount; ++LevelIndex)
    {
        ClipmapLevel& Window = Field.Levels[LevelIndex];
        const size_t CellCount = Window.RelightRamp.size();
        for (size_t CellIndex = 0; CellIndex < CellCount; ++CellIndex)
        {
            if (Window.ResidencyTable[CellIndex] != 0)
                Window.RelightRamp[CellIndex] = std::min(1.0f, Window.RelightRamp[CellIndex] + RampRate);
        }
    }
}

void ActivateWorldCell(ToroidalClipmapField& Field, uint32_t Level, CellCoordinate WorldCell)
{
    const CellCoordinate Physical = ResolvePhysicalCell(Field, Level, WorldCell);
    const uint32_t FlatIndex = FlattenPhysicalCell(Field, Level, Physical);
    Field.Levels[Level].ResidencyTable[FlatIndex] = 1;
}

} // namespace Frontier
