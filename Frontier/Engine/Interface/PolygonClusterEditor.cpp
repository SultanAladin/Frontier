/*==========================================================================================================================================
                                                     POLYGONCLUSTEREDITOR.CPP
==========================================================================================================================================*/
// 🧩 Standalone virtualized polygon cluster LOD DAG authoring editor bound to PolygonClusterInspector.

#include "Engine/Interface/PolygonClusterEditor.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>

namespace Frontier
{

void PolygonClusterEditor::ConstructInterface(RecordToken Token, RecordInspector& Inspector)
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    DiagramControls::ConstructEditorModalFrame(
        "PolygonClusterEditor — Virtualized LOD DAG & Hi-Z Cut", "Layers", Token, Inspector,
        [&](ImDrawList* DrawList, ImVec2 Min, ImVec2 Max) {
            const float W    = Max.x - Min.x;
            const float H    = Max.y - Min.y;
            const ImU32 Blue = ParseHexSpectrum("#78aaff", Palette.ThemeAccent);
            for (int Tier = 0; Tier < 6; ++Tier)
            {
                const int   Nodes = 1 << std::min(Tier, 4);
                const float Y     = Min.y + 58.0f + static_cast<float>(Tier) * ((H - 92.0f) / 6.0f);
                for (int Node = 0; Node < Nodes; ++Node)
                {
                    const float X = Min.x + 36.0f + (static_cast<float>(Node) + 0.5f) * ((W - 72.0f) / static_cast<float>(Nodes));
                    DrawList->AddRectFilled(ImVec2(X - 10.0f, Y - 6.0f), ImVec2(X + 10.0f, Y + 6.0f),
                                            ModulateSpectrumAlpha(Blue, 0.28f), 3.0f);
                    DrawList->AddRect(ImVec2(X - 10.0f, Y - 6.0f), ImVec2(X + 10.0f, Y + 6.0f), Blue, 3.0f);
                }
            }
            DrawList->AddText(Faces.Monospace, 10.5f, ImVec2(Min.x + 16.0f, Min.y + 14.0f),
                              Palette.TextSecondary, "VIRTUALIZED POLYGON CLUSTER DAG · HI-Z PYRAMID CUT");
        });
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
void PolygonClusterEditor::ConstructInterface(RecordToken, RecordInspector&) {}
} // namespace Frontier

#endif
