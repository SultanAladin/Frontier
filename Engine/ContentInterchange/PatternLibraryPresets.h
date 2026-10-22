/*============================================================================================================================================
                                                            PATTERNLIBRARYPRESETS.H
============================================================================================================================================*/
// 🧩 Seamless starter patterns for the Pattern library gallery. Each preset is ONE retained motif
// expanded by the array generator (single shape, arrayed and scaled), never hand-stamped duplicates;
// alternating inks recolour the expanded copies in placement order.

#pragma once
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "Engine/Render/VectorCoverage.h"
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace Frontier
{
inline size_t QueryPatternLibraryPresetCount() noexcept { return 6; }

inline const char *QueryPatternLibraryPresetName(size_t Index)
{
    constexpr const char *Names[]{"Polka Dots", "Half Drop", "Stripes", "Checker", "Diamond Lattice", "Starfield"};
    if (Index >= QueryPatternLibraryPresetCount()) throw std::runtime_error("Unknown pattern library preset.");
    return Names[Index];
}

// 📝 Every motif stays inside the 512 tile except deliberate half-drop seam twins,
// so each preset repeats without visible discontinuities.
inline VectorAuthoringDocument ConstructPatternLibraryPreset(size_t Index)
{
    const char *Name = QueryPatternLibraryPresetName(Index);
    VectorAuthoringDocument Document;
    Document.Name = Name;
    const std::array<std::array<float, 4>, 2> Palette{{{.8627f, .4627f, .3529f, 1}, {.35f, .55f, .45f, 1}}};

    // 📝 One motif at the lattice centroid (array placements centre on the motif bounds), expanded
    // by GenerateVectorAuthoringArray; Ink recolours copies in row-major placement order.
    const auto Stamp = [&](VectorAuthoringDocument Base, uint32_t Category, float CentreX, float CentreY,
                           float Width, float Height, const char *Stem,
                           const VectorAuthoringArraySpecification &Specification, uint32_t Columns,
                           const std::function<size_t(uint32_t, uint32_t)> &Ink)
    {
        VectorCoverageInstruction Source;
        Source.Topology[0] = Category;
        Source.Bounds = {CentreX, CentreY, Width, Height};
        Source.Colour = Palette[0];
        Source.ColourEnd = Source.Colour;
        Source.StrokeColour = {.23f, .24f, .28f, 1};
        Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
        const uint32_t Token = ConstructVectorAuthoringPrimitive(Base, Source, Stem);
        auto Result = GenerateVectorAuthoringArray(Base, std::span<const uint32_t>{&Token, 1}, Specification);
        for (size_t Copy = 0; Copy < Result.Selection.size(); ++Copy)
        {
            const size_t Pick = Ink(uint32_t(Copy / Columns), uint32_t(Copy % Columns)) % Palette.size();
            for (auto &Layer : Result.Document.Layers)
                if (Layer.Token == Result.Selection[Copy])
                {
                    auto &Instruction = Result.Document.Instructions[Layer.Root];
                    Instruction.Colour = Palette[Pick];
                    Instruction.ColourEnd = Palette[Pick];
                }
        }
        return std::move(Result.Document);
    };
    const auto Grid = [](uint32_t Columns, uint32_t Rows, float StepX, float StepY)
    {
        VectorAuthoringArraySpecification Specification;
        Specification.Category = VectorAuthoringArrayCategory::Grid;
        Specification.Columns = Columns;
        Specification.Rows = Rows;
        Specification.StepX = StepX;
        Specification.StepY = StepY;
        return Specification;
    };

    switch (Index)
    {
    case 0: // Polka Dots: 4x4 dots, checkerboard ink.
        return Stamp(std::move(Document), 1, 256, 256, 88, 88, "Polka dot", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t Column) { return size_t((Row + Column) % 2); });
    case 1: // Half Drop: even rows on the 128 lattice, odd rows shifted with seam twins at x 0/512.
        Document = Stamp(std::move(Document), 1, 256, 192, 72, 72, "Drop dot", Grid(4, 2, 128, 256), 4,
            [](uint32_t, uint32_t) { return size_t(0); });
        return Stamp(std::move(Document), 1, 256, 320, 72, 72, "Drop dot", Grid(5, 2, 128, 256), 5,
            [](uint32_t, uint32_t) { return size_t(1); });
    case 2: // Stripes: one bar in a 4-step linear array.
    {
        VectorAuthoringArraySpecification Specification;
        Specification.Category = VectorAuthoringArrayCategory::Linear;
        Specification.Count = 4;
        Specification.StepX = 128;
        Specification.StepY = 0;
        return Stamp(std::move(Document), 0, 256, 256, 56, 512, "Stripe", Specification, 4,
            [](uint32_t, uint32_t Column) { return size_t(Column % 2); });
    }
    case 3: // Checker: the two interleaved 128 sublattices of the 64 board.
        Document = Stamp(std::move(Document), 0, 224, 224, 64, 64, "Check", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t) { return size_t(Row % 2); });
        return Stamp(std::move(Document), 0, 288, 288, 64, 64, "Check", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t) { return size_t((Row + 1) % 2); });
    case 4: // Diamond Lattice: 4x4 diamonds, checkerboard ink.
        return Stamp(std::move(Document), 2, 256, 256, 104, 104, "Diamond", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t Column) { return size_t((Row + Column) % 2); });
    default: // Starfield: 4x4 stars, column ink.
        return Stamp(std::move(Document), 5, 256, 256, 92, 92, "Star", Grid(4, 4, 128, 128), 4,
            [](uint32_t Row, uint32_t Column) { return size_t((Row * 2 + Column) % 2); });
    }
}
} // namespace Frontier
