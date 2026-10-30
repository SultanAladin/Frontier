/*============================================================================================================================================
                                                       VECTORGRADIENTSTOPS.H
============================================================================================================================================*/
// 🧩 Shared CPU-side helpers for reading/writing a shape's up-to-4-stop gradient ramp (Engine/Render/VectorCoverage.h's
// GradientStopPositions/GradientStopColour2/GradientStopColour3), so StampWorkspacePanel and PatternWorkspacePanel
// author identical, already-validated stop layouts instead of hand-rolling the packed-slot bookkeeping twice.

#pragma once
#include "VectorAuthoringSelectionEditing.h"
#include <algorithm>
#include <vector>

namespace Frontier
{
inline constexpr int VectorGradientStopCapacity = 4;

struct VectorGradientStop
{
    float Position = 0.f;
    std::array<float, 4> Colour{1, 1, 1, 1};
};

// 📝 Unpacks the instruction's 4 fixed slots (Colour, ColourEnd, GradientStopColour2/3) into an ordinary,
// already-ascending list — stopping at the first inactive (-1) position, per the struct's documented contract.
inline std::vector<VectorGradientStop> QueryVectorGradientStops(const VectorCoverageInstruction &Source)
{
    const std::array<std::array<float, 4>, 4> Colours{Source.Colour, Source.ColourEnd, Source.GradientStopColour2, Source.GradientStopColour3};
    std::vector<VectorGradientStop> Stops;
    for (int Index = 0; Index < VectorGradientStopCapacity; ++Index)
    {
        const float Position = Source.GradientStopPositions[Index];
        if (Position < 0.f) break;
        Stops.push_back({Position, Colours[Index]});
    }
    if (Stops.size() < 2) Stops = {{0.f, Source.Colour}, {1.f, Source.ColourEnd}};
    return Stops;
}

// 📝 Packs a 2–4 entry, ascending-by-Position stop list back into every slot-shaped patch field a layout
// change can touch. Callers own sorting/clamping/count limits beforehand (see StampWorkspacePanel.cpp's
// gradient marker editing for the add/remove/drag call sites).
inline void ApplyVectorGradientStops(VectorAuthoringSourcePatch &Patch, const std::vector<VectorGradientStop> &Stops)
{
    std::array<float, 4> Positions{-1, -1, -1, -1};
    std::array<std::array<float, 4>, 4> Colours{std::array<float, 4>{1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}};
    const size_t Count = std::clamp<size_t>(Stops.size(), 2, VectorGradientStopCapacity);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        Positions[Index] = std::clamp(Stops[Index].Position, 0.f, 1.f);
        Colours[Index] = Stops[Index].Colour;
    }
    Patch.GradientStopPositions = Positions;
    Patch.Colour = Colours[0];
    Patch.ColourEnd = Colours[1];
    Patch.GradientStopColour2 = Colours[2];
    Patch.GradientStopColour3 = Colours[3];
}

// The stroke ramp has independent slots but uses the same editor and interpolation rules.
inline VectorCoverageInstruction QueryVectorStrokeGradientSource(const VectorCoverageInstruction &Source)
{
    auto Ramp = Source;
    Ramp.Colour = (Source.Topology[3] & VectorCoverageStrokeColourExplicit) ? Source.StrokeColour : Source.Colour;
    Ramp.ColourEnd = Source.StrokeColourEnd;
    Ramp.GradientStopPositions = Source.StrokeGradientStopPositions;
    Ramp.GradientStopColour2 = Source.StrokeGradientStopColour2;
    Ramp.GradientStopColour3 = Source.StrokeGradientStopColour3;
    Ramp.GradientAxis = Source.StrokeGradientAxis;
    Ramp.Transform[3] = Source.ShapeParameters[1];
    return Ramp;
}
inline std::vector<VectorGradientStop> QueryVectorStrokeGradientStops(const VectorCoverageInstruction &Source)
{
    return QueryVectorGradientStops(QueryVectorStrokeGradientSource(Source));
}
inline void ApplyVectorStrokeGradientStops(VectorAuthoringSourcePatch &Patch, const std::vector<VectorGradientStop> &Stops)
{
    VectorAuthoringSourcePatch Ramp;
    ApplyVectorGradientStops(Ramp, Stops);
    Patch.StrokeColour = Ramp.Colour; Patch.StrokeColourEnd = Ramp.ColourEnd;
    Patch.StrokeGradientStopPositions = Ramp.GradientStopPositions;
    Patch.StrokeGradientStopColour2 = Ramp.GradientStopColour2; Patch.StrokeGradientStopColour3 = Ramp.GradientStopColour3;
}

// 📝 Linear interpolation across whatever stops bracket Fraction — a CPU mirror of the GPU shader's
// EvaluateGradientColour, used so the Inspector's ramp preview bar matches the baked render exactly.
inline std::array<float, 4> EvaluateVectorGradientColour(const std::vector<VectorGradientStop> &Stops, float Fraction)
{
    if (Stops.empty()) return {1, 1, 1, 1};
    if (Fraction <= Stops.front().Position) return Stops.front().Colour;
    if (Fraction >= Stops.back().Position) return Stops.back().Colour;
    for (size_t Index = 0; Index + 1 < Stops.size(); ++Index)
    {
        const auto &A = Stops[Index]; const auto &B = Stops[Index + 1];
        if (Fraction > B.Position) continue;
        const float Span = std::max(B.Position - A.Position, 1e-5f);
        const float Local = std::clamp((Fraction - A.Position) / Span, 0.f, 1.f);
        std::array<float, 4> Result{};
        for (int Channel = 0; Channel < 4; ++Channel) Result[Channel] = A.Colour[Channel] + (B.Colour[Channel] - A.Colour[Channel]) * Local;
        return Result;
    }
    return Stops.back().Colour;
}
}
