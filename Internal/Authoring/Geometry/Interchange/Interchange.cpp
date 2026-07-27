/*============================================================================================================================================
                                                             INTERCHANGE.CPP
============================================================================================================================================*/
// 🧩 The public asset-import dispatch: inspect the file extension, route to the matching per-format decoder, and hand back the
//    translated ImportedModel objects. Format detection is a case-insensitive extension match (.gltf / .glb -> glTF, .obj ->
//    Wavefront, .fbx -> Filmbox); the decoders themselves own the real parsing + the vendored libraries. This TU stays vendor-free
//    so the public boundary never leaks a cgltf / fast_obj / ufbx type. Future asset classes (images / HDRI, CAD) route from here.

#include "Interchange.h"
#include "InterchangeDecoders.h"

#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // The lower-cased file extension including the leading dot, or an empty view when the path has none.
    bool MatchExtension(const char* Path, const char* Extension)
    {
        if (Path == nullptr || Extension == nullptr) return false;
        const size_t PathLength      = std::strlen(Path);
        const size_t ExtensionLength = std::strlen(Extension);
        if (ExtensionLength == 0 || PathLength < ExtensionLength) return false;

        const char* Tail = Path + (PathLength - ExtensionLength);
        for (size_t Index = 0; Index < ExtensionLength; ++Index)
        {
            char Left  = Tail[Index];
            char Right = Extension[Index];
            if (Left >= 'A' && Left <= 'Z')  Left  = static_cast<char>(Left + ('a' - 'A'));
            if (Right >= 'A' && Right <= 'Z') Right = static_cast<char>(Right + ('a' - 'A'));
            if (Left != Right) return false;
        }
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool EnumerateModelObjects(const char* Path, const InterchangeOptions& Options, std::vector<ImportedModel>& Result)
{
    if (Path == nullptr || Path[0] == '\0') return false;

    if (MatchExtension(Path, ".gltf") || MatchExtension(Path, ".glb"))
        return DecodeGltf(Path, Options, Result);
    if (MatchExtension(Path, ".obj"))
        return DecodeWavefront(Path, Options, Result);
    if (MatchExtension(Path, ".fbx"))
        return DecodeFilmbox(Path, Options, Result);

    return false;
}

bool TranslateModelFile(const char* Path, const InterchangeOptions& Options, ImportedModel& Result)
{
    std::vector<ImportedModel> Objects;
    if (!EnumerateModelObjects(Path, Options, Objects) || Objects.empty())
        return false;

    Result = std::move(Objects.front());
    return true;
}

} // namespace Frontier
