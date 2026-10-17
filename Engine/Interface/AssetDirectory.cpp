/*==========================================================================================================================================
                                                             ASSETDIRECTORY.CPP
==========================================================================================================================================*/
// 🧩 Locate packaged UI assets independently of the working directory.

#include "AssetDirectory.h"
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#endif

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   DIRECTORY RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path ResolveAssetDirectory(const std::filesystem::path &Requested)
{
    namespace fs = std::filesystem;
    std::error_code Failure;
    if (Requested.is_absolute() || fs::is_directory(Requested, Failure))
        return fs::absolute(Requested);
    const auto Generic = Requested.generic_string();
    if (Generic.rfind("UI/Assets/", 0) != 0)
        return fs::absolute(Requested);

    fs::path Executable;
#ifdef _WIN32
    wchar_t     ExecutablePath[32768];
    const DWORD Length = GetModuleFileNameW(nullptr, ExecutablePath, 32768);
    if (Length > 0 && Length < 32768)
        Executable = std::wstring(ExecutablePath, Length);
#elif defined(__linux__)
    Executable = fs::read_symlink("/proc/self/exe", Failure);
#elif defined(__APPLE__)
    uint32_t Capacity = 0;
    _NSGetExecutablePath(nullptr, &Capacity);
    std::vector<char> ExecutablePath(Capacity);
    if (_NSGetExecutablePath(ExecutablePath.data(), &Capacity) == 0)
        Executable = ExecutablePath.data();
#endif
    if (!Executable.empty())
    {
        for (auto Directory = Executable.parent_path(); !Directory.empty();)
        {
            const auto Candidate = Directory / Requested;
            if (fs::is_directory(Candidate, Failure))
                return Candidate;
            const auto Enclosure = Directory.parent_path();
            if (Enclosure == Directory)
                break;
            Directory = Enclosure;
        }
    }
    return fs::absolute(Requested);
}
} // namespace Frontier
