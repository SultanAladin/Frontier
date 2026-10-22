/*====================================================================================================================================
                                                    LOGSTREAMWORKSPACE.H
====================================================================================================================================*/
// 🧩 Project window host for the port's first workspace: exactly one docked panel — the Log Stream —
//    inside the copied PanelDockspace, with the Control Centre notch drawn by the application shell.
//    No other panels are registered; telemetry and scene panels join this host later, one at a time.

#pragma once

#include "Engine/Interface/LogStreamChannel.h"
#include "Engine/Interface/PanelDockspace.h"
#include "UI/Editor/LogStreamPanel.h"

#include <string>

namespace Frontier
{

class LogStreamWorkspace
{
  public:
    LogStreamWorkspace();

    void               ConfigureProject(std::string Name) { ProjectName = std::move(Name); }
    [[nodiscard]] const std::string& QueryProject() const noexcept { return ProjectName; }

    [[nodiscard]] PanelDockspace& QueryPanels() noexcept { return Panels; }
    [[nodiscard]] LogStreamChannel& QueryStream() noexcept { return Stream; }

    bool ConfigurePlatform() noexcept { return Panels.ConfigurePlatform(); }
    void ConstructInterface(float Scale);

  private:
    LogStreamChannel Stream;
    LogStreamPanel   Panel{ Stream };
    PanelDockspace   Panels;
    std::string      ProjectName = "Untitled project";
};

} // namespace Frontier
