//========================================================================================================================
//                                                  SCENEOBJECTTYPES.CPP
//========================================================================================================================
// 🧩 Descriptor tables, host rules and name lookups. The tables are the single source for the store, the validator,
//    the inspector and the saved form. Defaults match StreetlightSpecification.js in the web prototype.
//========================================================================================================================

#include "SceneObjectTypes.h"

#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                     CHOICE LABELS
//------------------------------------------------------------------------------------------------------------------------

const char* const IesProfileOptions[] = {
    "Type II · roadway",
    "Type III · wide roadway",
    "Type IV · perimeter",
    "Type V · circular",
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     DEFAULT VALUES
//------------------------------------------------------------------------------------------------------------------------

SceneObjectPropertyValue Number(float Value) noexcept
{
    SceneObjectPropertyValue Result{};
    Result.Number = Value;
    return Result;
}

SceneObjectPropertyValue Choice(uint32_t Value) noexcept
{
    SceneObjectPropertyValue Result{};
    Result.Choice = Value;
    return Result;
}

// #ffd9a0 — the warm white of the prototype's default light
SceneObjectPropertyValue WarmWhite() noexcept
{
    SceneObjectPropertyValue Result{};
    Result.Colour = SceneObjectColour{ 1.0f, 217.0f / 255.0f, 160.0f / 255.0f };
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DESCRIPTOR TABLES
//------------------------------------------------------------------------------------------------------------------------

const SceneObjectPropertyDescriptor PlinthProperties[] = {
    { "Base Width",  "m", "Base",        SceneObjectPropertyKind::Length, 0.3f,  1.2f,  Number(0.45f), nullptr, 0u },
    { "Base Height", "m", "Base",        SceneObjectPropertyKind::Length, 0.1f,  1.0f,  Number(0.5f),  nullptr, 0u },
};

const SceneObjectPropertyDescriptor ShaftProperties[] = {
    { "Lamp Height",    "m", "Pole",        SceneObjectPropertyKind::Length, 3.0f,  20.0f, Number(8.0f),  nullptr, 0u },
    { "Pole Diameter",  "m", "Pole",        SceneObjectPropertyKind::Length, 0.05f, 0.4f,  Number(0.14f), nullptr, 0u },
};

const SceneObjectPropertyDescriptor ArmProperties[] = {
    { "Arm Length", "m", "Attachments", SceneObjectPropertyKind::Length, 0.0f, 3.0f,  Number(1.2f), nullptr, 0u },
    { "Arm Tilt",   "°", "Attachments", SceneObjectPropertyKind::Angle,  0.0f, 30.0f, Number(8.0f), nullptr, 0u },
};

const SceneObjectPropertyDescriptor HousingProperties[] = {
    { "Case Length", "m", "Headlamp", SceneObjectPropertyKind::Length, 0.2f,  1.2f, Number(0.6f),  nullptr, 0u },
    { "Case Width",  "m", "Headlamp", SceneObjectPropertyKind::Length, 0.1f,  0.6f, Number(0.3f),  nullptr, 0u },
    { "Case Height", "m", "Headlamp", SceneObjectPropertyKind::Length, 0.05f, 0.4f, Number(0.15f), nullptr, 0u },
};

const SceneObjectPropertyDescriptor EmitterProperties[] = {
    { "Intensity",    "cd", "Light", SceneObjectPropertyKind::Intensity, 0.0f,  50000.0f, Number(12000.0f), nullptr, 0u },
    { "Colour",       "",   "Light", SceneObjectPropertyKind::Colour,    0.0f,  0.0f,     WarmWhite(),       nullptr, 0u },
    { "Light Radius", "m",  "Light", SceneObjectPropertyKind::Radius,    0.01f, 0.5f,     Number(0.05f),     nullptr, 0u },
};

const SceneObjectPropertyDescriptor PhotometricProperties[] = {
    { "IES Profile", "", "IES Profile", SceneObjectPropertyKind::Choice, 0.0f, 0.0f, Choice(0u),
      IesProfileOptions, static_cast<uint32_t>(sizeof(IesProfileOptions) / sizeof(IesProfileOptions[0])) },
};

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       NAME LOOKUPS
//------------------------------------------------------------------------------------------------------------------------

const char* SceneObjectPartClassName(SceneObjectPartClass Class) noexcept
{
    switch (Class)
    {
        case SceneObjectPartClass::Plinth:             return "PlinthPart";
        case SceneObjectPartClass::Shaft:              return "ShaftPart";
        case SceneObjectPartClass::Arm:                return "ArmPart";
        case SceneObjectPartClass::Housing:            return "HousingPart";
        case SceneObjectPartClass::Emitter:            return "EmitterPart";
        case SceneObjectPartClass::PhotometricProfile: return "PhotometricProfile";
    }
    return "Unknown";
}

const char* SceneObjectPartLabel(SceneObjectPartClass Class) noexcept
{
    switch (Class)
    {
        case SceneObjectPartClass::Plinth:             return "Base";
        case SceneObjectPartClass::Shaft:              return "Pole";
        case SceneObjectPartClass::Arm:                return "Attachments";
        case SceneObjectPartClass::Housing:            return "Headlamp";
        case SceneObjectPartClass::Emitter:            return "Light";
        case SceneObjectPartClass::PhotometricProfile: return "IES Profile";
    }
    return "Unknown";
}

const char* SceneObjectSocketName(SceneObjectSocket Socket) noexcept
{
    switch (Socket)
    {
        case SceneObjectSocket::None:     return "none";
        case SceneObjectSocket::Top:      return "Top";
        case SceneObjectSocket::Head:     return "Head";
        case SceneObjectSocket::Tip:      return "Tip";
        case SceneObjectSocket::Aperture: return "Aperture";
    }
    return "none";
}

const char* SceneObjectResultName(SceneObjectResult Result) noexcept
{
    switch (Result)
    {
        case SceneObjectResult::Accepted:          return "Accepted";
        case SceneObjectResult::IndexOutOfRange:   return "IndexOutOfRange";
        case SceneObjectResult::UnknownProperty:   return "UnknownProperty";
        case SceneObjectResult::WrongPropertyKind: return "WrongPropertyKind";
        case SceneObjectResult::MissingHost:       return "MissingHost";
        case SceneObjectResult::HostClassMismatch: return "HostClassMismatch";
        case SceneObjectResult::UnexpectedHost:    return "UnexpectedHost";
        case SceneObjectResult::MissingData:       return "MissingData";
        case SceneObjectResult::DataClassMismatch: return "DataClassMismatch";
        case SceneObjectResult::UnexpectedData:    return "UnexpectedData";
        case SceneObjectResult::InUse:             return "InUse";
    }
    return "Unknown";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       HOST RULES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The flat chain. Plinth sits on the ground; each later part sits on the one before it through a named socket.
SceneObjectHostRule SceneObjectHostRuleOf(SceneObjectPartClass Class) noexcept
{
    switch (Class)
    {
        case SceneObjectPartClass::Plinth:
            return { SceneObjectPartClass::Plinth, SceneObjectSocket::None, false, false, SceneObjectPartClass::Plinth };
        case SceneObjectPartClass::Shaft:
            return { SceneObjectPartClass::Plinth, SceneObjectSocket::Top, true, false, SceneObjectPartClass::Plinth };
        case SceneObjectPartClass::Arm:
            return { SceneObjectPartClass::Shaft, SceneObjectSocket::Head, true, false, SceneObjectPartClass::Plinth };
        case SceneObjectPartClass::Housing:
            return { SceneObjectPartClass::Arm, SceneObjectSocket::Tip, true, false, SceneObjectPartClass::Plinth };
        case SceneObjectPartClass::Emitter:
            return { SceneObjectPartClass::Housing, SceneObjectSocket::Aperture, true, true,
                     SceneObjectPartClass::PhotometricProfile };
        case SceneObjectPartClass::PhotometricProfile:
            return { SceneObjectPartClass::Plinth, SceneObjectSocket::None, false, false, SceneObjectPartClass::Plinth };
    }
    return { SceneObjectPartClass::Plinth, SceneObjectSocket::None, false, false, SceneObjectPartClass::Plinth };
}

SceneObjectPropertySpan SceneObjectPropertiesOf(SceneObjectPartClass Class) noexcept
{
    auto Span = [](const SceneObjectPropertyDescriptor* First, size_t Count) {
        return SceneObjectPropertySpan{ First, static_cast<uint32_t>(Count) };
    };
    switch (Class)
    {
        case SceneObjectPartClass::Plinth:             return Span(PlinthProperties, 2);
        case SceneObjectPartClass::Shaft:              return Span(ShaftProperties, 2);
        case SceneObjectPartClass::Arm:                return Span(ArmProperties, 2);
        case SceneObjectPartClass::Housing:            return Span(HousingProperties, 3);
        case SceneObjectPartClass::Emitter:            return Span(EmitterProperties, 3);
        case SceneObjectPartClass::PhotometricProfile: return Span(PhotometricProperties, 1);
    }
    return Span(nullptr, 0);
}

const SceneObjectPropertyDescriptor* FindSceneObjectProperty(SceneObjectPartClass Class, std::string_view Name) noexcept
{
    const SceneObjectPropertySpan Span = SceneObjectPropertiesOf(Class);
    for (uint32_t I = 0u; I < Span.Count; ++I)
    {
        if (Name == Span.First[I].Name)
            return &Span.First[I];
    }
    return nullptr;
}

bool IsNumericKind(SceneObjectPropertyKind Kind) noexcept
{
    return Kind == SceneObjectPropertyKind::Length || Kind == SceneObjectPropertyKind::Angle ||
           Kind == SceneObjectPropertyKind::Intensity || Kind == SceneObjectPropertyKind::Radius;
}

}   // namespace Frontier
