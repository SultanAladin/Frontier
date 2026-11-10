/*==========================================================================================================================================
                                                           VEHICLESCENEFIXTURE.H
==========================================================================================================================================*/
// 🧩 The sample VehicleHMI scene the workspace opens on: vehicle + tyres + proving grounds, owned records.

#pragma once

#include "Engine/Scene/RecordClassification.h"
#include "Engine/Scene/RecordToken.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Frontier
{

class SceneRecord;
class SceneDirectoryPanel;

//----------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE SCENE FIXTURE
//----------------------------------------------------------------------------------------------------------------------

// 📝 Same seed the Log Stream workspace captures use (Exhibits/LogStream): a vehicle with four tyres
//    (Rear Right hidden, so the eye-off row state renders), the proving-grounds environment with two
//    geometry records, a light and a camera. Records are real SceneRecords in the process SceneDirectory —
//    the Outliner, Inspector and spawn flow all operate on the genuine article, not on panel fakes.
class VehicleSceneFixture
{
  public:
    VehicleSceneFixture();
    ~VehicleSceneFixture();

    VehicleSceneFixture(const VehicleSceneFixture&)            = delete;
    VehicleSceneFixture& operator=(const VehicleSceneFixture&) = delete;

    // Service an Outliner spawn request: creates the record under Parent (root when invalid) and
    // returns its token so the host can select it.
    RecordToken Spawn(RecordClassification Classification, RecordToken Parent);

    [[nodiscard]] size_t QueryRecordCount() const noexcept { return Records.size(); }

  private:
    RecordToken AddRecord(RecordClassification Classification, const char* Name, RecordToken Parent, bool Visible);

    std::vector<std::unique_ptr<SceneRecord>> Records;
    uint32_t                                  SpawnedCount = 0;   // [records] - Session spawn counter for names
};

} // namespace Frontier
