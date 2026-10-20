//========================================================================================================================
//                                                SCENEOBJECTVALIDATION.CPP
//========================================================================================================================
// 🧩 Exercises the SceneObject model: template, flat links, clamped edits, removal rules, derived pole length, and
//    checked loading of corrupt lists. Prints expected and observed values; exits non-zero on any mismatch.
//========================================================================================================================

#include "SceneObjectPartStore.h"
#include "StreetlightSceneObject.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Frontier;

namespace {

int Passed = 0;
int Failed = 0;

void Expect(const char* Name, bool Condition, const std::string& Expected, const std::string& Observed)
{
    if (Condition)
        ++Passed;
    else
        ++Failed;
    std::printf("%s  %-44s expected %-24s observed %s\n", Condition ? "PASS" : "FAIL", Name, Expected.c_str(),
                Observed.c_str());
}

std::string ResultText(SceneObjectResult Result)
{
    return SceneObjectResultName(Result);
}

std::string NumberText(float Value)
{
    char Buffer[32];
    std::snprintf(Buffer, sizeof(Buffer), "%.4g", static_cast<double>(Value));
    return Buffer;
}

// Builds a valid entry list by hand so a test can break exactly one field.
std::vector<SceneObjectPartEntry> TemplateEntries()
{
    SceneObjectPartStore Store = MakeStreetlightSceneObject();
    return Store.Parts();
}

}   // namespace

