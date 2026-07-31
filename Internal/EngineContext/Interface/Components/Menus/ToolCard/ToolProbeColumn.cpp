/*==============================================================================================================================================
                                                        TOOLPROBECOLUMN.CPP
==============================================================================================================================================*/
// 🧩 Draws the measurement readout. Four row shapes share one descriptor and are told apart by which members the table set, so the dispatch here is a
//    sequence of tests on the descriptor rather than a switch on a kind field — the tables are hand-authored and a kind tag would be a second thing
//    to keep consistent with the members it describes.

#include "ToolProbeColumn.h"
#include "ToolGlyphInscription.h"

#include <cstdlib>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float BodyPadding      = 9.0f;    // [px] - .probe-body padding
    constexpr float SectionGap       = 9.0f;    // [px] - .probe-sec margin-bottom
    constexpr float SectionHeadTop   = 4.0f;    // [px] - .probe-sec-head padding-top
    constexpr float SectionHeadFoot  = 6.0f;    // [px] - .probe-sec-head padding-bottom
    constexpr float RowPaddingY      = 5.0f;    // [px] - .probe-row padding
    constexpr float RowPaddingX      = 7.0f;    // [px]
    constexpr float RowRounding      = 7.0f;    // [px] - .probe-row radius
    constexpr float RowGapX          = 8.0f;    // [px] - .probe-row gap
    constexpr float UnitMinWidth     = 20.0f;   // [px] - .probe-row .p-u min-width
    constexpr float VectorGap        = 4.0f;    // [px] - .probe-vec gap
    constexpr float VectorPaddingTop = 2.0f;    // [px] - .probe-vec padding-top
    constexpr float VectorPaddingBot = 5.0f;    // [px] - .probe-vec padding-bottom
    constexpr float VectorCellPadX   = 7.0f;    // [px] - .probe-vec .axis padding
    constexpr float VectorCellPadY   = 5.0f;    // [px]
    constexpr float VectorCellGap    = 5.0f;    // [px] - .probe-vec .axis gap
    constexpr float VectorRounding   = 7.0f;    // [px] - .probe-vec .axis radius
    constexpr float FlagDotEdge      = 7.0f;    // [px] - .probe-flag .dot
    constexpr float FlagGap          = 7.0f;    // [px] - .probe-flag gap

    constexpr ImU32 FlagMetTone   = IM_COL32(0x4C, 0xC2, 0x7A, 0xFF);   // a satisfied condition
    constexpr ImU32 FlagUnmetTone = IM_COL32(0xE0, 0x60, 0x3F, 0xFF);   // an unsatisfied one

    // 📝 Which of the four shapes a row is. Tested in the order the members disambiguate: a vector row and a condition row each own a
    //    member no other shape sets, so the scalar shape is what is left.
    enum class ProbeRowShape
    {
        Scalar,
        Vector,
        Condition,
    };

    ProbeRowShape ResolveRowShape(const ToolProbeRowDescriptor& Row)
    {
        if (Row.VectorLabel != nullptr)    { return ProbeRowShape::Vector; }
        if (Row.ConditionLabel != nullptr) { return ProbeRowShape::Condition; }
        return ProbeRowShape::Scalar;
    }

    float MeasureRowHeight(const ToolProbeRowDescriptor& Row)
    {
        const float LineHeight = ImGui::GetTextLineHeight();
        switch (ResolveRowShape(Row))
        {
            case ProbeRowShape::Vector:
                return VectorPaddingTop + VectorCellPadY * 2.0f + LineHeight + VectorPaddingBot;
            case ProbeRowShape::Condition:
            case ProbeRowShape::Scalar:
            default:
                return RowPaddingY * 2.0f + LineHeight;
        }
    }

    // 📝 A reading that has left its authored limits. The reading arrives pre-formatted as text, so the comparison goes through
    //    strtod — the table states "12.4" because that is what the pane must print, and re-deriving the number here keeps the limit
    //    check honest to the printed value rather than to a second copy that could disagree with it.
    bool ReadingBreachesLimits(const ToolProbeRowDescriptor& Row)
    {
        if (!Row.LimitPresent || Row.Reading == nullptr)
        {
            return false;
        }
        const double Reading = std::strtod(Row.Reading, nullptr);
        return (Reading < static_cast<double>(Row.LimitFloor)) || (Reading > static_cast<double>(Row.LimitCeiling));
    }

    void InscribeProbeRow(const ToolProbeRowDescriptor& Row,
                          ImVec2                        Origin,
                          float                         ColumnWidth,
                          const ToolCardPalette&        Palette)
    {
        ImDrawList* const Canvas     = ImGui::GetWindowDrawList();
        const float       LineHeight = ImGui::GetTextLineHeight();
        const float       RowWidth   = ColumnWidth - BodyPadding * 2.0f;

        switch (ResolveRowShape(Row))
        {
            //---------------------------------------------------------- VECTOR ----------------------------------------------------------
            case ProbeRowShape::Vector:
            {
                // Three equal cells on one line, each an axis name over black beside its reading. The label the table gives the vector
                // is carried by the section head above it, so the row itself is only the triple.
                int AxisCount = 0;
                while (AxisCount < ToolProbeAxisLimit && Row.AxisLabels[AxisCount] != nullptr)
                {
                    ++AxisCount;
                }
                if (AxisCount == 0)
                {
                    return;
                }

                const float CellWidth = (RowWidth - VectorGap * static_cast<float>(AxisCount - 1))
                                      / static_cast<float>(AxisCount);
                const float CellTop   = Origin.y + VectorPaddingTop;

                for (int AxisIndex = 0; AxisIndex < AxisCount; ++AxisIndex)
                {
                    const ImVec2 CellMin(Origin.x + static_cast<float>(AxisIndex) * (CellWidth + VectorGap), CellTop);
                    const ImVec2 CellMax(CellMin.x + CellWidth, CellTop + VectorCellPadY * 2.0f + LineHeight);
                    Canvas->AddRectFilled(CellMin, CellMax, Palette.ValueBlack, VectorRounding);

                    const float TextY = CellMin.y + VectorCellPadY;
                    Canvas->AddText(ImVec2(CellMin.x + VectorCellPadX, TextY), Palette.Faint, Row.AxisLabels[AxisIndex]);

                    const char* const Reading = (Row.AxisReadings[AxisIndex] != nullptr)
                                              ? Row.AxisReadings[AxisIndex] : "";
                    const float NameWidth = ImGui::CalcTextSize(Row.AxisLabels[AxisIndex]).x;
                    Canvas->PushClipRect(ImVec2(CellMin.x + VectorCellPadX + NameWidth + VectorCellGap, CellMin.y),
                                         ImVec2(CellMax.x - VectorCellPadX, CellMax.y), true);
                    Canvas->AddText(ImVec2(CellMin.x + VectorCellPadX + NameWidth + VectorCellGap, TextY),
                                    Palette.Ink, Reading);
                    Canvas->PopClipRect();
                }
                break;
            }

            //--------------------------------------------------------- CONDITION --------------------------------------------------------
            case ProbeRowShape::Condition:
            {
                const float MidY = Origin.y + RowPaddingY + LineHeight * 0.5f;
                Canvas->AddCircleFilled(ImVec2(Origin.x + RowPaddingX + FlagDotEdge * 0.5f, MidY), FlagDotEdge * 0.5f,
                                        Row.ConditionMet ? FlagMetTone : FlagUnmetTone);
                Canvas->AddText(ImVec2(Origin.x + RowPaddingX + FlagDotEdge + FlagGap, Origin.y + RowPaddingY),
                                Palette.Muted, Row.ConditionLabel);
                break;
            }

            //---------------------------------------------------------- SCALAR ----------------------------------------------------------
            case ProbeRowShape::Scalar:
            default:
            {
                const float TextY = Origin.y + RowPaddingY;
                float       Right = Origin.x + RowWidth - RowPaddingX;

                // The unit is laid out first, from the right, so the reading can be placed against its true left edge — a unit given a
                // minimum width would otherwise overlap a long reading.
                if (Row.Unit != nullptr && Row.Unit[0] != '\0')
                {
                    const ImVec2 UnitSize = ImGui::CalcTextSize(Row.Unit);
                    const float  UnitRoom = (UnitSize.x > UnitMinWidth) ? UnitSize.x : UnitMinWidth;
                    Canvas->AddText(ImVec2(Right - UnitRoom, TextY), Palette.Faint, Row.Unit);
                    Right -= UnitRoom + RowGapX;
                }

                if (Row.Reading != nullptr && Row.Reading[0] != '\0')
                {
                    const ImVec2 ReadingSize = ImGui::CalcTextSize(Row.Reading);
                    // 🔴 A reading outside its authored band reads red. Stating a limit and then printing a violation of it in the same
                    //    tone as a good value defeats the whole reason the limit is on screen.
                    const ImU32 ReadingInk = ReadingBreachesLimits(Row) ? Palette.BreachInk : Palette.Ink;
                    Canvas->AddText(ImVec2(Right - ReadingSize.x, TextY), ReadingInk, Row.Reading);
                    Right -= ReadingSize.x + RowGapX;
                }

                if (Row.Key != nullptr)
                {
                    Canvas->PushClipRect(ImVec2(Origin.x + RowPaddingX, Origin.y), ImVec2(Right, Origin.y + LineHeight * 2.0f), true);
                    Canvas->AddText(ImVec2(Origin.x + RowPaddingX, TextY), Palette.Muted, Row.Key);
                    Canvas->PopClipRect();
                }
                break;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float MeasureToolProbeHeight(const ToolProbeReadoutDescriptor& Readout,
                            int                               SelectedCount,
                            float                             ColumnWidth)
{
    (void)ColumnWidth;

    const float LineHeight  = ImGui::GetTextLineHeight();
    const float SectionHead = SectionHeadTop + LineHeight + SectionHeadFoot;
    float       Consumed    = BodyPadding * 2.0f;

    // 📝 A multi-pick reports the AGGREGATE instead of the per-component sections: a sum over twelve faces is a different statement
    //    from one face's own measurements, and printing one face's readings while twelve are picked would state a falsehood.
    if (SelectedCount > 1)
    {
        Consumed += SectionHead;
        for (int RowIndex = 0; RowIndex < Readout.AggregateCount; ++RowIndex)
        {
            Consumed += MeasureRowHeight(Readout.Aggregate[RowIndex]);
        }
        return Consumed;
    }

    for (int SectionIndex = 0; SectionIndex < Readout.SectionCount; ++SectionIndex)
    {
        const ToolProbeSectionDescriptor& Section = Readout.Sections[SectionIndex];
        Consumed += SectionHead;
        for (int RowIndex = 0; RowIndex < Section.RowCount; ++RowIndex)
        {
            Consumed += MeasureRowHeight(Section.Rows[RowIndex]);
        }
        if (SectionIndex + 1 < Readout.SectionCount)
        {
            Consumed += SectionGap;
        }
    }
    return Consumed;
}


void InscribeToolProbeColumn(const SvgIconRegistry*            Icons,
                             const ToolProbeReadoutDescriptor& Readout,
                             int                               SelectedCount,
                             ImVec2                            Origin,
                             float                             ColumnWidth,
                             const ToolCardPalette&            Palette)
{
    (void)Icons;

    ImDrawList* const Canvas     = ImGui::GetWindowDrawList();
    const float       LineHeight = ImGui::GetTextLineHeight();
    const float       RowLeft    = Origin.x + BodyPadding;
    float             CursorY    = Origin.y + BodyPadding;

    const auto InscribeSectionHead = [&](const char* Title)
    {
        Canvas->AddText(ImVec2(RowLeft + SectionHeadTop, CursorY + SectionHeadTop), Palette.Faint,
                        (Title != nullptr) ? Title : "");
        CursorY += SectionHeadTop + LineHeight + SectionHeadFoot;
    };

    if (SelectedCount > 1)
    {
        InscribeSectionHead("AGGREGATE");
        for (int RowIndex = 0; RowIndex < Readout.AggregateCount; ++RowIndex)
        {
            const ToolProbeRowDescriptor& Row = Readout.Aggregate[RowIndex];
            InscribeProbeRow(Row, ImVec2(RowLeft, CursorY), ColumnWidth, Palette);
            CursorY += MeasureRowHeight(Row);
        }
        return;
    }

    for (int SectionIndex = 0; SectionIndex < Readout.SectionCount; ++SectionIndex)
    {
        const ToolProbeSectionDescriptor& Section = Readout.Sections[SectionIndex];
        InscribeSectionHead(Section.Title);

        for (int RowIndex = 0; RowIndex < Section.RowCount; ++RowIndex)
        {
            const ToolProbeRowDescriptor& Row = Section.Rows[RowIndex];
            InscribeProbeRow(Row, ImVec2(RowLeft, CursorY), ColumnWidth, Palette);
            CursorY += MeasureRowHeight(Row);
        }

        if (SectionIndex + 1 < Readout.SectionCount)
        {
            CursorY += SectionGap;
        }
    }
}

} // namespace Frontier
