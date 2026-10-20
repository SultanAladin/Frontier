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
        for (uint32_t Shape = 0; Shape <= 4; ++Shape)
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
        Inspection.Zoom = 5;
        Confirm(ValidateMaterialPreviewConfiguration(Inspection), "500% preview zoom rejected.");
        Inspection.Zoom = 5.01f;
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Zoom above the 500% limit accepted.");
        Inspection = {};
        Inspection.Environment = MaterialPreviewEnvironment(4);
        Confirm(!ValidateMaterialPreviewConfiguration(Inspection), "Unknown environment accepted.");
        Inspection = {};
        Inspection.Shape = MaterialPreviewShape(5);
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
        std::string Diagnostic;
        const auto Geometry = ConstructMaterialInspectionGeometry();
        Confirm(ValidateSurfaceGeometry(Geometry, Diagnostic), Diagnostic.c_str());
        std::array<size_t,5> Populations{};
        size_t NegativeCubeTangents = 0;
        float ClothLowZ = 1, ClothHighZ = -1, ClothLowNormalZ = 1, ClothHighNormalZ = -1;
        for (const auto &Corner : Geometry.Corners)
        {
            const int Region = int(std::round(Corner.Position[0] / 16.f));
            Confirm(Region >= 0 && Region <= 4, "Inspection geometry escaped separated regions.");
            ++Populations[size_t(Region)];
            if (Region == 3 && Corner.Tangent[3] == -1) ++NegativeCubeTangents;
            if (Region == 4)
            {
                ClothLowZ = std::min(ClothLowZ, Corner.Position[2]);
                ClothHighZ = std::max(ClothHighZ, Corner.Position[2]);
                ClothLowNormalZ = std::min(ClothLowNormalZ, Corner.Normal[2]);
                ClothHighNormalZ = std::max(ClothHighNormalZ, Corner.Normal[2]);
            }
        }
        for (auto Population : Populations) Confirm(Population > 0, "Inspection surface missing.");
        Confirm(NegativeCubeTangents == 12, "Negative cube faces lost normal-map tangent handedness.");
        Confirm(ClothHighZ - ClothLowZ > .2f, "Draped cloth swatch is flat.");
        Confirm(ClothHighNormalZ - ClothLowNormalZ > .05f, "Draped cloth folds did not perturb normals.");
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
        const auto DragCanvas = [&](int Button, float Horizontal, float Vertical, bool Shift = false)
        {
            const auto Canvas = Panel.QueryPreviewControlExtent(7);
            const ImVec2 Centre((Canvas.MinimumX + Canvas.MaximumX) / 2, (Canvas.MinimumY + Canvas.MaximumY) / 2);
            IO.AddMousePosEvent(Centre.x, Centre.y);
            IO.AddKeyEvent(ImGuiMod_Shift, Shift);
            Integrate();
            IO.AddMouseButtonEvent(Button, true);
            Integrate();
            IO.AddMousePosEvent(Centre.x + Horizontal, Centre.y + Vertical);
            Integrate();
            IO.AddMouseButtonEvent(Button, false);
            IO.AddKeyEvent(ImGuiMod_Shift, false);
            Integrate();
        };
        DragCanvas(2, 30, 20);
        Confirm(Panel.QueryPreviewConfiguration().PanX > 0 && Panel.QueryPreviewConfiguration().PanY > 0,
            "MMB pan did not respond to actual pointer drag.");
        Click(6);
        Confirm(Panel.QueryPreviewConfiguration().PanX == 0 && Panel.QueryPreviewConfiguration().PanY == 0,
            "Reset view did not centre the specimen.");
        DragCanvas(0, 20, 15, true);
        Confirm(Panel.QueryPreviewConfiguration().PanX > 0 && Panel.QueryPreviewConfiguration().Yaw == 0,
            "Shift-left drag did not pan independently of orbit.");
        Click(6);
        DragCanvas(0, 35, 20);
        Confirm(Panel.QueryPreviewConfiguration().Yaw != 0 && Panel.QueryPreviewConfiguration().Pitch != 0,
            "Left drag did not orbit the specimen.");
        const auto BeforeRelease = Panel.QueryPreviewConfiguration();
        IO.AddMousePosEvent(10,10);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration() == BeforeRelease, "Released pointer continued moving the camera.");
        Click(6);
        DragCanvas(1, 0, -30);
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Right drag did not zoom.");
        Click(6);
        const auto Canvas = Panel.QueryPreviewControlExtent(7);
        IO.AddMousePosEvent((Canvas.MinimumX + Canvas.MaximumX) / 2, (Canvas.MinimumY + Canvas.MaximumY) / 2);
        Integrate();
        IO.AddMouseWheelEvent(0,1);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Canvas wheel zoom did not respond.");
        IO.AddMouseWheelEvent(0,100);
        Integrate();
        Confirm(std::abs(Panel.QueryPreviewConfiguration().Zoom - 5) < .001f,
            "Canvas wheel zoom did not reach the supported 500% magnification.");
        Click(6);
        Confirm(Panel.QueryPreviewConfiguration().Zoom == 1, "Preview did not reset cleanly from 500% magnification.");
        // 📝 Preview keys only drive the camera while the pointer hovers the inspection canvas, so parked
        // widget/popup input cannot move it. Return the pointer there before exercising F and arrow keys.
        IO.AddMousePosEvent((Canvas.MinimumX + Canvas.MaximumX) / 2, (Canvas.MinimumY + Canvas.MaximumY) / 2);
        Integrate();
        IO.AddMouseWheelEvent(0,1);
        Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Zoom > 1, "Canvas wheel zoom did not respond before F reset.");
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
            const float RowHeight = ControlKitTokens::DropdownRowHeight + ControlKitTokens::DropdownRowGap;
            const auto Position = Popup->DC.CursorStartPos;
            IO.AddMousePosEvent(Position.x + 20, Position.y + Index * RowHeight + ControlKitTokens::DropdownRowHeight * .5f);
            Integrate();
            IO.AddMouseButtonEvent(0,true);
            Integrate();
            IO.AddMouseButtonEvent(0,false);
            Integrate();
        };
        for (int Index = 1; Index <= 4; ++Index)
        {
            SelectPreset(3, Index);
            Confirm(int(Panel.QueryPreviewConfiguration().Shape) == Index, "Preview shape selector did not respond to pointer selection.");
            if (Index > 3) continue;
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
        const auto Fullscreen = Panel.QueryFullscreenPreviewExtent();
        Confirm(Fullscreen.Width() > 0 && Fullscreen.Height() > 0, "Fullscreen preview action has no pointer extent.");
        IO.AddMousePosEvent((Fullscreen.MinimumX + Fullscreen.MaximumX) * .5f, (Fullscreen.MinimumY + Fullscreen.MaximumY) * .5f);
        Integrate(); IO.AddMouseButtonEvent(0,true); Integrate(); IO.AddMouseButtonEvent(0,false); Integrate();
        Confirm(Panel.QueryFullscreenPreview() && Panel.QueryPreviewConfiguration().Zoom == 1 &&
            Panel.QueryPreviewConfiguration().PanX == 0 && Panel.QueryPreviewConfiguration().PanY == 0,
            "Fullscreen preview did not open with the specimen fitted and centred.");
        const ImVec2 FullscreenCentre(IO.DisplaySize.x * .5f, IO.DisplaySize.y * .5f);
        IO.AddMousePosEvent(FullscreenCentre.x, FullscreenCentre.y);
        Integrate(); IO.AddMouseButtonEvent(0,true); Integrate();
        IO.AddMousePosEvent(FullscreenCentre.x + 24, FullscreenCentre.y + 18); Integrate();
        IO.AddMouseButtonEvent(0,false); Integrate();
        Confirm(Panel.QueryPreviewConfiguration().Yaw != 0 && Panel.QueryPreviewConfiguration().Pitch != 0,
            "Fullscreen canvas drag did not orbit the specimen.");
        IO.AddKeyEvent(ImGuiKey_Escape,true); Integrate(); IO.AddKeyEvent(ImGuiKey_Escape,false); Integrate();
        Confirm(!Panel.QueryFullscreenPreview(), "Escape did not close fullscreen preview.");
        Confirm(Notifications >= 12, "Preview settings callbacks were not delivered.");
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
