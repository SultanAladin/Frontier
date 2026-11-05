/*==========================================================================================================================================
                                                             WORLDREGISTER.CPP
==========================================================================================================================================*/
// 🧩 Surveying a WorldSpace into register entries and writing them as the Markdown document beside the level.

#include "Frontier/Engine/Scene/WorldRegister.h"

#include "Frontier/Engine/Scene/WorldSpace.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace Frontier
{

namespace
{

std::array<double, 3> ToDouble(const float Coordinates[3])
{
    return { Coordinates[0], Coordinates[1], Coordinates[2] };
}

// 📝 A table cell: at most Width characters (UTF-8 aware), pipes escaped, the cut marked with an ellipsis.
std::string FitCell(const std::string& Text, size_t Width)
{
    std::string Escaped;
    for (const char Character : Text) Escaped += Character == '|' ? std::string("\\|") : std::string(1, Character);
    size_t Characters = 0, Cut = Escaped.size();
    for (size_t Index = 0; Index < Escaped.size(); ++Index)
    {
        if ((static_cast<unsigned char>(Escaped[Index]) & 0xC0) == 0x80) continue;
        if (Characters == Width - 1) Cut = Index;
        ++Characters;
    }
    if (Characters <= Width) return Escaped;
    return Escaped.substr(0, Cut) + "…";
}

std::string FormatTriple(const std::array<double, 3>& Coordinates, const char* Separator)
{
    char Text[96];
    std::snprintf(Text, sizeof(Text), "%.1f%s%.1f%s%.1f", Coordinates[0], Separator, Coordinates[1], Separator, Coordinates[2]);
    return Text;
}

} // namespace

const char* PhysicsParticipationName(PhysicsParticipation Participation) noexcept
{
    switch (Participation)
    {
        case PhysicsParticipation::None:     return "—";
        case PhysicsParticipation::Moving:   return "Moving";
        case PhysicsParticipation::Fixed:    return "Fixed";
        case PhysicsParticipation::Held:     return "Held";
        case PhysicsParticipation::Fracture: return "Fracture";
        case PhysicsParticipation::Vehicle:  return "Vehicle";
        case PhysicsParticipation::Tyre:     return "Tyre";
        case PhysicsParticipation::Skipped:  return "Skipped";
    }
    return "—";
}

void WorldRegister::Survey(const WorldSpace& World)
{
    WorldName = World.QueryWorldName();
    LevelPath = World.QueryLoadedLevelPath();
    Entries.clear();
    uint32_t Order = 0;
    for (const SceneRecordBase* Record : World.EnumerateRecords())
    {
        WorldRegisterEntry Entry;
        Entry.Order          = ++Order;
        Entry.Token          = Record->QueryToken();
        Entry.Type           = Record->QueryTypeIdentifier();
        Entry.Name           = Record->QueryRecordName();
        Entry.Classification = Record->Classification();
        if (const auto* Geometry = dynamic_cast<const GeometryRecord*>(Record))
        {
            const GeometryConfiguration& Configuration = Geometry->QueryConfiguration();
            const auto&                  Resolved      = Configuration.Resolved;
            Entry.Group            = Configuration.GroupName;
            Entry.Visible          = Configuration.Visible;
            Entry.Copies           = Resolved.Shown;
            Entry.Spatial          = true;
            Entry.PositionMetres   = ToDouble(Configuration.Spatial.PositionMetres);
            Entry.MinimumMetres    = ToDouble(Resolved.BoundsMinimum);
            Entry.MaximumMetres    = ToDouble(Resolved.BoundsMaximum);
            Entry.Bounded          = Entry.MaximumMetres[0] > Entry.MinimumMetres[0] && Entry.MaximumMetres[1] > Entry.MinimumMetres[1] &&
                                     Entry.MaximumMetres[2] > Entry.MinimumMetres[2];
            Entry.PhysicsAuthored  = Configuration.Physics;
            Entry.FractureAuthored = Configuration.Fracture.Enabled;
        }
        else if (const auto* Rigid = dynamic_cast<const RigidBodyRecord*>(Record))
        {
            Entry.Spatial         = true;
            Entry.PositionMetres  = ToDouble(Rigid->QuerySpatial().PositionMetres);
            Entry.PhysicsAuthored = Rigid->QueryPhysics().Enabled;
        }
        else if (const auto* Thermal = dynamic_cast<const ThermalBodyRecord*>(Record))
        {
            Entry.Spatial         = true;
            Entry.PositionMetres  = ToDouble(Thermal->QuerySpatial().PositionMetres);
            Entry.PhysicsAuthored = Thermal->QueryPhysics().Enabled;
        }
        else if (const auto* Vessel = dynamic_cast<const ChemicalVesselRecord*>(Record))
        {
            Entry.Spatial        = true;
            Entry.PositionMetres = ToDouble(Vessel->QuerySpatial().PositionMetres);
        }
        else if (const auto* SpatialRecord = dynamic_cast<const SpatialNodeRecord*>(Record))
        {
            Entry.Spatial        = true;
            Entry.PositionMetres = ToDouble(SpatialRecord->QuerySpatial().PositionMetres);
        }
        else if (const auto* Vehicle = dynamic_cast<const VehicleRecord*>(Record))
        {
            const VehicleConfiguration& Configuration = Vehicle->QueryConfiguration();
            Entry.Group           = Configuration.GroupName;
            Entry.Visible         = Configuration.Visible;
            Entry.Spatial         = true;
            Entry.PositionMetres  = ToDouble(Configuration.Spatial.PositionMetres);
            Entry.PhysicsAuthored = Configuration.Physics;
        }
        else if (const auto* Tyre = dynamic_cast<const TyreRecord*>(Record))
        {
            const TyreConfiguration& Configuration = Tyre->QueryConfiguration();
            Entry.PhysicsAuthored = Configuration.Enclosed;
            Entry.Group           = Configuration.SetName;
            static constexpr const char* CornerNames[4] = { "FL", "FR", "RL", "RR" };
            std::string EnclosureSummary = std::string(CornerNames[Configuration.Corner & 3u]) + " · " + Configuration.Compound + " · " +
                                           std::to_string(static_cast<int>(std::llround(Configuration.ColdPressure * 1.0e-3f))) + " kPa";
            if (const SceneRecordBase* Enclosing = const_cast<WorldSpace&>(World).FindRecord(Record->QueryParentToken()))
                EnclosureSummary += (Configuration.Enclosed ? " · fitted to " : " · pit lane for ") + Enclosing->QueryRecordName();
            Entry.Remark = std::move(EnclosureSummary);
        }
        Entries.push_back(std::move(Entry));
    }
}

WorldRegisterEntry* WorldRegister::Find(RecordToken Token) noexcept
{
    for (WorldRegisterEntry& Entry : Entries)
        if (Entry.Token == Token) return &Entry;
    return nullptr;
}

const WorldRegisterEntry* WorldRegister::Find(RecordToken Token) const noexcept
{
    for (const WorldRegisterEntry& Entry : Entries)
        if (Entry.Token == Token) return &Entry;
    return nullptr;
}

size_t WorldRegister::CountParticipation(PhysicsParticipation Participation) const noexcept
{
    return static_cast<size_t>(std::count_if(Entries.begin(), Entries.end(), [&](const WorldRegisterEntry& Entry) { return Entry.Physics == Participation; }));
}

std::string WorldRegister::EncodeMarkdown() const
{
    std::ostringstream Text;
    Text << "# World Register · " << WorldName << "\n\n";
    Text << "Every object of `" << (LevelPath.empty() ? std::string("(unsaved level)") : LevelPath.filename().string())
         << "` and the part it takes in the physics,\nwritten by the physics extension when the simulation starts. "
            "Generated: edits are overwritten.\n\n";
    Text << "## Physics parts\n\n";
    Text << "| Part     | Objects |\n";
    Text << "|----------|--------:|\n";
    const PhysicsParticipation Parts[] = { PhysicsParticipation::Moving,   PhysicsParticipation::Fixed,   PhysicsParticipation::Held,
                                           PhysicsParticipation::Fracture, PhysicsParticipation::Vehicle, PhysicsParticipation::Tyre,
                                           PhysicsParticipation::Skipped,  PhysicsParticipation::None };
    for (const PhysicsParticipation Part : Parts)
    {
        char Row[64];
        std::snprintf(Row, sizeof(Row), "| %-8s | %7zu |\n", Part == PhysicsParticipation::None ? "None" : PhysicsParticipationName(Part),
                      CountParticipation(Part));
        Text << Row;
    }
    char Total[64];
    std::snprintf(Total, sizeof(Total), "| %-8s | %7zu |\n\n", "Total", Entries.size());
    Text << Total;
    Text << "## Objects\n\n";
    Text << "| #   | Object                 | Type         | Physics  | Body  | Copies | Position (m)       | Size (m)         |\n";
    Text << "|-----|------------------------|--------------|----------|-------|--------|--------------------|------------------|\n";
    for (const WorldRegisterEntry& Entry : Entries)
    {
        const std::string Body     = Entry.Body == UINT32_MAX ? std::string("—") : std::to_string(Entry.Body);
        const std::string Position = Entry.Spatial ? FormatTriple(Entry.PositionMetres, ", ") : std::string("—");
        const std::array<double, 3> Size = { Entry.MaximumMetres[0] - Entry.MinimumMetres[0], Entry.MaximumMetres[1] - Entry.MinimumMetres[1],
                                             Entry.MaximumMetres[2] - Entry.MinimumMetres[2] };
        const std::string Extent   = Entry.Bounded ? FormatTriple(Size, " × ") : std::string("—");
        char Row[512];
        std::snprintf(Row, sizeof(Row), "| %-3u | %s | %s | %-8s | %-5s | %-6u | %s | %s |\n", Entry.Order, FitCell(Entry.Name, 22).c_str(),
                      FitCell(Entry.Type, 12).c_str(), PhysicsParticipationName(Entry.Physics), Body.c_str(), Entry.Copies,
                      FitCell(Position, 18).c_str(), FitCell(Extent, 16).c_str());
        Text << Row;
    }
    bool Heading = false;
    for (const WorldRegisterEntry& Entry : Entries)
    {
        if (Entry.Remark.empty()) continue;
        if (!Heading) Text << "\n## Remarks\n\n";
        Heading = true;
        Text << "- **" << Entry.Name << "**: " << Entry.Remark << "\n";
    }
    return Text.str();
}

bool WorldRegister::WriteMarkdown(const std::filesystem::path& DocumentPath, std::string* OutError) const
{
    std::error_code Fault;
    if (DocumentPath.has_parent_path()) std::filesystem::create_directories(DocumentPath.parent_path(), Fault);
    std::ofstream File(DocumentPath, std::ios::binary | std::ios::trunc);
    if (!File)
    {
        if (OutError) *OutError = "cannot write " + DocumentPath.string();
        return false;
    }
    const std::string Document = EncodeMarkdown();
    File.write(Document.data(), static_cast<std::streamsize>(Document.size()));
    return static_cast<bool>(File);
}

std::filesystem::path WorldRegister::ResolveDocumentPath(const std::filesystem::path& LevelSpacePath)
{
    return LevelSpacePath.parent_path() / (LevelSpacePath.stem().string() + ".WorldRegister.md");
}

} // namespace Frontier
