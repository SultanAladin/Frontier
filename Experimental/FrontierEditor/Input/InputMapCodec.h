/*==========================================================================================================================================
                                                              INPUTMAPCODEC.H
==========================================================================================================================================*/
// 🧩 TOML persistence for input assignments, using the shared chord tokens (stable across platforms).

#pragma once

#include <filesystem>
#include <string>

namespace Frontier
{

class InputMap;

//----------------------------------------------------------------------------------------------------------------------
//                                                      INPUT MAP CODEC
//----------------------------------------------------------------------------------------------------------------------

// Schema (Format = 1):
//   [[ActionAssignment]] Action, Context, Key (InputChordTable token), Control/Alt/Shift/Super, PointerButton
//   [[AxisAssignment]]   Axis, Context, Source, NegativeKey, PositiveKey, Scale, Deadzone
// Key tokens are the InputChordTable names, never platform scan codes — the same file loads on every host.
class InputMapCodec
{
  public:
    [[nodiscard]] static bool Encode(const std::filesystem::path& Path, const InputMap& Map,
                                     std::string& Diagnostic) noexcept;
    // Decode appends to the map (register into a fresh InputMap for an exact file image). Conflicting or
    // unrecognised entries are refused individually; the first refusal stops the decode with its diagnostic.
    [[nodiscard]] static bool Decode(const std::filesystem::path& Path, InputMap& Map,
                                     std::string& Diagnostic) noexcept;
};

} // namespace Frontier
