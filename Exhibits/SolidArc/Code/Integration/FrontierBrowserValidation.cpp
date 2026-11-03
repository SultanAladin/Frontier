/*==========================================================================================================================================
                                                       FRONTIERBROWSERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify CAD decoding and workspace launch through the existing Frontier project browser

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Browser/SolidArcBrowserLink.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include <imgui.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Frontier;
using namespace Frontier::SolidArc;

namespace
{
int Checks = 0;

void Check(bool AcceptedCondition, const char* Description)
{
    if (!AcceptedCondition) throw std::runtime_error(Description);
    ++Checks;
    std::cout << "PASS " << Description << '\n';
}

struct TemporaryDocumentDirectory
{
    std::filesystem::path Directory;

    TemporaryDocumentDirectory()
    {
        Directory = std::filesystem::temp_directory_path() /
            ("Frontier SolidArc verification " +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(Directory);
    }

    ~TemporaryDocumentDirectory()
    {
        std::error_code Failure;
        std::filesystem::remove_all(Directory, Failure);
    }
};
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    ImGui::CreateContext();
    try
    {
        TemporaryDocumentDirectory Temporary;
        SolidArcDocument Document;
        SolidArcConstructRequest Construction;
        Construction.Width  = 2;
        Construction.Depth  = 3;
        Construction.Height = 1;
        Check(ConstructSolidArcPrimitive(Document, Construction).AcceptedCondition, "real CAD fixture constructs");
        const auto Original = EncodeSolidArcDocument(Document);
        const auto File     = Temporary.Directory / "Body Panel.SOLIDARC";
        std::ofstream(File) << Original;
        const auto Preview = DescribeSolidArcBrowserDocument(File);
        Check(Preview.Refusal.empty(), "current Frontier browser decodes the CAD document");
        Check(Preview.Geometry && !Preview.Geometry->empty(), "preview contains real tessellated CAD geometry");
        Check(Preview.Summary.find("1 body") != std::string::npos, "browser summary comes from actual CAD regeneration");
        Check(!Preview.GeometryKey.empty(), "preview supplies a residence key");
        ProjectBrowserPanel Browser;
        int Launches = 0;
        SolidArcBrowserLinkSettings Settings;
        Settings.Launch = [&](const std::filesystem::path& Path, std::string&)
        {
            Check(Path == File, "launch callback receives the exact spaced document path");
            ++Launches;
            return true;
        };
        ConnectSolidArcBrowser(Browser, Settings);
        Check(bool(Browser.QueryDocumentGeometry) && bool(Browser.LaunchWorkspaceDocument),
              "existing browser receives CAD hooks");
        Check(Browser.QueryDocumentGeometry(Temporary.Directory / "ordinary.txt").Geometry == nullptr,
              "non-CAD documents bypass the CAD decoder");
        Check(!Browser.QueryDocumentGeometry(Temporary.Directory / "missing.solidarc").Refusal.empty(),
              "missing CAD documents give an explicit refusal");
        Check(Browser.AttachPaths({File}, true) == 1, "existing browser attaches the CAD file");
        const auto Entry = Browser.QueryEntry(Browser.QueryActiveToken());
        Check(Entry && Entry->Category == BrowserFileCategory::Cad && Entry->Geometry,
              "attached document is a CAD entry with native geometry");
        Browser.OpenActiveFile();
        Check(Launches == 1, "Open in SolidArc launches CAD exactly once");
        Check(!Browser.QueryLaunchOutcome().empty(), "launch status remains visible in the browser");
        std::string Refusal;
        Check(!LaunchSolidArcWorkspace(Temporary.Directory / "missing workspace", {}, File, Refusal) && !Refusal.empty(),
              "missing workspace reports failure rather than false success");
        Check(Browser.OpenSample("valley"), "existing Frontier project sample remains accessible");
        Check(Launches == 1, "opening an ordinary project does not launch SolidArc");
        std::ifstream Input(File);
        const std::string After((std::istreambuf_iterator<char>(Input)), {});
        Check(After == Original, "preview and browser launch leave the CAD source byte-identical");
        std::cout << "PASS " << Checks << " Frontier SolidArc browser checks\n";
        ImGui::DestroyContext();
        return 0;
    }
    catch (const std::exception& Failure)
    {
        std::cerr << "FAIL " << Failure.what() << '\n';
        ImGui::DestroyContext();
        return 1;
    }
}
