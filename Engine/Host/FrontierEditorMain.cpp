/*============================================================================================================================================
                                                          FRONTIEREDITORMAIN.CPP
============================================================================================================================================*/
// 🧩 Multi-workspace FrontierEditor desktop executable launching EditorLifecycle, UVWorkspaceHost, ControlCentre & ContentBrowser

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Host/DesktopWorkspaceRunner.h"

#include <filesystem>
#include <span>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

int main(int Argc, char** Argv)
{
    Frontier::Host::DesktopLaunchOptions Options {};
    Options.DefaultLaunchMode = Frontier::Configuration::HostLaunchMode::FeatureSet;
    Options.WindowTitle       = "Frontier Editor";
    Options.InitialWidthPx    = 1840u;
    Options.InitialHeightPx   = 980u;

    const std::span<const char* const> Args(
        const_cast<const char* const*>(Argv),
        static_cast<std::size_t>(Argc > 0 ? Argc : 0));

    return Frontier::Host::RunDesktopUVApplication(
        Options,
        Args,
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR),
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR));
}