int main()
{
    //--------------------------------------------------------------------------------------------------------------------
    //                                                   TEMPLATE
    //--------------------------------------------------------------------------------------------------------------------
    {
        SceneObjectPartStore Store = MakeStreetlightSceneObject();
        const std::vector<SceneObjectPartEntry>& Parts = Store.Parts();
        Expect("template part count", Store.Count() == 6u, "6", std::to_string(Store.Count()));
        Expect("template entry 0 is PlinthPart", !Parts.empty() && Parts[0].Class == SceneObjectPartClass::Plinth,
               "PlinthPart", Parts.empty() ? "none" : SceneObjectPartClassName(Parts[0].Class));
        const bool EmitterLinks = Parts.size() == 6u && Parts[5].HostIndex == 3 && Parts[5].DataIndex == 4;
        Expect("template emitter links host 3, data 4", EmitterLinks, "host 3, data 4",
               Parts.size() == 6u ? "host " + std::to_string(Parts[5].HostIndex) + ", data " +
                                        std::to_string(Parts[5].DataIndex)
                                  : "short list");
        Expect("template validates", Store.Validate(), "true", Store.Validate() ? "true" : "false");
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                DERIVED POLE LENGTH
    //--------------------------------------------------------------------------------------------------------------------
    {
        SceneObjectPartStore Store = MakeStreetlightSceneObject();
        float Pole = 0.0f;
        const bool Found = Store.PoleLength(Pole);
        Expect("pole length = lamp height - base height", Found && std::fabs(Pole - 7.5f) < 1e-6f, "7.5 m",
               Found ? NumberText(Pole) + " m" : "none");
        Store.SetNumber(1, "Lamp Height", 12.0f);
        (void)Store.PoleLength(Pole);
        Expect("pole length follows lamp height edit", std::fabs(Pole - 11.5f) < 1e-6f, "11.5 m", NumberText(Pole) + " m");
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 CLAMPED EDITS
    //--------------------------------------------------------------------------------------------------------------------
    {
        SceneObjectPartStore Store = MakeStreetlightSceneObject();
        Store.SetNumber(1, "Lamp Height", 25.0f);
        float Value = 0.0f;
        (void)Store.ReadNumber(1, "Lamp Height", Value);
        Expect("lamp height clamps to maximum", Value == 20.0f, "20", NumberText(Value));
        Store.SetNumber(1, "Lamp Height", -1.0f);
        (void)Store.ReadNumber(1, "Lamp Height", Value);
        Expect("lamp height clamps to minimum", Value == 3.0f, "3", NumberText(Value));
    }
    {
        SceneObjectPartStore Store = MakeStreetlightSceneObject();
        const SceneObjectResult Intensity = Store.SetNumber(5, "Intensity", 4200.0f);
        const SceneObjectResult Colour    = Store.SetColour(5, "Colour", SceneObjectColour{ 0.2f, 0.4f, 1.5f });
        const SceneObjectResult Radius    = Store.SetNumber(5, "Light Radius", 0.2f);
        float IntensityValue = 0.0f, RadiusValue = 0.0f;
        (void)Store.ReadNumber(5, "Intensity", IntensityValue);
        (void)Store.ReadNumber(5, "Light Radius", RadiusValue);
        const SceneObjectColour Stored = Store.Parts()[5].Values[1].Colour;
        Expect("intensity edit reads back", Intensity == SceneObjectResult::Accepted && IntensityValue == 4200.0f, "4200",
               NumberText(IntensityValue));
        Expect("light radius edit reads back", Radius == SceneObjectResult::Accepted && RadiusValue == 0.2f, "0.2",
               NumberText(RadiusValue));
        Expect("colour blue clamps to 1", Colour == SceneObjectResult::Accepted && Stored.Blue == 1.0f, "1",
               NumberText(Stored.Blue));
    }
    {
        SceneObjectPartStore Store = MakeStreetlightSceneObject();
        Store.SetChoice(4, "IES Profile", 9u);
        Expect("IES choice clamps to last option", Store.Parts()[4].Values[0].Choice == 3u, "3",
               std::to_string(Store.Parts()[4].Values[0].Choice));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                REFUSED EDITS
    //--------------------------------------------------------------------------------------------------------------------
    {
        SceneObjectPartStore Store = MakeStreetlightSceneObject();
        const SceneObjectResult Misspelt = Store.SetNumber(1, "Lamp Hieght", 9.0f);
        const SceneObjectResult WrongKind = Store.SetNumber(5, "Colour", 1.0f);
        const SceneObjectResult BadIndex  = Store.SetNumber(42, "Lamp Height", 9.0f);
        float Value = 0.0f;
        (void)Store.ReadNumber(1, "Lamp Height", Value);
        Expect("misspelt property refused", Misspelt == SceneObjectResult::UnknownProperty, "UnknownProperty",
               ResultText(Misspelt));
        Expect("colour edited as a number refused", WrongKind == SceneObjectResult::WrongPropertyKind,
               "WrongPropertyKind", ResultText(WrongKind));
        Expect("bad index refused", BadIndex == SceneObjectResult::IndexOutOfRange, "IndexOutOfRange", ResultText(BadIndex));
        Expect("refused edits leave value alone", Value == 8.0f, "8", NumberText(Value));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 LINK RULES
    //--------------------------------------------------------------------------------------------------------------------
    {
        SceneObjectPartStore Store;
        (void)Store.AddPart(SceneObjectPartClass::Plinth);
        const SceneObjectResult ArmOnPlinth  = Store.AddPart(SceneObjectPartClass::Arm, 0);
        const SceneObjectResult ShaftNoHost  = Store.AddPart(SceneObjectPartClass::Shaft);
        const SceneObjectResult ShaftBadHost = Store.AddPart(SceneObjectPartClass::Shaft, 7);
        const SceneObjectResult PlinthOnHost = Store.AddPart(SceneObjectPartClass::Plinth, 0);
        const SceneObjectResult LightNoHost  = Store.AddPart(SceneObjectPartClass::Emitter, 0, -1);
        Expect("arm on a plinth refused", ArmOnPlinth == SceneObjectResult::HostClassMismatch, "HostClassMismatch",
               ResultText(ArmOnPlinth));
        Expect("shaft without host refused", ShaftNoHost == SceneObjectResult::MissingHost, "MissingHost",
               ResultText(ShaftNoHost));
        Expect("shaft on a missing index refused", ShaftBadHost == SceneObjectResult::MissingHost, "MissingHost",
               ResultText(ShaftBadHost));
        Expect("light on a plinth refused", LightNoHost == SceneObjectResult::HostClassMismatch, "HostClassMismatch",
               ResultText(LightNoHost));
        SceneObjectPartStore Chain;
        (void)Chain.AddPart(SceneObjectPartClass::Plinth);
        (void)Chain.AddPart(SceneObjectPartClass::Shaft, 0);
        (void)Chain.AddPart(SceneObjectPartClass::Arm, 1);
        (void)Chain.AddPart(SceneObjectPartClass::Housing, 2);
        const SceneObjectResult LightNoData = Chain.AddPart(SceneObjectPartClass::Emitter, 3, -1);
        Expect("light without a profile refused", LightNoData == SceneObjectResult::MissingData, "MissingData",
               ResultText(LightNoData));
        Expect("plinth on the ground refused a host", PlinthOnHost == SceneObjectResult::UnexpectedHost,
               "UnexpectedHost", ResultText(PlinthOnHost));
        Expect("refused adds leave the store alone", Store.Count() == 1u, "1", std::to_string(Store.Count()));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    REMOVAL
    //--------------------------------------------------------------------------------------------------------------------
    {
        SceneObjectPartStore Store = MakeStreetlightSceneObject();
        const SceneObjectResult InUseHost = Store.RemovePart(2);
        const SceneObjectResult InUseData = Store.RemovePart(4);
        Expect("removing a host in use refused", InUseHost == SceneObjectResult::InUse, "InUse", ResultText(InUseHost));
        Expect("removing a profile in use refused", InUseData == SceneObjectResult::InUse, "InUse", ResultText(InUseData));
        const SceneObjectResult Unused = Store.RemovePart(5);
        Expect("removing the unused light accepted", Unused == SceneObjectResult::Accepted, "Accepted", ResultText(Unused));
        Expect("store has five parts after removal", Store.Count() == 5u, "5", std::to_string(Store.Count()));
    }
    {
        SceneObjectPartStore Store;
        (void)Store.AddPart(SceneObjectPartClass::Plinth);      // 0 — unused
        (void)Store.AddPart(SceneObjectPartClass::Plinth);      // 1 — hosts the shaft
        (void)Store.AddPart(SceneObjectPartClass::Shaft, 1);    // 2
        const SceneObjectResult Removed = Store.RemovePart(0);
        Expect("unused part before a host removed", Removed == SceneObjectResult::Accepted, "Accepted", ResultText(Removed));
        const int32_t NewHost = Store.Count() == 2u ? Store.Parts()[1].HostIndex : -99;
        Expect("host link shifts down after removal", NewHost == 0, "host 0", std::to_string(NewHost));
        Expect("shifted store still validates", Store.Validate(), "true", Store.Validate() ? "true" : "false");
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                CHECKED LOADING
    //--------------------------------------------------------------------------------------------------------------------
    {
        SceneObjectPartStore Store;
        std::vector<std::string> Problems;
        const bool Accepted = Store.TryAdoptParts(TemplateEntries(), &Problems);
        Expect("valid list is adopted", Accepted && Store.Count() == 6u, "true, 6 parts",
               (Accepted ? "true" : "false") + std::string(", ") + std::to_string(Store.Count()) + " parts");
    }
    {
        SceneObjectPartStore Store = MakeStreetlightSceneObject();
        std::vector<SceneObjectPartEntry> Corrupt = TemplateEntries();
        Corrupt[1].HostIndex = 5;                                  // shaft hosted on a later entry
        std::vector<std::string> Problems;
        const bool Accepted = Store.TryAdoptParts(Corrupt, &Problems);
        Expect("forward host refused", !Accepted, "false", Accepted ? "true" : "false");
        Expect("refused list leaves store unchanged", Store.Count() == 6u && Store.Parts()[1].HostIndex == 0, "6 parts, host 0",
               std::to_string(Store.Count()) + " parts, host " + std::to_string(Store.Parts()[1].HostIndex));
        Expect("refusal explains the fault", !Problems.empty(), "at least one line",
               std::to_string(Problems.size()) + " line(s)");
    }
    {
        SceneObjectPartStore Store;
        std::vector<SceneObjectPartEntry> Corrupt = TemplateEntries();
        Corrupt[0].Values[0].Number = 5.0f;                        // Base Width above its 1.2 m maximum
        const bool Accepted = Store.TryAdoptParts(Corrupt, nullptr);
        Expect("out-of-range value refused", !Accepted, "false", Accepted ? "true" : "false");
    }
    {
        SceneObjectPartStore Store;
        std::vector<SceneObjectPartEntry> Corrupt = TemplateEntries();
        Corrupt[5].Values.pop_back();                              // one value missing from the emitter
        const bool Accepted = Store.TryAdoptParts(Corrupt, nullptr);
        Expect("missing value refused", !Accepted, "false", Accepted ? "true" : "false");
    }

    std::printf("\nSceneObjectValidation: %d passed, %d failed\n", Passed, Failed);
    return Failed == 0 ? 0 : 1;
}
