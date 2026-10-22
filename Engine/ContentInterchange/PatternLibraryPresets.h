/*============================================================================================================================================
                                                            PATTERNLIBRARYPRESETS.H
============================================================================================================================================*/
// 🧩 Seamless starter patterns for the Pattern library gallery, built from retained vector primitives.

#pragma once
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "Engine/Render/VectorCoverage.h"
#include <cstdio>
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
    char Caption[64]{};
    const auto Motif = [&](uint32_t Category, float CentreX, float CentreY, float Width, float Height, size_t Ink, const char *Stem, int Ordinal)
    {
        VectorCoverageInstruction Source;
        Source.Topology[0] = Category;
        Source.Bounds = {CentreX, CentreY, Width, Height};
        Source.Colour = Palette[Ink % Palette.size()];
        Source.ColourEnd = Source.Colour;
        Source.StrokeColour = {.23f, .24f, .28f, 1};
        Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
        std::snprintf(Caption, sizeof(Caption), "%s %d", Stem, Ordinal);
        ConstructVectorAuthoringPrimitive(Document, Source, Caption);
    };
    int Ordinal = 0;
    switch (Index)
    {
    case 0:
        for (int Row = 0; Row < 4; ++Row)
            for (int Column = 0; Column < 4; ++Column)
                Motif(1, 64.f + 128.f * Column, 64.f + 128.f * Row, 88, 88, size_t((Row + Column) % 2), "Polka dot", ++Ordinal);
        break;
    case 1:
        for (int Row = 0; Row < 4; ++Row)
        {
            if (Row % 2 == 0)
                for (int Column = 0; Column < 4; ++Column)
                    Motif(1, 64.f + 128.f * Column, 64.f + 128.f * Row, 72, 72, 0, "Drop dot", ++Ordinal);
            else
            {
                Motif(1, 0, 64.f + 128.f * Row, 72, 72, 1, "Drop dot", ++Ordinal);
                for (int Column = 1; Column < 4; ++Column)
                    Motif(1, 128.f * Column, 64.f + 128.f * Row, 72, 72, 1, "Drop dot", ++Ordinal);
                Motif(1, 512, 64.f + 128.f * Row, 72, 72, 1, "Drop dot", ++Ordinal);
            }
        }
        break;
    case 2:
        for (int Column = 0; Column < 4; ++Column)
            Motif(0, 64.f + 128.f * Column, 256, 56, 512, size_t(Column % 2), "Stripe", ++Ordinal);
        break;
    case 3:
        for (int Row = 0; Row < 8; ++Row)
            for (int Column = 0; Column < 8; ++Column)
                if ((Row + Column) % 2 == 0)
                    Motif(0, 32.f + 64.f * Column, 32.f + 64.f * Row, 64, 64, size_t(((Row / 2) + Column) % 2), "Check", ++Ordinal);
        break;
    case 4:
        for (int Row = 0; Row < 4; ++Row)
            for (int Column = 0; Column < 4; ++Column)
                Motif(2, 64.f + 128.f * Column, 64.f + 128.f * Row, 104, 104, size_t((Row + Column) % 2), "Diamond", ++Ordinal);
        break;
    default:
        for (int Row = 0; Row < 4; ++Row)
            for (int Column = 0; Column < 4; ++Column)
                Motif(5, 64.f + 128.f * Column, 64.f + 128.f * Row, 92, 92, size_t((Row * 2 + Column) % 2), "Star", ++Ordinal);
        break;
    }
    return Document;
}
} // namespace Frontier
