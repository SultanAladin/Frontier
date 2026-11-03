/*==========================================================================================================================================
                                                          RENDERSETTINGSROWS.CPP
==========================================================================================================================================*/
// 🧩 The rows the Render settings sections share: a section card with its glyph, title and status, sub-headings, and switches and
//    sliders that fade with the reason a setting does not apply.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <cstdint>

namespace Frontier
{

namespace RenderSettingsRows
{

void BeginSection(ControlColumn& Page, const char* Title, const char* Glyph, const char* Status, const char* Caption)
{
    CardLook Look;
    Look.Radius        = 22.0f;
    Look.PaddingTop    = 16.0f;
    Look.PaddingSide   = 14.0f;
    Look.PaddingBottom = 6.0f;
    Look.MarginBottom  = 16.0f;
    Look.HeadingGlyph  = Glyph;
    Page.BeginCard(Look, Title, Status);
    if (Caption != nullptr && Caption[0] != '\0') CaptionText::Construct(Page, Caption, 0.0f, 12.0f);
}

void EndSection(ControlColumn& Page)
{
    Page.EndCard();
}

void SubHeading(ControlColumn& Page, const char* Title)
{
    // 📝 9 px tracked capitals, the prototype's .sub-heading.
    const ImVec2 Top = Page.Space(13.0f, 8.0f, 9.0f);
    char Upper[64]{};
    for (size_t Glyph = 0; Glyph + 1 < sizeof(Upper) && Title[Glyph] != '\0'; ++Glyph)
        Upper[Glyph] = static_cast<char>(Title[Glyph] >= 'a' && Title[Glyph] <= 'z' ? Title[Glyph] - 32 : Title[Glyph]);
    (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 9.0f * Page.QueryScale(),
                              ImVec2(Top.x, Top.y + GlyphRun::EmAscent * 9.0f * Page.QueryScale()), Page.QueryTheme().Eyebrow, Upper,
                              1.2f * Page.QueryScale());
}

bool Switch(ControlColumn& Page, const char* Id, const char* Label, bool& Setting, const char* Unavailable, const char* Tooltip)
{
    return ToggleRow::Construct(Page, Id, Label, Setting, Unavailable == nullptr, Unavailable != nullptr ? Unavailable : Tooltip);
}

bool Count(ControlColumn& Page, const char* Id, const char* Label, int& Setting, int Minimum, int Maximum, const char* Unit, const char* Unavailable,
           const char* Tooltip)
{
    uint32_t Quantity = static_cast<uint32_t>(std::clamp(Setting, Minimum, Maximum));
    if (!SliderRow::ConstructInteger(Page, Id, Label, Quantity, static_cast<uint32_t>(Minimum), static_cast<uint32_t>(Maximum), Unit != nullptr ? Unit : "",
                                     Unavailable == nullptr, Unavailable != nullptr ? Unavailable : Tooltip))
        return false;
    Setting = static_cast<int>(Quantity);
    return true;
}

bool Amount(ControlColumn& Page, const char* Id, const char* Label, float& Setting, float Minimum, float Maximum, float Step, const char* Unit,
            const char* Unavailable, const char* Tooltip)
{
    return SliderRow::Construct(Page, Id, Label, Setting, Minimum, Maximum, Step, Unit != nullptr ? Unit : "", 1.0f, Unavailable == nullptr,
                                Unavailable != nullptr ? Unavailable : Tooltip);
}

} // namespace RenderSettingsRows

} // namespace Frontier

#endif
