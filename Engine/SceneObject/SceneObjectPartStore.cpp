//========================================================================================================================
//                                                  SCENEOBJECTPARTSTORE.CPP
//========================================================================================================================
// 🧩 Rule enforcement for the flat part list. Links are checked when a part is added, and again by Validate.
//========================================================================================================================

#include "SceneObjectPartStore.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace Frontier {

namespace {

float Clamp(float Value, float Minimum, float Maximum) noexcept
{
    return std::min(Maximum, std::max(Minimum, Value));
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LINK RULES
//------------------------------------------------------------------------------------------------------------------------

SceneObjectResult SceneObjectPartStore::CheckLinks(SceneObjectPartClass Class, int32_t HostIndex,
                                                   int32_t DataIndex) const noexcept
{
    const SceneObjectHostRule Rule = SceneObjectHostRuleOf(Class);
    const int32_t Count            = static_cast<int32_t>(Entries.size());

    if (Rule.RequiresHost)
    {
        if (HostIndex < 0 || HostIndex >= Count)
            return SceneObjectResult::MissingHost;
        if (Entries[static_cast<size_t>(HostIndex)].Class != Rule.Host)
            return SceneObjectResult::HostClassMismatch;
    }
    else if (HostIndex != -1)
    {
        return SceneObjectResult::UnexpectedHost;
    }

    if (Rule.RequiresData)
    {
        if (DataIndex < 0 || DataIndex >= Count)
            return SceneObjectResult::MissingData;
        if (Entries[static_cast<size_t>(DataIndex)].Class != Rule.Data)
            return SceneObjectResult::DataClassMismatch;
    }
    else if (DataIndex != -1)
    {
        return SceneObjectResult::UnexpectedData;
    }

    return SceneObjectResult::Accepted;
}

SceneObjectResult SceneObjectPartStore::ResolveValue(size_t Index, std::string_view Name,
                                                     SceneObjectPropertyKind Expected, size_t& OutValueIndex) const noexcept
{
    if (Index >= Entries.size())
        return SceneObjectResult::IndexOutOfRange;

    const SceneObjectPartEntry& Entry = Entries[Index];
    const SceneObjectPropertyDescriptor* Descriptor = FindSceneObjectProperty(Entry.Class, Name);
    if (Descriptor == nullptr)
        return SceneObjectResult::UnknownProperty;

    const bool KindMatches = IsNumericKind(Expected) ? IsNumericKind(Descriptor->Kind) : Descriptor->Kind == Expected;
    if (!KindMatches)
        return SceneObjectResult::WrongPropertyKind;

    const SceneObjectPropertySpan Span = SceneObjectPropertiesOf(Entry.Class);
    OutValueIndex = static_cast<size_t>(Descriptor - Span.First);
    return SceneObjectResult::Accepted;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       QUERIES
//------------------------------------------------------------------------------------------------------------------------

SceneObjectResult SceneObjectPartStore::ReadNumber(size_t Index, std::string_view Name, float& OutValue) const noexcept
{
    size_t ValueIndex = 0u;
    const SceneObjectResult Result = ResolveValue(Index, Name, SceneObjectPropertyKind::Length, ValueIndex);
    if (Result != SceneObjectResult::Accepted)
        return Result;
    OutValue = Entries[Index].Values[ValueIndex].Number;
    return SceneObjectResult::Accepted;
}

bool SceneObjectPartStore::PoleLength(float& OutMetres) const noexcept
{
    for (const SceneObjectPartEntry& Shaft : Entries)
    {
        if (Shaft.Class != SceneObjectPartClass::Shaft)
            continue;
        if (Shaft.HostIndex < 0 || static_cast<size_t>(Shaft.HostIndex) >= Entries.size())
            return false;
        const SceneObjectPartEntry& Plinth = Entries[static_cast<size_t>(Shaft.HostIndex)];
        if (Plinth.Class != SceneObjectPartClass::Plinth)
            return false;

        float LampHeight = 0.0f, BaseHeight = 0.0f;
        const size_t ShaftIndex = static_cast<size_t>(&Shaft - Entries.data());
        if (ReadNumber(ShaftIndex, "Lamp Height", LampHeight) != SceneObjectResult::Accepted)
            return false;
        if (ReadNumber(static_cast<size_t>(Shaft.HostIndex), "Base Height", BaseHeight) != SceneObjectResult::Accepted)
            return false;
        OutMetres = LampHeight - BaseHeight;
        return true;
    }
    return false;
}

bool SceneObjectPartStore::Validate(std::vector<std::string>* Problems) const
{
    bool Valid = true;
    auto Report = [&](size_t Index, const char* Text) {
        Valid = false;
        if (Problems != nullptr)
        {
            char Line[160];
            std::snprintf(Line, sizeof(Line), "entry %zu (%s): %s", Index,
                          SceneObjectPartClassName(Entries[Index].Class), Text);
            Problems->emplace_back(Line);
        }
    };

    for (size_t I = 0u; I < Entries.size(); ++I)
    {
        const SceneObjectPartEntry& Entry = Entries[I];
        const SceneObjectHostRule Rule    = SceneObjectHostRuleOf(Entry.Class);
        const SceneObjectPropertySpan Span = SceneObjectPropertiesOf(Entry.Class);

        if (Entry.Values.size() != Span.Count)
            Report(I, "value count does not match the descriptor table");

        if (Rule.RequiresHost)
        {
            if (Entry.HostIndex < 0 || static_cast<size_t>(Entry.HostIndex) >= I)
                Report(I, "host must be an earlier entry");
            else if (Entries[static_cast<size_t>(Entry.HostIndex)].Class != Rule.Host)
                Report(I, "host has the wrong class");
            else if (Entry.HostSocket != Rule.Socket)
                Report(I, "host socket does not match the rule");
        }
        else if (Entry.HostIndex != -1)
        {
            Report(I, "root part must not name a host");
        }

        if (Rule.RequiresData)
        {
            if (Entry.DataIndex < 0 || static_cast<size_t>(Entry.DataIndex) >= I)
                Report(I, "data link must be an earlier entry");
            else if (Entries[static_cast<size_t>(Entry.DataIndex)].Class != Rule.Data)
                Report(I, "data link has the wrong class");
        }
        else if (Entry.DataIndex != -1)
        {
            Report(I, "part must not name a data entry");
        }

        for (uint32_t V = 0u; V < Span.Count && V < Entry.Values.size(); ++V)
        {
            const SceneObjectPropertyDescriptor& D = Span.First[V];
            const SceneObjectPropertyValue& Value  = Entry.Values[V];
            if (IsNumericKind(D.Kind) && (Value.Number < D.Minimum || Value.Number > D.Maximum))
                Report(I, "numeric value outside its range");
            if (D.Kind == SceneObjectPropertyKind::Choice && Value.Choice >= D.OptionCount)
                Report(I, "choice outside its options");
            if (D.Kind == SceneObjectPropertyKind::Colour)
            {
                const SceneObjectColour C = Value.Colour;
                if (C.Red < 0.0f || C.Red > 1.0f || C.Green < 0.0f || C.Green > 1.0f || C.Blue < 0.0f || C.Blue > 1.0f)
                    Report(I, "colour component outside 0..1");
            }
        }
    }
    return Valid;
}

bool SceneObjectPartStore::TryAdoptParts(std::vector<SceneObjectPartEntry> Candidate, std::vector<std::string>* Problems)
{
    SceneObjectPartStore Trial;
    Trial.Entries = std::move(Candidate);
    if (!Trial.Validate(Problems))
        return false;
    Entries = std::move(Trial.Entries);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        EDITS
//------------------------------------------------------------------------------------------------------------------------

SceneObjectResult SceneObjectPartStore::AddPart(SceneObjectPartClass Class, int32_t HostIndex, int32_t DataIndex)
{
    const SceneObjectResult Links = CheckLinks(Class, HostIndex, DataIndex);
    if (Links != SceneObjectResult::Accepted)
        return Links;

    const SceneObjectHostRule Rule     = SceneObjectHostRuleOf(Class);
    const SceneObjectPropertySpan Span = SceneObjectPropertiesOf(Class);

    SceneObjectPartEntry Entry;
    Entry.Class      = Class;
    Entry.HostIndex  = Rule.RequiresHost ? HostIndex : -1;
    Entry.HostSocket = Rule.RequiresHost ? Rule.Socket : SceneObjectSocket::None;
    Entry.DataIndex  = Rule.RequiresData ? DataIndex : -1;
    Entry.Values.reserve(Span.Count);
    for (uint32_t V = 0u; V < Span.Count; ++V)
        Entry.Values.push_back(Span.First[V].Default);

    Entries.push_back(std::move(Entry));
    return SceneObjectResult::Accepted;
}

SceneObjectResult SceneObjectPartStore::RemovePart(size_t Index)
{
    if (Index >= Entries.size())
        return SceneObjectResult::IndexOutOfRange;

    for (size_t I = 0u; I < Entries.size(); ++I)
    {
        if (I != Index && (Entries[I].HostIndex == static_cast<int32_t>(Index) ||
                           Entries[I].DataIndex == static_cast<int32_t>(Index)))
            return SceneObjectResult::InUse;
    }

    Entries.erase(Entries.begin() + static_cast<std::ptrdiff_t>(Index));
    for (SceneObjectPartEntry& Entry : Entries)
    {
        if (Entry.HostIndex > static_cast<int32_t>(Index))
            --Entry.HostIndex;
        if (Entry.DataIndex > static_cast<int32_t>(Index))
            --Entry.DataIndex;
    }
    return SceneObjectResult::Accepted;
}

SceneObjectResult SceneObjectPartStore::SetNumber(size_t Index, std::string_view Name, float Value)
{
    size_t ValueIndex = 0u;
    const SceneObjectResult Result = ResolveValue(Index, Name, SceneObjectPropertyKind::Length, ValueIndex);
    if (Result != SceneObjectResult::Accepted)
        return Result;

    const SceneObjectPropertyDescriptor* D = FindSceneObjectProperty(Entries[Index].Class, Name);
    Entries[Index].Values[ValueIndex].Number = Clamp(Value, D->Minimum, D->Maximum);
    return SceneObjectResult::Accepted;
}

SceneObjectResult SceneObjectPartStore::SetColour(size_t Index, std::string_view Name, SceneObjectColour Value)
{
    size_t ValueIndex = 0u;
    const SceneObjectResult Result = ResolveValue(Index, Name, SceneObjectPropertyKind::Colour, ValueIndex);
    if (Result != SceneObjectResult::Accepted)
        return Result;

    Entries[Index].Values[ValueIndex].Colour = SceneObjectColour{ Clamp(Value.Red, 0.0f, 1.0f),
                                                                   Clamp(Value.Green, 0.0f, 1.0f),
                                                                   Clamp(Value.Blue, 0.0f, 1.0f) };
    return SceneObjectResult::Accepted;
}

SceneObjectResult SceneObjectPartStore::SetChoice(size_t Index, std::string_view Name, uint32_t Value)
{
    size_t ValueIndex = 0u;
    const SceneObjectResult Result = ResolveValue(Index, Name, SceneObjectPropertyKind::Choice, ValueIndex);
    if (Result != SceneObjectResult::Accepted)
        return Result;

    const SceneObjectPropertyDescriptor* D = FindSceneObjectProperty(Entries[Index].Class, Name);
    Entries[Index].Values[ValueIndex].Choice = std::min(Value, D->OptionCount - 1u);
    return SceneObjectResult::Accepted;
}

}   // namespace Frontier
