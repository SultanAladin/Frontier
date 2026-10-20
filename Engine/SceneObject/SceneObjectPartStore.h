//========================================================================================================================
//                                                  SCENEOBJECTPARTSTORE.H
//========================================================================================================================
// 🧩 The flat list that is a SceneObject. Adds, removes, edits and validates entries; no entry owns another.
//    Every edit either succeeds or returns the reason it did not, and leaves the store unchanged when it fails.
//========================================================================================================================
#pragma once

#include "SceneObjectTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace Frontier {

class SceneObjectPartStore
{
public:
    //--------------------------------------------------------------------------------------------------------------------
    //                                                    QUERIES
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] size_t Count() const noexcept { return Entries.size(); }
    [[nodiscard]] const std::vector<SceneObjectPartEntry>& Parts() const noexcept { return Entries; }

    // 📝 Reads one stored number. Returns the reason when the name, index or kind is wrong.
    [[nodiscard]] SceneObjectResult ReadNumber(size_t Index, std::string_view Name, float& OutValue) const noexcept;

    // 📝 Pole length in metres: the shaft's Lamp Height minus the Base Height of the plinth it stands on.
    //    False when the store has no shaft, or the shaft's host is not a plinth.
    [[nodiscard]] bool PoleLength(float& OutMetres) const noexcept;

    // 📝 Checks every rule against the current contents. Problems are appended as readable lines when provided.
    [[nodiscard]] bool Validate(std::vector<std::string>* Problems = nullptr) const;

    // 📝 Replaces the contents with a candidate list (a loaded file) only when every rule holds. On false the store is
    //    unchanged, so a corrupt file can never leave a half-built SceneObject behind.
    [[nodiscard]] bool TryAdoptParts(std::vector<SceneObjectPartEntry> Candidate, std::vector<std::string>* Problems = nullptr);

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    EDITS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Appends one part. HostIndex and DataIndex must name earlier entries of the required class.
    SceneObjectResult AddPart(SceneObjectPartClass Class, int32_t HostIndex = -1, int32_t DataIndex = -1);

    // 📝 Removes one part. Refused while another part names it as host or data.
    SceneObjectResult RemovePart(size_t Index);

    // 📝 Numeric edits are clamped to the descriptor range; the stored value is the clamped one.
    SceneObjectResult SetNumber(size_t Index, std::string_view Name, float Value);
    SceneObjectResult SetColour(size_t Index, std::string_view Name, SceneObjectColour Value);

    // 📝 Choice edits are clamped to the last option.
    SceneObjectResult SetChoice(size_t Index, std::string_view Name, uint32_t Value);

private:
    [[nodiscard]] SceneObjectResult CheckLinks(SceneObjectPartClass Class, int32_t HostIndex,
                                               int32_t DataIndex) const noexcept;
    [[nodiscard]] SceneObjectResult ResolveValue(size_t Index, std::string_view Name, SceneObjectPropertyKind Expected,
                                                 size_t& OutValueIndex) const noexcept;

    std::vector<SceneObjectPartEntry> Entries;
};

}   // namespace Frontier
