/*====================================================================================================================================
                                                     LOGSTREAMPANEL.CPP
====================================================================================================================================*/
// 🧩 ImGui recording for the Log Stream panel: level + channel filters are added from dropdowns into an
//    active chip list; removing a chip returns it to its dropdown (approved HTML prototype behaviour).

#include "LogStreamPanel.h"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace Frontier
{
namespace
{

constexpr std::array<const char*, 5> LevelNames = { "ERROR", "WARN", "INFO", "DEBUG", "TRACE" };
constexpr std::array<ImVec4, 5> LevelColours = {
    ImVec4(0.97f, 0.44f, 0.44f, 1.0f), ImVec4(0.98f, 0.57f, 0.24f, 1.0f), ImVec4(0.24f, 0.86f, 0.52f, 1.0f),
    ImVec4(0.22f, 0.74f, 0.97f, 1.0f), ImVec4(0.65f, 0.55f, 0.98f, 1.0f)
};

}   // namespace

void LogStreamPanel::RecordInterface() noexcept
{
    if (QueryPhase() != ActivationPhase::Active) return;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Log Stream")) { ImGui::End(); return; }
    ConstructHostedInterface();
    ImGui::End();
}

void LogStreamPanel::ConstructHostedInterface() noexcept
{

    // ① Filter dropdowns: picking an entry activates it and removes it from the dropdown.
    LogStreamFilter Filter;
    ImGui::SetNextItemWidth(210.0f);
    if (ImGui::BeginCombo("##addLevel", "+ Add level filter..."))
    {
        for (uint32_t Level = 0; Level < 5; ++Level)
            if (!ActiveLevels[Level] && ImGui::Selectable(LevelNames[Level])) ActiveLevels[Level] = true;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(230.0f);
    if (ImGui::BeginCombo("##addChannel", "+ Add division filter..."))
    {
        for (const std::string& Channel : KnownChannels)
            if (std::find(ActiveChannels.begin(), ActiveChannels.end(), Channel) == ActiveChannels.end() &&
                ImGui::Selectable(Channel.c_str()))
                ActiveChannels.push_back(Channel);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0f);
    ImGui::InputTextWithHint("##search", "search stream...", Search, sizeof(Search));
    ImGui::SameLine();
    if (ImGui::Button(Paused ? "Resume" : "Pause")) Paused = !Paused;
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &Follow);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) Stream.Clear();

    // ② Active filter chips: the x returns the filter to its dropdown.
    bool AnyChips = false;
    for (uint32_t Level = 0; Level < 5; ++Level)
    {
        if (!ActiveLevels[Level]) continue;
        AnyChips = true;
        ImGui::PushStyleColor(ImGuiCol_Button, LevelColours[Level]);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
        if (ImGui::SmallButton((std::string(LevelNames[Level]) + " x##lvl" + std::to_string(Level)).c_str()))
            ActiveLevels[Level] = false;
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
    }
    for (size_t Index = 0; Index < ActiveChannels.size();)
    {
        AnyChips = true;
        if (ImGui::SmallButton((ActiveChannels[Index] + " x##ch" + std::to_string(Index)).c_str()))
        {
            ActiveChannels.erase(ActiveChannels.begin() + int(Index));
            continue;
        }
        ++Index;
        ImGui::SameLine();
    }
    if (AnyChips) ImGui::NewLine();

    // ③ Assemble the filter exactly as the channel predicate expects it.
    Filter.LevelMask = 0;
    bool AnyLevel = false;
    for (uint32_t Level = 0; Level < 5; ++Level)
        if (ActiveLevels[Level]) { Filter.LevelMask |= 1u << Level; AnyLevel = true; }
    if (!AnyLevel) Filter.LevelMask = 0x1Fu;
    Filter.Channels = ActiveChannels;
    Filter.Search = Search;

    // ④ Stream region: level-coloured rows, newest at the bottom, follow = autoscroll.
    if (!Paused)
    {
        Shown = Stream.Snapshot(Filter);
        for (const LogStreamEntry& Entry : Shown)
            if (std::find(KnownChannels.begin(), KnownChannels.end(), Entry.Channel) == KnownChannels.end())
                KnownChannels.push_back(Entry.Channel);
    }
    ImGui::BeginChild("LogStreamRegion", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const LogStreamEntry& Entry : Shown)
    {
        ImGui::TextDisabled("f%llu", (unsigned long long)Entry.Frame);
        ImGui::SameLine();
        ImGui::TextColored(LevelColours[uint32_t(Entry.Level)], "%-5s", LevelNames[uint32_t(Entry.Level)]);
        ImGui::SameLine();
        ImGui::TextDisabled("%-14s", Entry.Channel.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(Entry.Message.c_str());
    }
    if (Follow && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

}   // namespace Frontier
