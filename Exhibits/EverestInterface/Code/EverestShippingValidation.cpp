/*==========================================================================================================================================
                                                   EVERESTSHIPPINGVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Shipping validation (FRONTIER_EDITOR == 0): proves SceneWorld, .levelspace containers, physics, thermal, and chemical simulation link
//    and run cleanly with SceneDirectory, InspectorRuntime, and Editor UI compiled out completely.

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordStore.h"
#include "EverestWorkspace.h"

#if FRONTIER_EDITOR
#error "EverestShippingValidation must compile with FRONTIER_EDITOR == 0."
#endif

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

void Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        throw std::runtime_error(Message);
    }
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    try
    {
        Require(ArgumentCount == 2, "Expected Projects/Everest path argument.");
        const std::filesystem::path EverestRoot = std::filesystem::absolute(Arguments[1]);

        Everest::EverestWorkspace Workspace(EverestRoot);
        Frontier::SceneWorld&     World = Workspace.QuerySceneWorld();

        Require(World.QueryRecordCount() == 22, "Expected 22 dynamic records from Default.levelspace in Shipping.");
        Require(Frontier::RecordStore::Instance().QueryCount() == 22, "RecordStore count mismatch in Shipping.");
        Require(Workspace.QueryActiveLevelPath().extension() == ".levelspace", "Active level must carry .levelspace extension.");

        // Verify composable physics / thermal / chemical simulation configurations & helpers.
        auto* ThermalBody = dynamic_cast<Frontier::ThermalBodyRecord*>(World.FindFirstByType("ThermalBody"));
        Require(ThermalBody != nullptr, "ThermalBodyRecord missing from Default.levelspace.");
        Require(ThermalBody->QueryPhysics().Mass.MassKilograms > 10.0f, "MassConfiguration mass missing.");
        Require(ThermalBody->QueryThermal().ComputeRadiativeFluxWPerM2() > 100.0f, "Stefan-Boltzmann radiative flux invalid.");
        Require(ThermalBody->QueryThermal().ComputeSensibleHeatJoules(ThermalBody->QueryPhysics().Mass.MassKilograms) > 1000.0f,
                "Sensible heat helper invalid.");

        auto* Vessel = dynamic_cast<Frontier::ChemicalVesselRecord*>(World.FindFirstByType("ChemicalVessel"));
        Require(Vessel != nullptr, "ChemicalVesselRecord missing from Default.levelspace.");
        const float InitialMoles  = Vessel->QueryChemical().AmountMoles;
        const float InitialKelvin = Vessel->QueryThermal().TemperatureKelvin;
        Require(Vessel->QueryChemical().ComputeArrheniusRateConstant(InitialKelvin) > 0.0f, "Arrhenius rate helper invalid.");

        auto* Oscillator = dynamic_cast<Everest::HarmonicOscillatorRecord*>(World.FindFirstByType("HarmonicOscillator"));
        Require(Oscillator != nullptr, "HarmonicOscillatorRecord missing from Default.levelspace.");
        const float InitialDisp = Oscillator->QueryOscillator().CurrentDisplacementM;

        // Step simulation frames via EverestWorkspace -> EverestApplication -> SceneWorld.
        for (int Step = 0; Step < 30; ++Step)
        {
            Workspace.IntegrateSimulation(1.0 / 60.0);
        }
        Require(Workspace.QueryApplication().QuerySimulatedFrames() == 30, "EverestApplication frame counter mismatch.");
        Require(Vessel->QueryChemical().AmountMoles < InitialMoles, "Arrhenius chemical step did not consume reagent.");
        Require(Vessel->QueryThermal().TemperatureKelvin > InitialKelvin, "Exothermic enthalpy did not heat vessel.");
        Require(std::fabs(Oscillator->QueryOscillator().CurrentDisplacementM - InitialDisp) > 1.0e-4f,
                "HarmonicOscillatorRecord did not advance displacement.");

        // Dynamic creation, deletion, and .levelspace round-trip in Shipping.
        const Frontier::RecordToken AddedNode = World.ConstructRecord("SpatialNode", "Temporary Frame");
        Require(AddedNode.IsValid() && World.QueryRecordCount() == 23, "Dynamic SpatialNode construction failed.");
        Require(World.DestroyRecord(AddedNode) && World.QueryRecordCount() == 22, "Dynamic SpatialNode destruction failed.");

        std::string ErrorDiag;
        const std::filesystem::path BadExtPath = std::filesystem::temp_directory_path() / "InvalidFormat.wrong";
        Require(!World.SaveLevelSpace(BadExtPath, &ErrorDiag), "SaveLevelSpace must reject non-.levelspace extension.");

        const std::filesystem::path RoundTripPath = std::filesystem::temp_directory_path() / "EverestShippingRoundTrip.levelspace";
        Require(World.SaveLevelSpace(RoundTripPath, &ErrorDiag), "SaveLevelSpace failed in Shipping.");
        Frontier::SceneWorld Reloaded;
        Everest::RegisterEverestSimulationTypes(Reloaded);
        Require(Reloaded.LoadLevelSpace(RoundTripPath, &ErrorDiag), "LoadLevelSpace failed in Shipping.");
        Require(Reloaded.QueryRecordCount() == 22, "Reloaded .levelspace record count mismatch.");
        std::filesystem::remove(RoundTripPath);

        std::cout << "[EverestShippingValidation] Passed (FRONTIER_EDITOR=0, 22 .levelspace records, physics+thermal+chemical verified).\n";
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::cerr << "[EverestShippingValidation] FAILED: " << Error.what() << "\n";
        return 1;
    }
}
