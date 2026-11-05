/*==========================================================================================================================================
                                                              WORLDREGISTER.H
==========================================================================================================================================*/
// 🧩 The WorldRegister: one entry for every object in a WorldSpace (its type, classification, where it stands, how many copies it
//    shows, its bounds) and the part it takes in the physics (moving, fixed, held, fracture, vehicle …), kept as a Markdown document
//    beside the level for now (<Level>.WorldRegister.md). The physics extension surveys the world into it, records each object's
//    part as it builds the PhysicsSpace, then writes the document.

#pragma once

#include "Frontier/Engine/Scene/RecordClassification.h"
#include "Frontier/Engine/Scene/RecordToken.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{

class WorldSpace;

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class PhysicsParticipation : uint8_t
{
    None,       // not a physical object (environment, settings, interface)
    Moving,     // a moving rigid body written back to its record
    Fixed,      // a fixed collider (the scene's static geometry)
    Held,       // several copies share one transform, so it keeps its place and is not collided against
    Fracture,   // a fracturable object: a fixed collider over its bounds until the fracture work
    Vehicle,    // a vehicle: a moving chassis body and its tyres
    Tyre,       // a tyre enclosed by a vehicle (simulated with it)
    Skipped,    // physical but without a size, hidden or refused by the solver
};

[[nodiscard]] const char* PhysicsParticipationName(PhysicsParticipation Participation) noexcept;

struct WorldRegisterEntry
{
    uint32_t              Order          = 0;       // [-] position in the level
    RecordToken           Token{};
    std::string           Type{};
    std::string           Name{};
    std::string           Group{};
    RecordClassification  Classification = RecordClassification::Unclassified;
    bool                  Visible        = true;
    uint32_t              Copies         = 1;       // [-] copies shown (geometry), 1 otherwise
    bool                  Spatial        = false;   // [-] it stands somewhere (Position is meaningful)
    std::array<double, 3> PositionMetres = { 0.0, 0.0, 0.0 };
    bool                  Bounded        = false;   // [-] Minimum and Maximum are meaningful
    std::array<double, 3> MinimumMetres  = { 0.0, 0.0, 0.0 };
    std::array<double, 3> MaximumMetres  = { 0.0, 0.0, 0.0 };
    bool                  PhysicsAuthored  = false; // [-] the record asks for physics
    bool                  FractureAuthored = false; // [-] the record asks for fracture
    PhysicsParticipation  Physics        = PhysicsParticipation::None;
    uint32_t              Body           = UINT32_MAX;   // [-] PhysicsSpace body index while simulating
    std::string           Remark{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     WORLD REGISTER
//------------------------------------------------------------------------------------------------------------------------

class WorldRegister
{
  public:
    // 📝 Replaces the entries with one per record of World, in level order (physics parts reset to None).
    void Survey(const WorldSpace& World);

    [[nodiscard]] WorldRegisterEntry*                    Find(RecordToken Token) noexcept;
    [[nodiscard]] const WorldRegisterEntry*              Find(RecordToken Token) const noexcept;
    [[nodiscard]] const std::vector<WorldRegisterEntry>& QueryEntries() const noexcept { return Entries; }
    [[nodiscard]] std::vector<WorldRegisterEntry>&       AccessEntries() noexcept { return Entries; }
    [[nodiscard]] size_t                                 CountParticipation(PhysicsParticipation Participation) const noexcept;
    [[nodiscard]] const std::string&                     QueryWorldName() const noexcept { return WorldName; }

    // 📝 The document: a summary of the physics parts, then one row per object.
    [[nodiscard]] std::string EncodeMarkdown() const;
    bool WriteMarkdown(const std::filesystem::path& DocumentPath, std::string* OutError = nullptr) const;
    // 📝 <folder>/<Level>.WorldRegister.md beside a levelspace file.
    [[nodiscard]] static std::filesystem::path ResolveDocumentPath(const std::filesystem::path& LevelSpacePath);

  private:
    std::string                     WorldName{};
    std::filesystem::path           LevelPath{};
    std::vector<WorldRegisterEntry> Entries{};
};

} // namespace Frontier
