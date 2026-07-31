/*==============================================================================================================================================
                                                          SUNSHADOWCLIPMAP.CPP
==============================================================================================================================================*/
// The light-space scrolling window behind the sun shadows. Basis solve, per-level re-centring, exposed-strip residency invalidation, and the
// tile addressing the page allocator (P6.2) and depth raster (P6.4) will index through. See the header for the light-space ruling.

#include "Graphics/Shadow/SunShadowClipmap.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

// The light window is centred on the observer, so the physical corner sits half a window away from them on both axes.
[[nodiscard]] TileCoordinate SolveCenteredOrigin(const SunShadowLevel& Level, TileCoordinate ObserverTile)
{
    const int32_t HalfSpan = (int32_t)Level.Resolution / 2;
    // ToroidalOrigin is stored in ADD form: (LightTile + Origin) must place the window's corner at physical 0, so it is the negated corner.
    return TileCoordinate{ HalfSpan - ObserverTile.XTile, HalfSpan - ObserverTile.YTile };
}

// The light-space XY of a world point, dropping the along-light depth the tile lattice does not address.
void ProjectToLightPlane(const SunShadowBasis& Basis, Vector3f WorldPosition, float& OutX, float& OutY)
{
    OutX = DotVector(WorldPosition, Basis.RightAxis);
    OutY = DotVector(WorldPosition, Basis.UpAxis);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                            BASIS
//------------------------------------------------------------------------------------------------------------------------

SunShadowBasis SolveSunShadowBasis(Vector3f SolarDirection)
{
    SunShadowBasis Basis;

    // Forward runs ALONG the light's travel, i.e. away from the sun, so light-space depth increases with distance from it.
    const Vector3f ToSun = NormalizeVector(SolarDirection);
    Basis.ForwardAxis = ScaleVector(ToSun, -1.0f);

    // 🔴 Gram-Schmidt against world +Z, EXCEPT when the sun is essentially overhead — there ToSun is parallel to +Z, the cross product is the
    //    zero vector, and normalizing it yields NaN. That NaN would propagate into every tile coordinate, so the shadows would disappear at
    //    exactly local noon and read as a shadow-math failure rather than a degenerate basis. Falling back to +X keeps the frame orthonormal;
    //    which reference axis is used is arbitrary, since any frame perpendicular to a vertical sun is equally valid.
    const Vector3f WorldUp{ 0.0f, 0.0f, 1.0f };
    const float    VerticalAlignment = std::fabs(DotVector(ToSun, WorldUp));

    const Vector3f Reference = (VerticalAlignment > 1.0f - 1e-3f) ? Vector3f{ 1.0f, 0.0f, 0.0f } : WorldUp;

    Basis.RightAxis = NormalizeVector(CrossVector(Reference, Basis.ForwardAxis));
    Basis.UpAxis    = NormalizeVector(CrossVector(Basis.ForwardAxis, Basis.RightAxis));
    return Basis;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

void InitializeSunShadowClipmap(SunShadowClipmap& Clipmap, uint32_t LevelCount, uint32_t Resolution, float BaseTileMetres)
{
    const uint32_t BoundedLevels = std::min(std::max(LevelCount, 1u), ShadowTilemapLodCount);

    Clipmap.Levels.assign(BoundedLevels, SunShadowLevel{});

    float TileMetres = BaseTileMetres;
    for (uint32_t LevelIterator = 0; LevelIterator < BoundedLevels; ++LevelIterator)
    {
        SunShadowLevel& Level = Clipmap.Levels[LevelIterator];
        Level.Resolution   = Resolution;
        Level.TileMetres   = TileMetres;
        Level.OriginSeeded = false;
        Level.ToroidalOrigin = TileCoordinate{};
        // Every tile starts vacant: nothing has been rendered into any page yet, and claiming residency here would let the first image sample
        // pages that hold the atlas clear value rather than depth.
        Level.ResidencyTable.assign((size_t)Resolution * (size_t)Resolution, (uint8_t)0);

        TileMetres *= 2.0f;   // radius-doubling ladder
    }

    Clipmap.SolarDirectionSeeded = false;
    Clipmap.ReadyCondition       = !Clipmap.Levels.empty();
}

void FinalizeSunShadowClipmap(SunShadowClipmap& Clipmap)
{
    Clipmap.Levels.clear();
    Clipmap.SolarDirectionSeeded = false;
    Clipmap.ReadyCondition       = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

TileCoordinate ResolveSunShadowTile(const SunShadowClipmap& Clipmap, uint32_t Level, Vector3f WorldPosition)
{
    if (Level >= Clipmap.Levels.size())
        return TileCoordinate{};

    const SunShadowLevel& Window = Clipmap.Levels[Level];
    float LightX = 0.0f;
    float LightY = 0.0f;
    ProjectToLightPlane(Clipmap.Basis, WorldPosition, LightX, LightY);

    return TileCoordinate{ FloorToLatticeCell(LightX, Window.TileMetres),
                           FloorToLatticeCell(LightY, Window.TileMetres) };
}

TileCoordinate ResolveSunShadowPhysicalTile(const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    if (Level >= Clipmap.Levels.size())
        return TileCoordinate{};

    const SunShadowLevel& Window  = Clipmap.Levels[Level];
    const int32_t         Modulus = (int32_t)Window.Resolution;

    return TileCoordinate{ WrapToroidalIndex(LightTile.XTile + Window.ToroidalOrigin.XTile, Modulus),
                           WrapToroidalIndex(LightTile.YTile + Window.ToroidalOrigin.YTile, Modulus) };
}

uint32_t ResolveSunShadowTileIndex(const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    if (Level >= Clipmap.Levels.size())
        return 0;

    const SunShadowLevel& Window   = Clipmap.Levels[Level];
    const TileCoordinate  Physical = ResolveSunShadowPhysicalTile(Clipmap, Level, LightTile);
    return (uint32_t)Physical.YTile * Window.Resolution + (uint32_t)Physical.XTile;
}

bool SunShadowTileResident(const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    if (Level >= Clipmap.Levels.size())
        return false;

    const SunShadowLevel& Window = Clipmap.Levels[Level];
    const uint32_t        Index  = ResolveSunShadowTileIndex(Clipmap, Level, LightTile);
    return Index < Window.ResidencyTable.size() && Window.ResidencyTable[Index] != 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           SCROLL
//------------------------------------------------------------------------------------------------------------------------

SunShadowScrollResult EvaluateSunShadowScroll(const SunShadowClipmap& Clipmap,
                                              uint32_t                Level,
                                              Vector3f                ObserverPosition,
                                              Vector3f                SolarDirection)
{
    SunShadowScrollResult Outcome;
    Outcome.Level = Level;
    if (Level >= Clipmap.Levels.size())
        return Outcome;

    const SunShadowLevel& Window     = Clipmap.Levels[Level];
    const int32_t         Resolution = (int32_t)Window.Resolution;

    const TileCoordinate ObserverTile = ResolveSunShadowTile(Clipmap, Level, ObserverPosition);
    const TileCoordinate NewOrigin    = SolveCenteredOrigin(Window, ObserverTile);

    Outcome.OriginBefore = Window.ToroidalOrigin;
    Outcome.OriginAfter  = NewOrigin;
    Outcome.TileShift    = TileCoordinate{ NewOrigin.XTile - Window.ToroidalOrigin.XTile,
                                           NewOrigin.YTile - Window.ToroidalOrigin.YTile };

    // 🔴 The rotation test is a SEPARATE, COARSER path than scrolling, and must not be folded into it. A scroll assumes the lattice basis is
    //    unchanged and only the origin moved — that is what makes retained tiles reusable. When the sun rotates, every tile's WORLD footprint
    //    changes even if the origin does not move at all, so the strip solve would report "nothing exposed" while every cached page silently
    //    describes the wrong region. The threshold is deliberately tight: shadows from a stale basis look plausible, not obviously broken.
    if (Clipmap.SolarDirectionSeeded)
    {
        const float Alignment = DotVector(NormalizeVector(SolarDirection), NormalizeVector(Clipmap.SolarDirectionPrevious));
        if (Alignment < std::cos(1e-4f))
            Outcome.WholeWindowDirtyCondition = true;
    }

    // First seeding: nothing was ever rendered, so treat the level as wholly dirty rather than reporting a strip.
    if (!Window.OriginSeeded)
    {
        Outcome.WholeWindowDirtyCondition = true;
        return Outcome;
    }

    if (Outcome.WholeWindowDirtyCondition)
        return Outcome;   // strips are meaningless once the basis moved — the caller invalidates everything

    if (Outcome.TileShift.XTile == 0 && Outcome.TileShift.YTile == 0)
        return Outcome;   // stationary in tile space: nothing exposed

    // 📝 The window's corner in LIGHT-tile space is the negated origin, so the corner moved by the negated shift. The exposed strip is the part
    //    of the NEW window that the OLD one did not cover along each moved axis.
    const int32_t WindowMinimumX = -NewOrigin.XTile;
    const int32_t WindowMinimumY = -NewOrigin.YTile;
    const int32_t CornerDeltaX   = -Outcome.TileShift.XTile;
    const int32_t CornerDeltaY   = -Outcome.TileShift.YTile;

    ToroidalStrip Strip;
    if (SolveToroidalStrip(0, CornerDeltaX, WindowMinimumX, Resolution, Strip))
        Outcome.ExposedStrips[Outcome.ExposedStripCount++] = Strip;
    if (SolveToroidalStrip(1, CornerDeltaY, WindowMinimumY, Resolution, Strip))
        Outcome.ExposedStrips[Outcome.ExposedStripCount++] = Strip;

    return Outcome;
}

void IntegrateSunShadowResidency(SunShadowClipmap& Clipmap, const SunShadowScrollResult& Scroll)
{
    if (Scroll.Level >= Clipmap.Levels.size())
        return;

    SunShadowLevel& Window     = Clipmap.Levels[Scroll.Level];
    const int32_t   Resolution = (int32_t)Window.Resolution;

    // ⚠️ The origin advances BEFORE residency is touched: every wrap below must address the tile lattice as it is AFTER the scroll, since that
    //    is the arrangement the newly-exposed tiles will be rendered into.
    Window.ToroidalOrigin = Scroll.OriginAfter;
    Window.OriginSeeded   = true;

    if (Scroll.WholeWindowDirtyCondition)
    {
        std::fill(Window.ResidencyTable.begin(), Window.ResidencyTable.end(), (uint8_t)0);
        return;
    }

    // The window now spans light tiles [-Origin, -Origin + Resolution) on each axis; a strip is that range narrowed on its own axis.
    const int32_t WindowMinimumX = -Window.ToroidalOrigin.XTile;
    const int32_t WindowMinimumY = -Window.ToroidalOrigin.YTile;

    for (uint32_t StripIterator = 0; StripIterator < Scroll.ExposedStripCount; ++StripIterator)
    {
        const ToroidalStrip& Strip = Scroll.ExposedStrips[StripIterator];

        // The moved axis takes the strip's narrowed range; the other axis stays at the window's full extent.
        const int32_t MinimumX = (Strip.Axis == 0) ? Strip.MinimumCell : WindowMinimumX;
        const int32_t MaximumX = (Strip.Axis == 0) ? Strip.MaximumCell : WindowMinimumX + Resolution;
        const int32_t MinimumY = (Strip.Axis == 1) ? Strip.MinimumCell : WindowMinimumY;
        const int32_t MaximumY = (Strip.Axis == 1) ? Strip.MaximumCell : WindowMinimumY + Resolution;

        for (int32_t YTile = MinimumY; YTile < MaximumY; ++YTile)
        {
            for (int32_t XTile = MinimumX; XTile < MaximumX; ++XTile)
            {
                const uint32_t Index = ResolveSunShadowTileIndex(Clipmap, Scroll.Level, TileCoordinate{ XTile, YTile });
                if (Index < Window.ResidencyTable.size())
                    Window.ResidencyTable[Index] = 0;
            }
        }
    }
}

void RefreshSunShadowClipmap(SunShadowClipmap& Clipmap, Vector3f ObserverPosition, Vector3f SolarDirection)
{
    if (!Clipmap.ReadyCondition || Clipmap.Levels.empty())
        return;

    // ⚠️ ORDER: every level is evaluated and committed against the basis of the PREVIOUS image, because the rotation test compares this image's
    //    sun against the recorded one. Refreshing the basis first would make that comparison trivially true and the whole-window invalidation
    //    would never fire — the shadows would then be rendered through a rotated basis using pages cached under the old one.
    for (uint32_t LevelIterator = 0; LevelIterator < (uint32_t)Clipmap.Levels.size(); ++LevelIterator)
    {
        const SunShadowScrollResult Scroll = EvaluateSunShadowScroll(Clipmap, LevelIterator, ObserverPosition, SolarDirection);
        IntegrateSunShadowResidency(Clipmap, Scroll);
    }

    Clipmap.Basis                  = SolveSunShadowBasis(SolarDirection);
    Clipmap.SolarDirectionPrevious = NormalizeVector(SolarDirection);
    Clipmap.SolarDirectionSeeded   = true;
}

} // namespace Frontier
