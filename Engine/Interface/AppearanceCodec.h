/*==========================================================================================================================================
                                                             APPEARANCECODEC.H
==========================================================================================================================================*/
// 🧩 Validate and atomically persist theme and typography preferences.

#pragma once

#include "AppearanceInspector.h"
#include <filesystem>
#include <string>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    APPEARANCE CODEC
//------------------------------------------------------------------------------------------------------------------------

class AppearanceCodec
{
  public:
    static bool Validate(const AppearanceSettings &Preferences) noexcept;
    static bool Decode(const std::filesystem::path &Path, AppearanceSettings &Preferences, std::string &FamilyName,
                       std::string &Diagnostic) noexcept;
    static bool Encode(const std::filesystem::path &Path, const AppearanceSettings &Preferences,
                       const std::string &FamilyName, std::string &Diagnostic) noexcept;
};
} // namespace Frontier
