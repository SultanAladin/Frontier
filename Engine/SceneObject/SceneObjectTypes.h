//========================================================================================================================
//                                                  SCENEOBJECTTYPES.H
//========================================================================================================================
// 🧩 Vocabulary of a SceneObject: part classes, sockets, property descriptors, host rules and the flat part entry.
//    A SceneObject is one ordered list of entries. No entry owns another; every link is an index into that list.
//========================================================================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                       ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SceneObjectPartClass : uint8_t
{
    Plinth,
    Shaft,
    Arm,
    Housing,
    Emitter,
    PhotometricProfile,
};

// A socket is a named attachment point on a host part. The socket name is fixed by the host rule.
enum class SceneObjectSocket : uint8_t
{
    None,
    Top,
    Head,
    Tip,
    Aperture,
};

enum class SceneObjectPropertyKind : uint8_t
{
    Length,
    Angle,
    Intensity,
    Radius,
    Colour,
    Choice,
};

// Outcome of every edit and every structural change. Anything other than Accepted leaves the store unchanged.
enum class SceneObjectResult : uint8_t
{
    Accepted,
    IndexOutOfRange,
    UnknownProperty,
    WrongPropertyKind,
    MissingHost,
    HostClassMismatch,
    UnexpectedHost,
    MissingData,
    DataClassMismatch,
    UnexpectedData,
    InUse,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SceneObjectColour
{
    float Red   = 0.0f;   // [-] 0..1
    float Green = 0.0f;   // [-] 0..1
    float Blue  = 0.0f;   // [-] 0..1
};

// One stored value. Only the member matching the descriptor's Kind is meaningful.
struct SceneObjectPropertyValue
{
    float              Number = 0.0f;   // [SI units as the descriptor states] Length, Angle, Intensity, Radius
    SceneObjectColour  Colour{};        // [-] Colour
    uint32_t           Choice = 0u;     // [-] index into the descriptor's options
};

struct SceneObjectPropertyDescriptor
{
    const char*                Name;        // [-] Label shown in the inspector and used by the editor
    const char*                Unit;        // [-] Unit text, empty when none
    const char*                Group;       // [-] Inspector group (the part's UI label)
    SceneObjectPropertyKind    Kind;        // [-] Controls the widget and the legal edits
    float                      Minimum;     // [-] Lower clamp for numeric kinds
    float                      Maximum;     // [-] Upper clamp for numeric kinds
    SceneObjectPropertyValue   Default;     // [-] Value a new part starts with
    const char* const*         Options;     // [-] Choice labels, null for other kinds
    uint32_t                   OptionCount; // [-] Number of choice labels
};

struct SceneObjectPropertySpan
{
    const SceneObjectPropertyDescriptor* First; // [-] First descriptor of the part class, null when none
    uint32_t                             Count; // [-] Number of descriptors
};

struct SceneObjectHostRule
{
    SceneObjectPartClass Host;          // [-] Class the part must sit on, ignored when RequiresHost is false
    SceneObjectSocket    Socket;        // [-] Socket on that host the part attaches to
    bool                 RequiresHost;  // [-] false for root parts that sit on the ground
    bool                 RequiresData;  // [-] true when the part links to a data entry
    SceneObjectPartClass Data;          // [-] Class the data entry must be, ignored when RequiresData is false
};

struct SceneObjectPartEntry
{
    SceneObjectPartClass              Class     = SceneObjectPartClass::Plinth;
    int32_t                           HostIndex = -1;                       // [-] Index of the host entry, -1 for none
    SceneObjectSocket                 HostSocket = SceneObjectSocket::None; // [-] Socket used on the host
    int32_t                           DataIndex = -1;                       // [-] Index of a data entry, -1 for none
    std::vector<SceneObjectPropertyValue> Values;                           // [-] One per descriptor, in table order
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Stable identifiers, as they appear in code and in saved files.
[[nodiscard]] const char* SceneObjectPartClassName(SceneObjectPartClass Class) noexcept;

// 📝 The label the user reads in the inspector. Differs from the identifier where the identifier would be banned.
[[nodiscard]] const char* SceneObjectPartLabel(SceneObjectPartClass Class) noexcept;

[[nodiscard]] const char* SceneObjectSocketName(SceneObjectSocket Socket) noexcept;
[[nodiscard]] const char* SceneObjectResultName(SceneObjectResult Result) noexcept;

[[nodiscard]] SceneObjectHostRule     SceneObjectHostRuleOf(SceneObjectPartClass Class) noexcept;
[[nodiscard]] SceneObjectPropertySpan SceneObjectPropertiesOf(SceneObjectPartClass Class) noexcept;

// Null when the class has no property of that name.
[[nodiscard]] const SceneObjectPropertyDescriptor* FindSceneObjectProperty(SceneObjectPartClass Class,
                                                                           std::string_view Name) noexcept;

[[nodiscard]] bool IsNumericKind(SceneObjectPropertyKind Kind) noexcept;

}   // namespace Frontier
