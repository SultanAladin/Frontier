/*==============================================================================================================================================
                                                        CLIPMAPFIELDVALIDATION.CPP
==============================================================================================================================================*/
// 🧩 The P5c exit gate: proves the ToroidalClipmapField scroll + wrap are unit-correct IN ISOLATION (no Vulkan). It asserts the toroidal wrap
//    is a bijection over the physical range, that a stationary camera exposes nothing, that a one-cell scroll exposes exactly one L-slab of
//    the right thickness on the moved axis, that retained cells keep residency while only the exposed slab is invalidated, and that the
//    relight-ramp stub climbs on resident cells and stays zero on vacant ones. Prints a PASS/FAIL line per check and exits non-zero on any
//    failure so the Build.bat surfaces a regression.

#include "EngineContext/SpatialAcceleration/ToroidalClipmapField.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Frontier;

namespace
{

int FailureCount = 0;

void ReportCheck(const char* Description, bool Condition)
{
    std::printf("  [%s] %s\n", Condition ? "PASS" : "FAIL", Description);
    if (!Condition)
        ++FailureCount;
}

// Count resident cells across one level (dense physical array).
uint32_t CountResident(const ClipmapLevel& Window)
{
    uint32_t Total = 0;
    for (uint8_t Residency : Window.ResidencyTable)
        Total += (Residency != 0) ? 1u : 0u;
    return Total;
}

// -- Check 1: the toroidal wrap is a bijection over [0, Resolution)³ ----------------------------------------------------
void CheckWrapBijection()
{
    std::printf("check: toroidal wrap is a bijection\n");
    ToroidalClipmapField Field;
    ConfigureClipmapField(Field, 1, 8, 1.0f);
    // Push a non-trivial (and negative-inducing) origin.
    Field.Levels[0].ToroidalOrigin = CellCoordinate{ 5, -3, 100 };

    const int32_t Resolution = 8;
    std::vector<uint8_t> Hit((size_t)Resolution * Resolution * Resolution, 0);
    bool RangeOk = true;
    for (int32_t Z = -Resolution; Z < 2 * Resolution; ++Z)
        for (int32_t Y = -Resolution; Y < 2 * Resolution; ++Y)
            for (int32_t X = -Resolution; X < 2 * Resolution; ++X)
            {
                const CellCoordinate Physical = ResolvePhysicalCell(Field, 0, CellCoordinate{ X, Y, Z });
                RangeOk = RangeOk
                    && Physical.XCell >= 0 && Physical.XCell < Resolution
                    && Physical.YCell >= 0 && Physical.YCell < Resolution
                    && Physical.ZCell >= 0 && Physical.ZCell < Resolution;
            }
    ReportCheck("every wrapped cell lands in [0, Resolution)", RangeOk);

    // A full Resolution³ block of DISTINCT world cells must hit every physical cell exactly once.
    for (int32_t Z = 0; Z < Resolution; ++Z)
        for (int32_t Y = 0; Y < Resolution; ++Y)
            for (int32_t X = 0; X < Resolution; ++X)
            {
                const CellCoordinate Physical = ResolvePhysicalCell(Field, 0, CellCoordinate{ X, Y, Z });
                Hit[FlattenPhysicalCell(Field, 0, Physical)] += 1;
            }
    bool BijectionOk = true;
    for (uint8_t HitCount : Hit)
        BijectionOk = BijectionOk && (HitCount == 1);
    ReportCheck("a full window maps one-to-one onto the physical range", BijectionOk);
}

// -- Check 2: radius-doubling ladder --------------------------------------------------------------------------------------
void CheckRadiusDoubling()
{
    std::printf("check: radius-doubling cell ladder\n");
    ToroidalClipmapField Field;
    ConfigureClipmapField(Field, 4, 32, 1.0f);
    ReportCheck("level count == 4", Field.LevelCount == 4);
    ReportCheck("level 0 cell == 1 m", Field.Levels[0].CellMetres == 1.0f);
    ReportCheck("level 1 cell == 2 m", Field.Levels[1].CellMetres == 2.0f);
    ReportCheck("level 2 cell == 4 m", Field.Levels[2].CellMetres == 4.0f);
    ReportCheck("level 3 cell == 8 m", Field.Levels[3].CellMetres == 8.0f);
}

// -- Check 3: stationary camera exposes nothing after the initial seed ---------------------------------------------------
void CheckStationaryNoScroll()
{
    std::printf("check: stationary camera exposes no slab\n");
    ToroidalClipmapField Field;
    ConfigureClipmapField(Field, 1, 16, 1.0f);

    // Seed: first evaluation exposes the whole window.
    const Vector3f CameraPosition{ 40.0f, 40.0f, 40.0f };
    ClipmapScrollResult Seed = EvaluateClipmapScroll(Field, 0, CameraPosition);
    ReportCheck("first seed exposes exactly one (full-window) span", Seed.ExposedSpanCount == 1);
    IntegrateScrollResidency(Field, Seed);

    // Same position again → no movement, no exposure.
    ClipmapScrollResult Still = EvaluateClipmapScroll(Field, 0, CameraPosition);
    ReportCheck("same position yields zero shift", Still.CellShift.XCell == 0 && Still.CellShift.YCell == 0 && Still.CellShift.ZCell == 0);
    ReportCheck("same position exposes no span", Still.ExposedSpanCount == 0);
}

// -- Check 4: a one-cell scroll exposes exactly one L-slab of thickness 1 ------------------------------------------------
void CheckSingleCellScroll()
{
    std::printf("check: one-cell scroll exposes a single 1-thick slab\n");
    ToroidalClipmapField Field;
    ConfigureClipmapField(Field, 1, 16, 1.0f);
    const int32_t Resolution = 16;

    // Seed centred at origin, then fill every cell resident so we can watch invalidation.
    Vector3f CameraPosition{ 0.5f, 0.5f, 0.5f };
    ClipmapScrollResult Seed = EvaluateClipmapScroll(Field, 0, CameraPosition);
    IntegrateScrollResidency(Field, Seed);
    for (int32_t Z = -Resolution; Z < Resolution; ++Z)
        for (int32_t Y = -Resolution; Y < Resolution; ++Y)
            for (int32_t X = -Resolution; X < Resolution; ++X)
                ActivateWorldCell(Field, 0, CellCoordinate{ X, Y, Z });
    const uint32_t ResidentBefore = CountResident(Field.Levels[0]);
    ReportCheck("window fully resident before scroll", ResidentBefore == (uint32_t)(Resolution * Resolution * Resolution));

    // Move the camera +1 cell along X only.
    CameraPosition.XCoord += 1.0f;
    ClipmapScrollResult Scroll = EvaluateClipmapScroll(Field, 0, CameraPosition);
    ReportCheck("one span exposed", Scroll.ExposedSpanCount == 1);
    const ClipmapScrollSpan& Span = Scroll.ExposedSpans[0];
    const int32_t ThicknessX = Span.MaximumCell.XCell - Span.MinimumCell.XCell;
    const int32_t ThicknessY = Span.MaximumCell.YCell - Span.MinimumCell.YCell;
    const int32_t ThicknessZ = Span.MaximumCell.ZCell - Span.MinimumCell.ZCell;
    ReportCheck("slab is 1 cell thick on the moved axis (X)", ThicknessX == 1);
    ReportCheck("slab is full-width on Y", ThicknessY == Resolution);
    ReportCheck("slab is full-width on Z", ThicknessZ == Resolution);

    IntegrateScrollResidency(Field, Scroll);
    const uint32_t ResidentAfter = CountResident(Field.Levels[0]);
    // Exactly one Resolution² slab (the exposed strip) is invalidated.
    ReportCheck("exactly one slab-worth of cells invalidated", ResidentBefore - ResidentAfter == (uint32_t)(Resolution * Resolution));
}

// -- Check 5: retained cells keep residency; the relight ramp climbs only on resident cells ------------------------------
void CheckResidencyAndRamp()
{
    std::printf("check: residency retention + relight ramp\n");
    ToroidalClipmapField Field;
    ConfigureClipmapField(Field, 1, 8, 1.0f);
    Vector3f CameraPosition{ 0.5f, 0.5f, 0.5f };
    ClipmapScrollResult Seed = EvaluateClipmapScroll(Field, 0, CameraPosition);
    IntegrateScrollResidency(Field, Seed);

    // Activate a single interior cell and ramp it.
    ActivateWorldCell(Field, 0, CellCoordinate{ 0, 0, 0 });
    AdvanceRelightRamp(Field, 0.25f);
    const CellCoordinate Physical = ResolvePhysicalCell(Field, 0, CellCoordinate{ 0, 0, 0 });
    const uint32_t FlatIndex = FlattenPhysicalCell(Field, 0, Physical);
    ReportCheck("resident cell ramp climbed to 0.25", Field.Levels[0].RelightRamp[FlatIndex] == 0.25f);

    // A vacant neighbour stays at zero.
    const CellCoordinate VacantPhysical = ResolvePhysicalCell(Field, 0, CellCoordinate{ 1, 0, 0 });
    const uint32_t VacantIndex = FlattenPhysicalCell(Field, 0, VacantPhysical);
    ReportCheck("vacant cell ramp stays 0", Field.Levels[0].RelightRamp[VacantIndex] == 0.0f);

    // Ramp clamps at 1.
    for (int32_t Step = 0; Step < 10; ++Step)
        AdvanceRelightRamp(Field, 0.25f);
    ReportCheck("resident cell ramp clamps at 1", Field.Levels[0].RelightRamp[FlatIndex] == 1.0f);
}

} // namespace

int main()
{
    std::printf("ToroidalClipmapField — P5c unit-correctness validation\n\n");
    CheckWrapBijection();
    CheckRadiusDoubling();
    CheckStationaryNoScroll();
    CheckSingleCellScroll();
    CheckResidencyAndRamp();

    std::printf("\n%s (%d failure%s)\n", FailureCount == 0 ? "ALL CHECKS PASSED" : "VALIDATION FAILED",
                FailureCount, FailureCount == 1 ? "" : "s");
    return FailureCount == 0 ? 0 : 1;
}
