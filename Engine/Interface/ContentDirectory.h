/*==========================================================================================================================================
                                                              CONTENTDIRECTORY.H
==========================================================================================================================================*/
// 🧩 Locate packaged engine content independently of the working directory.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   DIRECTORY RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

inline std::string EncodeContentPath(const std::filesystem::path &Path)
{
    const auto UTF8 = Path.u8string();
    return std::string(reinterpret_cast<const char *>(UTF8.data()), UTF8.size());
}

inline std::filesystem::path DecodeContentPath(std::string_view Path)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(Path.data()), Path.size()));
}

std::filesystem::path ResolveContentDirectory(const std::filesystem::path &Requested);
} // namespace Frontier
