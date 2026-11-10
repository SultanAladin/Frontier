/*==========================================================================================================================================
                                                          VEHICLESCENEFIXTURE.CPP
==========================================================================================================================================*/
// 🧩 The sample VehicleHMI scene the workspace opens on: vehicle + tyres + proving grounds, owned records.

#include "Workspace/VehicleSceneFixture.h"

#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SceneRecord.h"

#include <string>

namespace Frontier
{

VehicleSceneFixture::VehicleSceneFixture()
{
    const RecordToken Vehicle = AddRecord(RecordClassification::Vehicle, "VehicleHMI Test Vehicle", {}, true);
    (void)AddRecord(RecordClassification::Tyre, "Front Left", Vehicle, true);
    (void)AddRecord(RecordClassification::Tyre, "Front Right", Vehicle, true);
    (void)AddRecord(RecordClassification::Tyre, "Rear Left", Vehicle, true);
    (void)AddRecord(RecordClassification::Tyre, "Rear Right", Vehicle, false);
    const RecordToken Grounds = AddRecord(RecordClassification::Environment, "Proving Grounds", {}, true);
    (void)AddRecord(RecordClassification::Geometry, "Test Cube", Grounds, true);
    (void)AddRecord(RecordClassification::Geometry, "Ground Plane", Grounds, true);
    (void)AddRecord(RecordClassification::Illumination, "Key Light", {}, true);
    (void)AddRecord(RecordClassification::Camera, "Chase Camera", {}, true);
}

VehicleSceneFixture::~VehicleSceneFixture() = default;

RecordToken VehicleSceneFixture::AddRecord(RecordClassification Classification, const char* Name, RecordToken Parent,
                                           bool Visible)
{
    auto              Record    = std::make_unique<SceneRecord>(Classification);
    SceneDirectory&   Directory = SceneDirectory::Instance();
    const RecordToken Token     = Record->Token();
    (void)Directory.ConfigureName(Token, Name);
    if (Parent.IsValid())
    {
        (void)Directory.ConfigureParent(Token, Parent);
    }
    (void)Directory.ConfigureVisible(Token, Visible);
    Records.push_back(std::move(Record));
    return Token;
}

RecordToken VehicleSceneFixture::Spawn(RecordClassification Classification, RecordToken Parent)
{
    ++SpawnedCount;
    const std::string Name = std::string("New ") + RecordClassificationName(Classification) + " " +
                             std::to_string(SpawnedCount);
    return AddRecord(Classification, Name.c_str(), Parent, true);
}

} // namespace Frontier
