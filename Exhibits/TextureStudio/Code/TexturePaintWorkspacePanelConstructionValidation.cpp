/*============================================================================================================================================
                                       TEXTUREPAINTWORKSPACEPANELCONSTRUCTIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Construction coverage for the Texture Paint workspace native port:
//    (1) TexturePaintWorkspacePanel constructs/Initializes/Activates cleanly, exactly as
//        TextureStudioApplication does unconditionally at startup (mirrors the Baking/Stamp
//        construction validations' startup-crash guard).
//    (2) the embedded document model matches Alloy's default project shell: document name
//        "Teapot study", three layers with the paint layer "Surface detail" selected on top,
//        snapshot history seeded, and the teapot topology (the viewport's paint/pick data)
//        really built.

#include "UI/TextureStudio/TexturePaintWorkspacePanel.h"
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
} // namespace

int main()
{
    using namespace Frontier;
    try
    {
        TexturePaintWorkspacePanel Panel;
        Require(Panel.Initialize(), "TexturePaintWorkspacePanel::Initialize must succeed");
        Require(Panel.Activate(), "TexturePaintWorkspacePanel::Activate must succeed");

        Require(Panel.QueryDocumentName() == "Teapot study", "document must be named Teapot study");
        Require(Panel.QueryLayerCount() == 3, "default document must have three layers");
        Require(Panel.QuerySelectedLayerName() == "Surface detail", "top paint layer must start selected");
        Require(Panel.QueryHistoryCount() >= 1, "history must start seeded with an initial revision");
        Require(Panel.QueryTriangleCount() > 0, "teapot topology must build triangles");
        Require(Panel.QueryMaterialAssetCount() == 0, "no material assets before the app configures them");
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "TexturePaintWorkspacePanelConstructionValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "TexturePaintWorkspacePanelConstructionValidation passed.\n";
    return 0;
}
