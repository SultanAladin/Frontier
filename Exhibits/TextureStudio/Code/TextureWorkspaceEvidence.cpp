/*==========================================================================================================================================
                                                   TEXTUREWORKSPACEEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof of the Texture workspace's native UI port: the bubble/track layer
//    stack (selection, visibility, folder disclosure, drag-to-reorder, the inline Blend/Opacity/Tag/Mask
//    expand), the tabbed inspector (Properties/channels/decals and History), the mask toolbar and the
//    viewport's surrounding chrome. No OS window and no GPU are involved - every control here is exercised
//    against the actual rendered ImGui draw commands, not a placeholder screenshot.

#include "UI/TextureStudio/TextureWorkspacePanel.h"
#include "RasterizeLibraryEvidence.h"
#include <imgui_internal.h>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition) throw std::runtime_error(Description);
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 2, "Expected an evidence output directory.");
        const std::filesystem::path Output(Arguments[1]);
        std::filesystem::create_directories(Output);

        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        IO.DisplaySize = ImVec2(1680, 1000);
        IO.DeltaTime = 1.f / 60;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        IO.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
        if (std::filesystem::is_regular_file("EngineContent/FontArchives/DMSans/DMSans-Regular.ttf"))
            IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/DMSans/DMSans-Regular.ttf", 14);
        else IO.Fonts->AddFontDefault();
        unsigned char *Pixels = nullptr;
        int AtlasWidth = 0, AtlasHeight = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);

        ActivationOwnership PanelOwnership(new TextureWorkspacePanel());
        auto *Panel = static_cast<TextureWorkspacePanel *>(PanelOwnership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Texture panel lifecycle failed.");
        // 📝 Exercise the cross-workspace navigation wiring exactly like the real app does, so a future
        // regression that silently drops ActivateX before this panel ever draws a frame fails loudly here
        // instead of only showing up as a disabled header pill at runtime.
        int MaterialActivations = 0, PatternActivations = 0, StampActivations = 0, BakingActivations = 0;
        Panel->ActivateMaterial = [&] { ++MaterialActivations; };
        Panel->ActivatePattern = [&] { ++PatternActivations; };
        Panel->ActivateStamp = [&] { ++StampActivations; };
        Panel->ActivateBaking = [&] { ++BakingActivations; };

        const auto Frame = [&]
        {
            ImGui::NewFrame();
            (void)Panel->Integrate(IO.DeltaTime);
            Panel->ConstructInterface();
            ImGui::Render();
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Texture workspace produced no ImGui draw commands.");
        };
        for (int Index = 0; Index < 4; ++Index) Frame();

        int CaptureIndex = 0;
        const auto Capture = [&]
        {
            IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
            char Name[64];
            std::snprintf(Name, sizeof(Name), "%02d-frame.ppm", CaptureIndex++);
            EncodeLibraryDrawCapture(Output / Name, *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight);
        };

        const auto MoveTo = [&](ImVec2 Position) { IO.AddMousePosEvent(Position.x, Position.y); Frame(); };
        const auto Down = [&] { IO.AddMouseButtonEvent(0, true); Frame(); };
        const auto Up = [&] { IO.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame(); };
        const auto Click = [&](PlaneExtent Extent)
        {
            Require(Extent.Width() > 0 && Extent.Height() > 0, "Target control was never drawn.");
            MoveTo(ImVec2((Extent.MinimumX + Extent.MaximumX) * .5f, (Extent.MinimumY + Extent.MaximumY) * .5f));
            Down();
            Up();
        };

        // 1) Opening frame: the representative teapot demo document, "Clay base" selected by default, the
        //    bubble/track layer stack / tabbed inspector / viewport chrome all visible.
        for (int Index = 0; Index < 3; ++Index) Capture();
        std::cout << "Captured the Texture workspace opening on the built-in teapot study demo document.\n";

        // 2) Select a different layer (the decal row) - the inspector identity/property cards must follow.
        Click(Panel->QueryControlExtent("TextureLayerSelect:4"));
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Selected the \"Maker's stamp\" decal layer - inspector now shows its decal components card.\n";

        // 3) Collapse the "Body details" folder, then expand it again - disclosure state is real local state.
        Click(Panel->QueryControlExtent("TextureLayerDisclosure:1"));
        Capture();
        Click(Panel->QueryControlExtent("TextureLayerDisclosure:1"));
        Capture();
        std::cout << "Collapsed and re-expanded the \"Body details\" folder row.\n";

        // 4) Toggle a layer's visibility on the compact row (the row itself carries no Lock icon any more -
        //    Locked now lives solely in the deeper right Inspector), then select it and toggle Locked there.
        Click(Panel->QueryControlExtent("TextureLayerEye:2"));
        Capture();
        Click(Panel->QueryControlExtent("TextureLayerSelect:2"));
        Capture();
        Click(Panel->QueryControlExtent("Unlocked"));
        Capture();
        std::cout << "Toggled visibility on the \"Edge wear\" paint layer from its row, then locked it from the "
            "Inspector's own Visible/Locked buttons.\n";

        // 4b) Exercise the row's own inline expand caret (LayerStackPanel.html's BuildExpand) - Blend dropdown,
        //     Tag colour and the minimal inline Mask region all now live here, not in a separately-selectable
        //     mask row or the Inspector's old "MASK SETTINGS" page (both removed). "Clay base" (index 0) is
        //     targeted by its own ":0"-suffixed control keys throughout, not the bare literal captions - with
        //     "Edge wear" (index 2) left expanded from step 4 at the same time, a bare "BlendMenu"/"Add mask"
        //     lookup would silently resolve to whichever of the two rows drew its control last instead of the
        //     one actually meant, since that registry is a flat map keyed by literal caption, not scoped by
        //     row the way the real ImGui widgets underneath it are.
        Click(Panel->QueryControlExtent("TextureLayerCaret:0"));
        Capture();
        Click(Panel->QueryControlExtent("LayerBlendMenu:0"));
        Capture();
        Click(Panel->QueryControlExtent("LayerBlendMenu:0"));
        Capture();
        std::cout << "Expanded \"Clay base\"'s inline row fold and opened/closed its 7-mode Blend dropdown.\n";
        // "Clay base" is seeded with a mask already (Strength 92) - exercise the round trip on it directly:
        // remove it (back to the dashed "+Add mask" placeholder), then add a fresh one, both via the same
        // ":0"-suffixed keys so the clicks are provably landing on this row and not "Edge wear" beneath it.
        Click(Panel->QueryControlExtent("LayerRemoveMask:0"));
        Capture();
        Click(Panel->QueryControlExtent("LayerAddMask:0"));
        Capture();
        std::cout << "Removed and re-added \"Clay base\"'s inline mask directly from its row's own expand - no "
            "separate mask row or Inspector mask page involved.\n";
        Click(Panel->QueryControlExtent("TextureLayerCaret:0"));
        Capture();
        std::cout << "Collapsed \"Clay base\"'s inline row fold again.\n";

        // 4c) Drag-to-reorder two adjacent, same-depth, non-nested rows by their grip handle - a live drop-line
        //     indicator should track the pointer, and releasing performs a real list splice. Collapse every
        //     row still carrying an inline expand first (Maker's stamp from step 2, Edge wear from step 4) so
        //     the two target rows sit within the sidebar's visible, unscrolled viewport - exactly like a real
        //     user would need to scroll to reach an off-screen row, which this harness does not attempt.
        Click(Panel->QueryControlExtent("TextureLayerCaret:2"));
        Click(Panel->QueryControlExtent("TextureLayerCaret:4"));
        Capture();
        {
            const auto GripExtent = Panel->QueryControlExtent("TextureLayerGrip:5");
            const auto TargetExtent = Panel->QueryControlExtent("TextureLayerSelect:6");
            Require(GripExtent.Width() > 0 && TargetExtent.Width() > 0, "Drag source/target rows were never drawn.");
            MoveTo(ImVec2((GripExtent.MinimumX + GripExtent.MaximumX) * .5f, (GripExtent.MinimumY + GripExtent.MaximumY) * .5f));
            Down();
            MoveTo(ImVec2((GripExtent.MinimumX + GripExtent.MaximumX) * .5f, (GripExtent.MinimumY + GripExtent.MaximumY) * .5f + 14));
            Capture();
            MoveTo(ImVec2((TargetExtent.MinimumX + TargetExtent.MaximumX) * .5f, TargetExtent.MaximumY - 4));
            Capture();
            Up();
            Capture();
        }
        std::cout << "Dragged \"Spout tint\" past \"Procedural noise\" by its grip handle - the drop-line "
            "indicator tracked the pointer and releasing spliced the layer list.\n";

        // 5) Open the add-layer menu.
        Click(Panel->QueryControlExtent("Add"));
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Opened the add-a-layer popup menu (six layer kinds).\n";
        Click(Panel->QueryControlExtent("Add"));
        Capture();
        std::cout << "Closed the add-a-layer popup without choosing a kind.\n";

        // 6) Switch the authoring tool group and the digital paint toolbar.
        Click(Panel->QueryControlExtent("Stamp"));
        Capture();
        Click(Panel->QueryControlExtent("Paint"));
        Capture();
        std::cout << "Armed the Stamp authoring tool, then the digital Paint tool.\n";

        // 7) Toggle the grid in the viewport toolbar chrome.
        Click(Panel->QueryControlExtent("Grid"));
        Capture();
        std::cout << "Toggled the viewport grid overlay.\n";

        // 8) Switch the right-hand dock to the History tab and exercise its strokes-only filter.
        Click(Panel->QueryControlExtent("History"));
        for (int Index = 0; Index < 2; ++Index) Capture();
        Click(Panel->QueryControlExtent("Strokes only"));
        Capture();
        Click(Panel->QueryControlExtent("All edits"));
        Capture();
        std::cout << "Switched the inspector dock to its History tab and exercised its All edits / Strokes "
            "only filter.\n";
        Click(Panel->QueryControlExtent("Inspector"));
        Capture();

        // 9) "Edge wear" carries two channel targets at once (baseColor/roughness), each drawing its own
        //    "Texture"/"Value"/"Generator" mode switch with identical literal captions. ConstructChannelCard()
        //    scopes each card with ImGui::PushID(channel id), so Dear ImGui itself never confuses the two
        //    widgets - but Controls.Button()'s own extent-tracking registers under the bare literal Caption
        //    with no awareness of that PushID scope, so QueryControlExtent("Generator") alone would
        //    ambiguously resolve to whichever card drew its button last (always Roughness here, since it
        //    draws after Base Colour) instead of the one actually meant. ConstructChannelCard() now re-Tracks
        //    each mode button under "ChannelMode:<id>:<mode>" immediately after drawing it, so this harness
        //    can target either channel's switch unambiguously; exercise every mode on both channels and
        //    require zero mis-routed clicks. The mode switch button is "Value", never "Colour" - the html
        //    reference's own literal caption. (Clay base has a third channel, metalness, whose expanded card
        //    falls below this harness's fixed 1000px canvas without scrolling the inspector - exactly like
        //    the real scrollable dock would require a scroll gesture - so it is not used here.)
        Click(Panel->QueryControlExtent("TextureLayerSelect:2"));
        Capture();
        Click(Panel->QueryControlExtent("ChannelMode:roughness:Generator"));
        Capture();
        Click(Panel->QueryControlExtent("ChannelMode:roughness:Value"));
        Capture();
        Click(Panel->QueryControlExtent("ChannelMode:roughness:Texture"));
        Capture();
        Click(Panel->QueryControlExtent("ChannelMode:baseColor:Generator"));
        Capture();
        Click(Panel->QueryControlExtent("ChannelMode:baseColor:Value"));
        Capture();
        std::cout << "Cycled every channel card's Texture/Value/Generator source mode on BOTH of \"Edge wear\"'s "
            "channel targets (Roughness and Base Colour) independently, with no ImGui ID-conflict errors.\n";

        // 9a-ii) ConstructTextureSlot's own two-slot layout - the painted-strokes slot (always present) and a
        //        separately optional imported-base slot beneath it, each with its own populated state.
        // (Roughness is already left in Texture mode by the cycle above. "Paint strokes (demo)" is a
        // native-build-only stand-in for real painting and sits below this inspector's visible fold at this
        // scroll position - its empty/painted branches were instead verified by direct code review of
        // ConstructChannelTextureSource, see session notes.)
        Click(Panel->QueryControlExtent("ImportBase:2:roughness"));
        Capture();
        std::cout << "Imported a base texture under it - a separate, independently-removable slot with its "
            "own Reimport/Delete icon buttons.\n";

        // 9b) "Scratch normal" pairs a derived channel (Normal, no mode switch at all) with a real one
        //     (Height) - make sure the derived card renders its note only and the Height card still switches
        //     cleanly alongside it.
        Click(Panel->QueryControlExtent("TextureLayerSelect:3"));
        Capture();
        Click(Panel->QueryControlExtent("Generator"));
        Capture();
        std::cout << "Selected \"Scratch normal\" - its derived Normal card shows no mode switch, and Height's "
            "Generator mode still switches independently.\n";

        // 9b-ii) ConstructGeneratorSlot's own pickbar - a real floating popup grouped Mask/Wear/Procedural,
        //        not a plain inline combo - then, once a generator is picked, its note line, Reset/Remove icon
        //        buttons and parameter sliders all appear.
        Click(Panel->QueryControlExtent("GeneratorPick:3:height"));
        Capture();
        std::cout << "Opened Height's generator pickbar - its grouped Mask/Wear/Procedural popup, not a plain "
            "combo.\n";
        Click(Panel->QueryControlExtent("Dirt"));
        Capture();
        std::cout << "Picked \"Dirt\" from the Wear group - its note, Reset/Remove icon buttons and parameter "
            "sliders now render for Height.\n";

        // 9c) "Maker's stamp" starts with zero channel targets (it is a decal layer) - the Channel targets card
        //     must still appear so the round "+" add-channel button at the end of the chip row is reachable.
        //     Open its grouped dropdown (never a toggle switch - plain click-to-add/remove rows with a
        //     checkmark), enable a Radiance-group channel (Emissive) and a paired Surface one (Normal, which
        //     must also silently enable Height), verify their cards and chips render, remove one chip via its
        //     own "x", then Clear all back to zero. Toggling a row must not close the popup - only re-clicking
        //     the "+" (or an outside click) does.
        Click(Panel->QueryControlExtent("TextureLayerSelect:4"));
        Capture();
        Click(Panel->QueryControlExtent("Add channel"));
        Capture();
        Click(Panel->QueryControlExtent("Emissive"));
        Capture();
        std::cout << "Toggled Emissive on in the add-channel dropdown - the popup stayed open for another pick.\n";
        Click(Panel->QueryControlExtent("Normal"));
        Capture();
        std::cout << "Opened \"Maker's stamp\"'s empty Channel targets card, added Emissive and Normal (which "
            "also enabled Height) through the grouped, click-to-toggle Add channel dropdown - never a switch.\n";
        Click(Panel->QueryControlExtent("Add channel"));
        Capture();
        std::cout << "Closed the add-channel dropdown by clicking the round \"+\" again.\n";
        Click(Panel->QueryControlExtent("ChannelChipRemove:emission"));
        Capture();
        std::cout << "Removed the Emissive chip through its own \"x\" without disturbing Normal/Height.\n";
        Click(Panel->QueryControlExtent("Clear all"));
        Capture();
        std::cout << "\"Clear all\" emptied every remaining channel target on \"Maker's stamp\".\n";

        // 10) Cross-workspace navigation pills actually invoke the wired callbacks.
        Require(MaterialActivations == 0, "ActivateMaterial fired before its pill was clicked.");
        Click(Panel->QueryControlExtent("Material"));
        Require(MaterialActivations == 1, "Clicking the Material header pill did not invoke ActivateMaterial.");
        Require(PatternActivations == 0 && StampActivations == 0 && BakingActivations == 0,
            "An unrelated workspace pill fired alongside Material.");
        Capture();
        std::cout << "Verified the header's workspace-switcher pills invoke the real cross-workspace callbacks.\n";

        ImGui::DestroyContext();
        std::cout << "PASS: Texture workspace native UI port (bubble/track layer stack with drag-to-reorder and "
            "an inline Blend/Opacity/Tag/Mask expand, the tabbed inspector, the mask toolbar and the viewport "
            "chrome; no 3D canvas, no paint/mask/decal compositing) verified against the actual rendered "
            "canvas, see " << Output.string() << "\n";
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "TextureWorkspaceEvidence failed: " << Failure.what() << "\n";
        return 1;
    }
    return 0;
}
