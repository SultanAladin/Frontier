/*==============================================================================================================================================
                                                            RENDERREPORTPASS.CPP
==============================================================================================================================================*/
// 🧩 The "Render Report" body: a 2×2 metric grid (value + delta) over dot-matrix Memory & I/O rows (LiveTelemetryScene perf card)

#include "RenderReportPass.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const uint32_t MetricColumns = 2u;    // [-] - the metric grid is 2 wide
static const float    MetricRowGap  = 6.0f;   // [px] - vertical gap between metric rows
static const float    SectionGap    = 12.0f;  // [px] - gap between the metric grid and the matrix block
static const uint32_t MatrixRows    = 4u;     // [-] - dot rows per matrix strip (HTML drawMatrix rows = 4)
static const uint32_t MatrixColumns = 40u;    // [-] - dot columns per matrix strip (HTML cols = min(hist,40))
static const float    MatrixHeight  = 30.0f;  // [px] - a matrix strip's pixel height
static const float    MatrixLabelGap = 3.0f;  // [px] - gap between a row's name/amount line and its dot strip
static const float    MatrixRowGap  = 10.0f;  // [px] - gap between successive matrix rows

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 One metric cell: status dot + label on the label line, the big mono value (with small unit) below, and a delta line that
    //    arrows against the previous sample. Drawn into the given cell rectangle top-left.
    void ConstructMetricCell(const InstrumentBodyContext&    Context,
                             const PanelMetricCell&          Cell,
                             const SignalEvaluationInterval& Range,
                             float                           CellX,
                             float                           CellY)
    {
        ImDrawList* DrawList = Context.DrawList;
        ImFont* Font = ImGui::GetFont();
        float BaseSize = ImGui::GetFontSize();

        // 📝 Label line: a small coloured dot then the uppercase-ish label, muted.
        DrawList->AddCircleFilled(ImVec2(CellX + 3.0f, CellY + BaseSize * 0.4f), 3.0f, ResolveInstrumentColour(Cell.DotTint));
        DrawList->AddText(Font, BaseSize * 0.8f, ImVec2(CellX + 11.0f, CellY - 1.0f), IM_COL32(108, 108, 116, 255), Cell.Label);

        // 📝 Value line: the latest sample, big mono, with the small unit suffix trailing.
        char ValueText[24];
        std::snprintf(ValueText, sizeof(ValueText), "%.*f", Cell.DecimalPlaces, Range.Latest);
        float ValueY = CellY + BaseSize * 0.9f;
        float ValueSize = BaseSize * 1.5f;
        DrawList->AddText(Font, ValueSize, ImVec2(CellX, ValueY), IM_COL32(244, 244, 246, 255), ValueText);
        if (Cell.UnitSuffix[0] != '\0')
        {
            ImVec2 ValueExtent = Font->CalcTextSizeA(ValueSize, FLT_MAX, 0.0f, ValueText);
            DrawList->AddText(Font, BaseSize * 0.8f, ImVec2(CellX + ValueExtent.x + 2.0f, ValueY + ValueSize - BaseSize),
                              IM_COL32(108, 108, 116, 255), Cell.UnitSuffix);
        }

        // 📝 Delta line: compare the newest sample to the one before it; arrow up (green) / down (coral) / flat (muted).
        float Previous = (Range.Count >= 2u) ? Range.Samples[Range.Count - 2u] : Range.Latest;
        float Change = Range.Latest - Previous;
        ImU32 DeltaColour = (Change > 1e-4f)  ? IM_COL32(61, 220, 132, 255)
                          : (Change < -1e-4f) ? IM_COL32(255, 90, 82, 255)
                                              : IM_COL32(108, 108, 116, 255);
        const char* Marker = (Change > 1e-4f) ? "\xE2\x96\xB2" : (Change < -1e-4f) ? "\xE2\x96\xBC" : "\xE2\x80\x94"; // ▲ ▼ —
        char DeltaText[32];
        std::snprintf(DeltaText, sizeof(DeltaText), "%s %.*f", Marker, Cell.DecimalPlaces, Change < 0.0f ? -Change : Change);
        DrawList->AddText(Font, BaseSize * 0.72f, ImVec2(CellX, ValueY + ValueSize + 1.0f), DeltaColour, DeltaText);
    }

    // 📝 One dot-matrix row: the name + amount line, then a MatrixRows×MatrixColumns lit-cell field whose fill height per column
    //    is the (constant, current) 0..1 level — the HTML animates history across columns; we light every column to the current
    //    level (the ring's latest against the row's capacity), which reads the same at a glance and needs no per-column history.
    void ConstructMatrixRow(const InstrumentBodyContext&    Context,
                            const PanelMatrixRow&           Row,
                            const SignalEvaluationInterval& Range,
                            float                           StripX,
                            float                           StripY,
                            float                           StripWidth)
    {
        ImDrawList* DrawList = Context.DrawList;
        ImFont* Font = ImGui::GetFont();
        float BaseSize = ImGui::GetFontSize();

        // 📝 Name (left, muted) + amount (right, mono) on the label line.
        DrawList->AddText(Font, BaseSize * 0.78f, ImVec2(StripX, StripY), IM_COL32(108, 108, 116, 255), Row.Label);
        char AmountText[32];
        std::snprintf(AmountText, sizeof(AmountText), "%d %s", static_cast<int>(Range.Latest + 0.5f), Row.UnitSuffix);
        ImVec2 AmountExtent = Font->CalcTextSizeA(BaseSize * 0.78f, FLT_MAX, 0.0f, AmountText);
        DrawList->AddText(Font, BaseSize * 0.78f, ImVec2(StripX + StripWidth - AmountExtent.x, StripY),
                          IM_COL32(207, 207, 214, 255), AmountText);

        // 📝 The 0..1 fill level: signal / capacity, or signal / running-max when capacity is auto (0).
        float Denominator = (Row.Capacity > 1e-4f) ? Row.Capacity : (Range.Maximum > 1e-4f ? Range.Maximum : 1.0f);
        float Level = Range.Latest / Denominator;
        if (Level < 0.0f) { Level = 0.0f; }
        if (Level > 1.0f) { Level = 1.0f; }
        uint32_t LitRows = static_cast<uint32_t>(Level * MatrixRows + 0.5f);

        // 📝 Lit-cell grid: lit dots (bottom-up) in the row tint, unlit dots faint white — the SegmentUI dot-matrix look.
        float GridTop = StripY + BaseSize + MatrixLabelGap;
        float CellW = StripWidth / static_cast<float>(MatrixColumns);
        float CellH = MatrixHeight / static_cast<float>(MatrixRows);
        float Radius = (CellW < CellH ? CellW : CellH) * 0.40f;
        ImU32 LitColour   = ResolveInstrumentColour(Row.LitTint);
        ImU32 UnlitColour = IM_COL32(255, 255, 255, 18);
        for (uint32_t Column = 0u; Column < MatrixColumns; ++Column)
        {
            for (uint32_t Cell = 0u; Cell < MatrixRows; ++Cell)
            {
                bool On = (MatrixRows - 1u - Cell) < LitRows;
                float CenterX = StripX + Column * CellW + CellW * 0.5f;
                float CenterY = GridTop + Cell * CellH + CellH * 0.5f;
                DrawList->AddCircleFilled(ImVec2(CenterX, CenterY), Radius, On ? LitColour : UnlitColour);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the metric grid then the matrix rows, top to bottom, inside the body rectangle.
void ConstructRenderReport(const InstrumentBodyContext& Context)
{
    const PanelStaticPayload& Payload = Context.Presentation->Payload;
    float BaseSize = ImGui::GetFontSize();

    float OriginX   = Context.BodyMinimum.x;
    float OriginY   = Context.BodyMinimum.y;
    float BodyWidth = Context.BodyMaximum.x - Context.BodyMinimum.x;

    // 📝 Metric grid: 2 columns, ceil(MetricCount/2) rows. Each cell is a label + value + delta stack ~3 lines tall.
    float ColumnWidth = BodyWidth / static_cast<float>(MetricColumns);
    float CellHeight  = BaseSize * 3.4f + MetricRowGap;
    uint32_t MetricRowsUsed = (Payload.MetricCount + MetricColumns - 1u) / MetricColumns;
    for (uint32_t Index = 0u; Index < Payload.MetricCount; ++Index)
    {
        uint32_t Column = Index % MetricColumns;
        uint32_t Row    = Index / MetricColumns;
        float CellX = OriginX + Column * ColumnWidth;
        float CellY = OriginY + Row * CellHeight;
        ConstructMetricCell(Context, Payload.MetricCells[Index], ResolveBodyRange(Context, Index), CellX, CellY);
    }

    // 📝 Matrix block below the grid. The matrix signals follow the metric signals in the record's slot order, so slot
    //    MetricCount is the first matrix row's signal.
    float MatrixTop = OriginY + MetricRowsUsed * CellHeight + SectionGap;
    float RowStride = BaseSize + MatrixLabelGap + MatrixHeight + MatrixRowGap;
    for (uint32_t Index = 0u; Index < Payload.MatrixCount; ++Index)
    {
        float StripY = MatrixTop + Index * RowStride;
        uint32_t SignalSlot = Payload.MetricCount + Index;
        ConstructMatrixRow(Context, Payload.MatrixRows[Index], ResolveBodyRange(Context, SignalSlot),
                           OriginX, StripY, BodyWidth);
    }
}

}   // namespace Frontier
