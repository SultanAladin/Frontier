/*==============================================================================================================================================
                                                        INSPECTORGLYPHS.CPP
==============================================================================================================================================*/
// 🧩 PLACEHOLDER stage: one reference mark bound to every classification + chrome key. All keys carry identical bytes, so the registry's
//    content-hash dedup uploads ONE texture the whole pack shares. The panel resolves these keys through ClassificationGlyphKey /
//    ChromeGlyphKey. ⚠️ Swap the reference mark for the recoloured GLYPH builders (Layers / Folder / Cube / …) + the mono UI set for real art.

#include "InspectorGlyphs.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <cstdint>
#include <cstring>

namespace SceneDirectoryInspectorValidation
{

namespace
{
    // The supersampled master edge every inspector glyph rasterizes at.
    constexpr std::uint32_t InspectorGlyphMasterEdge = 64u;

    // 📝 The one reference mark, until the real classification / chrome art is generated. A rounded square with a centred dot in --ink.
    const char* const ReferenceGlyphDocument =
        "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
        "<rect x=\"4\" y=\"4\" width=\"16\" height=\"16\" rx=\"4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\"/>"
        "<circle cx=\"12\" cy=\"12\" r=\"2\" fill=\"#ececf0\"/>"
        "</svg>";

    // Every classification key the tree / cards / timeline reference (ClassificationKey lowercase strings).
    const char* const ClassificationKeys[] =
    {
        "scene", "folder", "sketch", "solid", "cylinder", "sphere", "cone", "revolve", "loft"
    };

    // Every chrome / UI glyph the panel draws (the prototype's UI table keys).
    const char* const ChromeKeys[] =
    {
        "search", "collapse", "plus", "chevron", "stepNext", "stepPrev", "undo", "redo",
        "sliders", "clock", "mark", "eyeOpen", "eyeOff", "rename", "duplicate", "group", "trash", "target"
    };
}

std::string ClassificationGlyphKey(RecordClassification Classification)
{
    std::string Key = "sdi-class-";
    Key += ClassificationKey(Classification);
    return Key;
}

std::string ChromeGlyphKey(const char* Name)
{
    std::string Key = "sdi-ui-";
    Key += (Name ? Name : "");
    return Key;
}

bool RegisterInspectorGlyphPack(Frontier::SvgIconRegistry& Registry)
{
    bool EveryGlyphRegistered = true;
    const std::uint32_t ByteCount = static_cast<std::uint32_t>(std::strlen(ReferenceGlyphDocument));

    for (const char* ClassKey : ClassificationKeys)
    {
        std::string Key = "sdi-class-";
        Key += ClassKey;
        if (!Frontier::RegisterSvgIcon(Registry, Key, ReferenceGlyphDocument, ByteCount, InspectorGlyphMasterEdge))
            EveryGlyphRegistered = false;
    }

    for (const char* UiKey : ChromeKeys)
    {
        std::string Key = "sdi-ui-";
        Key += UiKey;
        if (!Frontier::RegisterSvgIcon(Registry, Key, ReferenceGlyphDocument, ByteCount, InspectorGlyphMasterEdge))
            EveryGlyphRegistered = false;
    }

    return EveryGlyphRegistered;
}

} // namespace SceneDirectoryInspectorValidation
