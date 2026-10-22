/*====================================================================================================================================
                                                    LOGSTREAMWORKSPACE.CPP
====================================================================================================================================*/
// 🧩 See header: one panel only (Log Stream), hosted by the copied dockspace; the notch is the shell's.

#include "UI/Editor/LogStreamWorkspace.h"

#include <imgui.h>

#include <stdexcept>

namespace Frontier
{

LogStreamWorkspace::LogStreamWorkspace()
{
    if (!Panel.Initialize() || !Panel.Activate())
        throw std::runtime_error("Log Stream panel activation failed.");
    if (!Panels.Register({ "logstream", "Log Stream", [&]() { Panel.ConstructHostedInterface(); }, true }))
        throw std::runtime_error("Log Stream panel registration refused.");
    Panels.ConfigureWorkspaceInsets(0, 0);
    ImGui::GetIO().IniFilename = nullptr; // layout is session-only until project persistence lands
}

void LogStreamWorkspace::ConstructInterface(float Scale)
{
    Panels.ConstructInterface(Scale);
}

} // namespace Frontier
