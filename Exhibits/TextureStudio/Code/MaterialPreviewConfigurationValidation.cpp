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
        Inspection = {};
        Inspection.PanX = std::numeric_limits<float>::infinity();
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Infinite pan accepted.");
        Inspection = {};
        Inspection.Pitch = 2;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Unbounded orbit pitch accepted.");
        Inspection = {};
        Inspection.Shape = MaterialPreviewShape::Cube;
        Inspection.PanX = .3f;
        Inspection.PanY = -.2f;
        Inspection.Yaw = .8f;
        Inspection.Pitch = -.4f;
        const auto Camera = ConstructMaterialPreviewProjection(Inspection);
        Confirm(std::abs(Camera.ClipFromSurface[0] * 48 + Camera.ClipFromSurface[12] - Inspection.PanX) < .0001f &&
            std::abs(Camera.ClipFromSurface[1] * 48 + Camera.ClipFromSurface[13] - Inspection.PanY) < .0001f,
            "Rotating an offset inspection shape lost its camera centre.");
        Inspection.Environment = MaterialPreviewEnvironment::CoolRim;
        Inspection.Exposure = 2;
        ResetMaterialPreviewView(Inspection);
        Confirm(Inspection.Shape == MaterialPreviewShape::Cube && Inspection.Environment == MaterialPreviewEnvironment::CoolRim &&
            Inspection.Exposure == 2 && Inspection.PanX == 0 && Inspection.Yaw == 0 && Inspection.Zoom == 1,
            "View reset changed authored inspection lighting/shape.");
        Inspection = {}; Inspection.ViewportAspect = 2;
        const auto Wide = ConstructMaterialPreviewProjection(Inspection);
        Inspection.ViewportAspect = 1;
        const auto Square = ConstructMaterialPreviewProjection(Inspection);
        Confirm(std::abs(Wide.ClipFromSurface[0] * 2 - Square.ClipFromSurface[0]) < .00001f, "Wide preview projection did not compensate viewport aspect.");
        MaterialPreviewMotion Motion; Inspection = {}; Inspection.PanX = 1; Inspection.Zoom = 2;
        Motion.ConfigureDestination(Inspection); Motion.Advance(.12f);
        Confirm(std::abs(Motion.QueryDisplayed().PanX - (1.f - std::exp(-1.f))) < .0001f, "120 ms destination motion is not exponential.");
        Confirm(Motion.QueryDisplayed().Zoom > 1 && Motion.QueryDisplayed().Zoom < 2, "Displayed zoom snapped instead of converging.");
        for (const int Rate : {4, 30, 60, 144})
        {
            Motion.Reset({}); Motion.ConfigureDestination(Inspection);
            for (int Sample = 0; Sample < Rate; ++Sample) Motion.Advance(1.f / Rate);
            Confirm(std::abs(Motion.QueryDisplayed().PanX - (1.f - std::exp(-1.f / .12f))) < .0001f, "Motion convergence depends on frame rate.");
        }
        Motion.Advance(0, true); Confirm(Motion.QueryDisplayed() == Inspection, "Reduced motion did not snap destination.");
        Motion.Reset({}); Confirm(Motion.QueryDisplayed().PanX == 0, "Motion reset did not reset displayed camera.");
        Confirm(!Motion.Advance(std::numeric_limits<float>::quiet_NaN()), "NaN frame time altered the displayed camera.");
        std::string Diagnostic;
        const auto Geometry = ConstructMaterialInspectionGeometry();
        Confirm(ValidateSurfaceGeometry(Geometry, Diagnostic), Diagnostic.c_str());
        std::array<size_t,4> Populations{};
        size_t NegativeCubeTangents = 0;
        for (const auto &Corner : Geometry.Corners)
        {
            const int Region = int(std::round(Corner.Position[0] / 16.f));
            Confirm(Region >= 0 && Region <= 3, "Inspection geometry escaped separated regions.");
            ++Populations[size_t(Region)];
            if (Region == 3 && Corner.Tangent[3] == -1) ++NegativeCubeTangents;
        }
        for (auto Population : Populations) Confirm(Population > 0, "Inspection surface missing.");
        Confirm(NegativeCubeTangents == 12, "Negative cube faces lost normal-map tangent handedness.");
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
        const auto DragCanvas = [&](int Button, float Horizontal, float Vertical, bool Shift = false, bool Control = false)
        {
            const auto Canvas = Panel.QueryPreviewControlExtent(7);
            const ImVec2 Centre((Canvas.MinimumX + Canvas.MaximumX) / 2, (Canvas.MinimumY + Canvas.MaximumY) / 2);
            IO.AddMousePosEvent(Centre.x, Centre.y);
            IO.AddKeyEvent(ImGuiMod_Shift, Shift);
            IO.AddKeyEvent(ImGuiMod_Ctrl, Control);
            Integrate();
            IO.AddMouseButtonEvent(Button, true);
            Integrate();
            IO.AddMousePosEvent(Centre.x + Horizontal, Centre.y + Vertical);
            Integrate();
            IO.AddMouseButtonEvent(Button, false);
            IO.AddKeyEvent(ImGuiMod_Shift, false);
            IO.AddKeyEvent(ImGuiMod_Ctrl, false);
            Integrate();
        };
        DragCanvas(2, 30, 20, true);
        Confirm(Panel.QueryPreviewConfiguration().PanX > 0 && Panel.QueryPreviewConfiguration().PanY > 0,
            "Blender Shift-MMB pan did not respond to actual pointer drag.");
        Click(6);
        Confirm(Panel.QueryPreviewConfiguration().PanX == 0 && Panel.QueryPreviewConfiguration().PanY == 0,
            "Reset view did not centre the specimen.");
        DragCanvas(0, 20, 15, true);
        Confirm(Panel.QueryPreviewConfiguration().PanX > 0 && Panel.QueryPreviewConfiguration().Yaw == 0,
            "Shift-left drag did not pan independently of orbit.");
        Click(6);
        DragCanvas(2, 35, 20);
        Confirm(Panel.QueryPreviewConfiguration().Yaw != 0 && Panel.QueryPreviewConfiguration().Pitch != 0,
            "Blender MMB did not orbit the specimen.");
        const auto BeforeRelease = Panel.QueryPreviewConfiguration();
        IO.AddMousePosEvent(10,10);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration() == BeforeRelease, "Released pointer continued moving the camera.");
        Click(6);
        DragCanvas(2, 0, -30, false, true);
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Blender Ctrl-MMB did not zoom.");
        Click(6);
        const auto Canvas = Panel.QueryPreviewControlExtent(7);
        IO.AddMousePosEvent((Canvas.MinimumX + Canvas.MaximumX) / 2, (Canvas.MinimumY + Canvas.MaximumY) / 2);
        Integrate();
        IO.AddMouseWheelEvent(0,1);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Canvas wheel zoom did not respond.");
        IO.AddKeyEvent(ImGuiKey_F,true);
        Integrate();
        IO.AddKeyEvent(ImGuiKey_F,false);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Zoom == 1, "F did not reset the camera.");
        IO.AddKeyEvent(ImGuiKey_RightArrow,true);
        Integrate();
        IO.AddKeyEvent(ImGuiKey_RightArrow,false);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().PanX > 0, "Keyboard pan did not respond.");
        Click(6);
        const auto BeforeWidgetWheel = Panel.QueryPreviewConfiguration();
        const auto ShapeExtent = Panel.QueryPreviewControlExtent(3);
        IO.AddMousePosEvent((ShapeExtent.MinimumX + ShapeExtent.MaximumX) / 2, (ShapeExtent.MinimumY + ShapeExtent.MaximumY) / 2);
        Integrate();
        IO.AddMouseWheelEvent(0,1);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration() == BeforeWidgetWheel, "Wheel over a selector leaked into preview navigation.");
        const auto SelectPreset = [&](size_t Control, int Index)
        {
            Click(Control);
            Integrate();
            Integrate();
            auto *Context = ImGui::GetCurrentContext();
            Confirm(!Context->OpenPopupStack.empty(), "Preview selector did not open a popup.");
            auto *Popup = Context->OpenPopupStack.back().Window;
            Confirm(Popup != nullptr, "Preview selector popup has no window.");
            Confirm(Popup->WindowPadding.x >= 8 && Popup->WindowPadding.y >= 8 && Popup->WindowRounding >= 14,
                "Material dropdown lost padding or rounded corners.");
            const auto Extent = Panel.QueryMenuOptionExtent(size_t(Index));
            Confirm(Extent.Height() >= 28, "Material dropdown option has no padded hit region.");
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) / 2, (Extent.MinimumY + Extent.MaximumY) / 2);
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
        Report << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"actual_zoom_pointer_events\":true,\"pan_orbit_wheel_keyboard_release_and_widget_isolation\":true,\"actual_shape_environment_exposure_pointer_events\":true}\n";
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
