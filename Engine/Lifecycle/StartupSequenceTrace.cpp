/*====================================================================================================================================
                                                   STARTUPSEQUENCETRACE.CPP
====================================================================================================================================*/
// 🧩 Timed, journaled startup steps; retention follows the compile-time mode declared in StartupSequenceTrace.h.

#include "Engine/Lifecycle/StartupSequenceTrace.h"

#include <chrono>

namespace Frontier
{

void StartupSequenceTrace::AddStep(std::string Name, StepFunction Step)
{
    Pending.push_back({ std::move(Name), std::move(Step) });
}

void StartupSequenceTrace::Clear()
{
    Pending.clear();
    Entries.clear();
    TotalMicroseconds = 0;
    Succeeded = true;
}

bool StartupSequenceTrace::Run()
{
    Entries.clear();
    TotalMicroseconds = 0;
    Succeeded = true;
    const bool RetainAll = QueryMode() != StartupTraceMode::Release;
    for (const PendingStep& PendingStep : Pending)
    {
        const auto Begin = std::chrono::steady_clock::now();
        const bool Ok = PendingStep.Step ? PendingStep.Step() : false;
        const auto End = std::chrono::steady_clock::now();
        const uint64_t Micros = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(End - Begin).count());
        TotalMicroseconds += Micros;
        // 📝 Release retains only failures (plus the running total above): a shipped build carries no
        //    per-step strings, while development/editor keep the full journal for the log panel.
        if (RetainAll || !Ok) Entries.push_back({ PendingStep.Name, Micros, Ok });
        if (!Ok)
        {
            Succeeded = false;
            break;   // a failed startup step halts the sequence in every mode; later steps never run half-initialized
        }
    }
    return Succeeded;
}

}   // namespace Frontier
