/*====================================================================================================================================
                                                     LOGSTREAMCHANNEL.H
====================================================================================================================================*/
// 🧩 The one structured log stream every engine division emits to: levelled, channel-tagged, frame-tagged
//    records in a bounded ring, consumed by the Log Stream panel and (in development) a file journal.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

enum class LogLevel : uint32_t
{
    Error = 0,
    Warning = 1,
    Info = 2,
    Debug = 3,
    Trace = 4
};

struct LogStreamEntry
{
    LogLevel    Level = LogLevel::Info;  // [-] - severity
    std::string Channel;                 // [-] - emitting division, e.g. "RenderGraph"
    uint64_t    Frame = 0;               // [idx] - frame the record belongs to (0 = startup)
    std::string Message;                 // [-] - record text
};

struct LogStreamFilter
{
    uint32_t                 LevelMask = 0x1Fu;  // [-] - bit per LogLevel; all levels pass by default
    std::vector<std::string> Channels;           // [-] - empty = every channel passes
    std::string              Search;             // [-] - case-insensitive substring; empty = pass
    [[nodiscard]] bool Passes(const LogStreamEntry& Entry) const;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     LOG STREAM
//------------------------------------------------------------------------------------------------------------------------

// 📝 Bounded ring (oldest overwritten). One mutex: emission is rare compared to rendering work, and a
//    single lock keeps ordering total, which the panel's frame tags rely on.
class LogStreamChannel
{
public:
    static constexpr size_t Capacity = 8192;

    void Emit(LogLevel Level, std::string Channel, uint64_t Frame, std::string Message);
    void Clear();

    // Newest-last copy of every retained entry passing the filter (panel polls this per frame).
    [[nodiscard]] std::vector<LogStreamEntry> Snapshot(const LogStreamFilter& Filter) const;
    [[nodiscard]] size_t QueryRetainedCount() const;

private:
    mutable std::mutex        Mutex;
    std::vector<LogStreamEntry> Ring;
    size_t                    Head = 0;    // next write slot
    size_t                    Count = 0;   // retained entries, <= Capacity
};

}   // namespace Frontier
