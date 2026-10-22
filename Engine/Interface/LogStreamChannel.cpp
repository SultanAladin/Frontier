/*====================================================================================================================================
                                                    LOGSTREAMCHANNEL.CPP
====================================================================================================================================*/
// 🧩 Bounded-ring structured log stream; filtering is a pure predicate so the panel and tests share it.

#include "Engine/Interface/LogStreamChannel.h"

#include <algorithm>
#include <cctype>

namespace Frontier
{

bool LogStreamFilter::Passes(const LogStreamEntry& Entry) const
{
    if (!(LevelMask & (1u << uint32_t(Entry.Level)))) return false;
    if (!Channels.empty() && std::find(Channels.begin(), Channels.end(), Entry.Channel) == Channels.end()) return false;
    if (!Search.empty())
    {
        const auto Contains = [](const std::string& Haystack, const std::string& Needle)
        {
            return std::search(Haystack.begin(), Haystack.end(), Needle.begin(), Needle.end(),
                               [](char A, char B) { return std::tolower(uint8_t(A)) == std::tolower(uint8_t(B)); }) != Haystack.end();
        };
        if (!Contains(Entry.Message, Search) && !Contains(Entry.Channel, Search)) return false;
    }
    return true;
}

void LogStreamChannel::Emit(LogLevel Level, std::string Channel, uint64_t Frame, std::string Message)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Ring.size() < Capacity) Ring.push_back({});
    Ring[Head] = { Level, std::move(Channel), Frame, std::move(Message) };
    Head = (Head + 1) % Capacity;
    Count = std::min(Count + 1, Capacity);
}

void LogStreamChannel::Clear()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    Head = 0;
    Count = 0;
}

std::vector<LogStreamEntry> LogStreamChannel::Snapshot(const LogStreamFilter& Filter) const
{
    std::lock_guard<std::mutex> Lock(Mutex);
    std::vector<LogStreamEntry> Result;
    Result.reserve(Count);
    const size_t Oldest = (Head + Capacity - Count) % Capacity;
    for (size_t I = 0; I < Count; ++I)
    {
        const LogStreamEntry& Entry = Ring[(Oldest + I) % Capacity];
        if (Filter.Passes(Entry)) Result.push_back(Entry);
    }
    return Result;
}

size_t LogStreamChannel::QueryRetainedCount() const
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return Count;
}

}   // namespace Frontier
