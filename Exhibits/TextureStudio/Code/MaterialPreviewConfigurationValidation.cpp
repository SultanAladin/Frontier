/*==========================================================================================================================================
                                               MATERIALPREVIEWCONFIGURATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify inspection bounds, separated geometry, real zoom-pill input and bounded persistent diagnostics.

#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include "Engine/Render/MaterialPreviewGeometry.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
#include <limits>

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition, const char *Description)
        {
            if (!Condition) throw std::runtime_error(Description);
            ++Checks;
        };
        Confirm(Count == 2, "Expected JSON report destination.");
        MaterialPreviewConfiguration Inspection;
        Confirm(ValidateMaterialPreviewConfiguration(Inspection), "Default inspection configuration rejected.");
        for (uint32_t Shape = 0; Shape <= 3; ++Shape)
            for (uint32_t Environment = 0; Environment <= 3; ++Environment)
            {
                Inspection.Shape = MaterialPreviewShape(Shape);
                Inspection.Environment = MaterialPreviewEnvironment(Environment);
                Confirm(ValidateMaterialPreviewConfiguration(Inspection), "Supported inspection preset rejected.");
            }
        Inspection.Zoom = std::numeric_limits<float>::quiet_NaN();
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "NaN zoom accepted.");
        Inspection = {};
        Inspection.Zoom = .49f;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Out-of-range zoom accepted.");
        Inspection = {};
        Inspection.Environment = MaterialPreviewEnvironment(4);
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Unknown environment accepted.");
        Inspection = {};
        Inspection.Shape = MaterialPreviewShape(4);
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Unknown shape accepted.");
        Inspection = {};
        Inspection.Exposure = 4.01f;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Out-of-range exposure accepted.");
        std::string Diagnostic;
        const auto Geometry = ConstructMaterialInspectionGeometry();
        Confirm(ValidateSurfaceGeometry(Geometry, Diagnostic), Diagnostic.c_str());
        std::array<size_t,4> Populations{};
        for (const auto &Corner : Geometry.Corners)
        {
            const int Region = int(std::round(Corner.Position[0] / 16.f));
            Confirm(Region >= 0 && Region <= 3, "Inspection geometry escaped separated regions.");
            ++Populations[size_t(Region)];
        }
        for (auto Population : Populations) Confirm(Population > 0, "Inspection surface missing.");
        DiagnosticJournal::Initialize("InspectionValidation");
        DiagnosticJournal::Append("Acceptance", "Preview controls validation\nembedded newline");
        const auto Log = DiagnosticJournal::QueryFilename();
        Confirm(std::filesystem::is_regular_file(Log), "Persistent diagnostic file missing.");
        std::ifstream Input(Log);
        std::string FirstLine;
        std::getline(Input, FirstLine);
        Confirm(FirstLine.find("ms [Acceptance]") != std::string::npos, "Timestamp/category missing from diagnostic.");
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        IO.DisplaySize = ImVec2(1440,1000);
        IO.DeltaTime = 1.f / 60;
        unsigned char *Pixels = nullptr;
        int Width = 0, Height = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);
        IO.Fonts->SetTexID(ImTextureID(1));
        ActivationOwnership Ownership(new MaterialLibraryPanel());
        auto &Panel = *static_cast<MaterialLibraryPanel *>(Ownership.get());
        Confirm(Panel.Initialize() && Panel.Activate(), "Preview panel activation failed.");
        Confirm(Panel.ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio"), "Material fixtures unavailable.");
        uint32_t Notifications = 0;
        Panel.ConfigurePreview = [&](const MaterialPreviewConfiguration &Configuration)
        {
            Confirm(ValidateMaterialPreviewConfiguration(Configuration), "UI emitted invalid inspection settings.");
            ++Notifications;
        };
        const auto Integrate = [&]()
        {
            ImGui::NewFrame();
            Panel.ConstructInterface();
            ImGui::Render();
        };
        for (int Index = 0; Index < 4; ++Index) Integrate();
        const auto Click = [&](size_t Index)
        {
            const auto Extent = Panel.QueryPreviewControlExtent(Index);
            Confirm(Extent.MaximumX > Extent.MinimumX && Extent.MaximumY > Extent.MinimumY, "Preview control has no clickable extent.");
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) / 2, (Extent.MinimumY + Extent.MaximumY) / 2);
            Integrate();
            IO.AddMouseButtonEvent(0,true);
            Integrate();
            IO.AddMouseButtonEvent(0,false);
            Integrate();
        };
        Click(2);
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Zoom-in pill did not respond to actual pointer input.");
        Click(0);
        Confirm(std::abs(Panel.QueryPreviewConfiguration().Zoom - 1) < .001f, "Zoom-out pill did not respond.");
        Click(2);
        Click(1);
        Confirm(Panel.QueryPreviewConfiguration().Zoom == 1, "Zoom percentage reset did not respond.");
        const auto SelectPreset = [&](size_t Control, int Index)
        {
            Click(Control);
            Integrate();
            Integrate();
            auto *Context = ImGui::GetCurrentContext();
            Confirm(!Context->OpenPopupStack.empty(), "Preview selector did not open a popup.");
            auto *Popup = Context->OpenPopupStack.back().Window;
            Confirm(Popup != nullptr, "Preview selector popup has no window.");
            const float RowHeight = Popup->FontRefSize + ImGui::GetStyle().ItemSpacing.y;
            const auto Position = Popup->DC.CursorStartPos;
            IO.AddMousePosEvent(Position.x + 20, Position.y + Index * RowHeight + Popup->FontRefSize * .5f);
            Integrate();
            IO.AddMouseButtonEvent(0,true);
            Integrate();
            IO.AddMouseButtonEvent(0,false);
            Integrate();
        };
        for (int Index = 1; Index <= 3; ++Index)
        {
            SelectPreset(3, Index);
            Confirm(int(Panel.QueryPreviewConfiguration().Shape) == Index, "Preview shape selector did not respond to pointer selection.");
            SelectPreset(4, Index);
            Confirm(int(Panel.QueryPreviewConfiguration().Environment) == Index, "Environment selector did not respond to pointer selection.");
        }
        const auto ExposureExtent = Panel.QueryPreviewControlExtent(5);
        IO.AddMousePosEvent(ExposureExtent.MinimumX + (ExposureExtent.MaximumX - ExposureExtent.MinimumX) * .85f,
            (ExposureExtent.MinimumY + ExposureExtent.MaximumY) * .5f);
        Integrate();
        IO.AddMouseButtonEvent(0,true);
        Integrate();
        IO.AddMouseButtonEvent(0,false);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Exposure != 1, "Shared exposure slider did not respond to pointer input.");
        Confirm(Notifications >= 11, "Preview settings callbacks were not delivered.");
        Ownership.reset();
        ImGui::DestroyContext();
        std::ofstream Report(Arguments[1]);
        Report << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"actual_zoom_pointer_events\":true,\"actual_shape_environment_exposure_pointer_events\":true}\n";
        Confirm(bool(Report), "Could not write preview acceptance report.");
        std::cout << "Material inspection checks passed: " << Checks << '\n';
        return 0;
    }
    catch (const std::exception &Failure)
    {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
