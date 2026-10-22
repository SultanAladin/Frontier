/*====================================================================================================================================
                                                    STARTUPSEQUENCETRACE.H
====================================================================================================================================*/
// 🧩 Recorded startup initialization: every step timed and journaled, with compile-time retention modes
//    (FRONTIER_DEVELOPMENT / FRONTIER_EDITOR / release) so production never pays for development tracing.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct StartupStepEntry
{
    std::string Name;              // [-] - step name, e.g. "Vulkan device"
    uint64_t    Microseconds = 0;  // [µs] - measured wall duration of the step
    bool        Succeeded = true;  // [-] - step returned success
};

// 📝 Compile-time build mode, selected by the build, never at runtime:
//    FRONTIER_DEVELOPMENT (define)  -> every step retained, failure halts startup
//    FRONTIER_EDITOR (=1)           -> every step retained, failure halts startup
//    release (neither)              -> only failures + the total are retained; tracing costs one timer per step
enum class StartupTraceMode
{
    Development,
    Editor,
    Release
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STARTUP TRACE
//------------------------------------------------------------------------------------------------------------------------

class StartupSequenceTrace
{
public:
    using StepFunction = std::function<bool()>;

    void AddStep(std::string Name, StepFunction Step);
    // Runs steps in registration order; stops at the first failure. Every executed step is timed.
    // Returns true when every step succeeded.
    bool Run();
    void Clear();

    [[nodiscard]] static constexpr StartupTraceMode QueryMode() noexcept
    {
#if defined(FRONTIER_DEVELOPMENT)
        return StartupTraceMode::Development;
#elif defined(FRONTIER_EDITOR) && FRONTIER_EDITOR
        return StartupTraceMode::Editor;
#else
        return StartupTraceMode::Release;
#endif
    }
    [[nodiscard]] static const char* QueryModeName() noexcept
    {
        switch (QueryMode())
        {
            case StartupTraceMode::Development: return "development";
            case StartupTraceMode::Editor:      return "editor";
            case StartupTraceMode::Release:     return "release";
        }
        return "release";
    }

    [[nodiscard]] const std::vector<StartupStepEntry>& QueryEntries() const noexcept { return Entries; }
    [[nodiscard]] uint64_t QueryTotalMicroseconds() const noexcept { return TotalMicroseconds; }
    [[nodiscard]] bool     QuerySucceeded() const noexcept { return Succeeded; }

private:
    struct PendingStep
    {
        std::string  Name;
        StepFunction Step;
    };
    std::vector<PendingStep>     Pending;
    std::vector<StartupStepEntry> Entries;
    uint64_t                     TotalMicroseconds = 0;
    bool                         Succeeded = true;
};

}   // namespace Frontier
