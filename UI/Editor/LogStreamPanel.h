/*====================================================================================================================================
                                                      LOGSTREAMPANEL.H
====================================================================================================================================*/
// 🧩 Log Stream panel: the first native panel of the full-engine port — a live view over LogStreamChannel
//    with dropdown-added filters (add -> chip list -> remove returns to the dropdown), mirroring the approved
//    HTML Debug/Telemetry design. This panel is deliberately only the log stream; telemetry follows separately.

#pragma once

#include "Engine/Lifecycle/ActivationSequence.h"
#include "Engine/Interface/LogStreamChannel.h"

#include <array>
#include <string>
#include <vector>

namespace Frontier
{

class LogStreamPanel final : public ActivationSequence
{
public:
    explicit LogStreamPanel(LogStreamChannel& Stream) : Stream(Stream) {}

    void RecordInterface() noexcept;

private:
    LogStreamChannel&        Stream;
    std::array<bool, 5>      ActiveLevels{ false, false, false, false, false };  // none active = unfiltered
    std::vector<std::string> ActiveChannels;
    std::vector<std::string> KnownChannels;
    char                     Search[96] = {};
    bool                     Paused = false;
    bool                     Follow = true;
    std::vector<LogStreamEntry> Shown;   // last snapshot; held while paused
};

}   // namespace Frontier
