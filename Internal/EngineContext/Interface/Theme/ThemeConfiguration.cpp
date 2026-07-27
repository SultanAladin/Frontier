/*==============================================================================================================================================
                                                            THEMECONFIGURATION.CPP
==============================================================================================================================================*/
// 🧩 The one place layout-metric literals are permitted. Editing a value here re-spaces every panel and control that reads the theme.
//    The pill-style metrics (row height, UI scale, rounding, segment widths) are additionally overridable at runtime from a plain
//    "InterfaceScale.config" file — edit a line + relaunch to resize every control, no rebuild. Absent file => the baked defaults below.

#include "ThemeConfiguration.h"

#include <cstdlib>
#include <fstream>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 One overridable "Key = Value" line from InterfaceScale.config, parsed into a float. Comments start with '#'.
    void ApplyConfigLine(const std::string& Line, ThemeMetrics& Metrics)
    {
        const std::size_t Comment = Line.find('#');
        const std::string Body    = (Comment == std::string::npos) ? Line : Line.substr(0, Comment);

        const std::size_t Equals = Body.find('=');
        if (Equals == std::string::npos)
        {
            return;
        }

        // 📝 Trim surrounding whitespace off the key and value halves.
        auto Trim = [](std::string Text) -> std::string
        {
            const std::size_t First = Text.find_first_not_of(" \t\r\n");
            if (First == std::string::npos) { return std::string(); }
            const std::size_t Last = Text.find_last_not_of(" \t\r\n");
            return Text.substr(First, Last - First + 1);
        };

        const std::string Key   = Trim(Body.substr(0, Equals));
        const std::string Value = Trim(Body.substr(Equals + 1));
        if (Key.empty() || Value.empty())
        {
            return;
        }

        const float Number = static_cast<float>(std::atof(Value.c_str()));

        if      (Key == "UiScale")         { Metrics.UiScale          = Number; }
        else if (Key == "RowHeight")       { Metrics.PillRowHeight    = Number; }
        else if (Key == "PillRounding")    { Metrics.PillRounding     = Number; }
        else if (Key == "SideSegmentWidth"){ Metrics.SideSegmentWidth = Number; }
        else if (Key == "NumericFontScale"){ Metrics.NumericFontScale = Number; }
        else if (Key == "SegmentFontScale"){ Metrics.SegmentFontScale = Number; }
        else if (Key == "LabelColumnRatio"){ Metrics.LabelColumnRatio = Number; }
        else if (Key == "ControlSpacing")  { Metrics.ControlSpacing   = Number; }
    }


    // 📝 Overlay InterfaceScale.config onto the baked defaults if the file is found in one of the standard relative locations.
    //    Kept dependency-free (plain ifstream) so scaling never waits on the app config spine.
    void OverlayScaleConfig(ThemeMetrics& Metrics)
    {
        static const char* const CandidatePaths[] =
        {
            "InterfaceScale.config",
            "Config/InterfaceScale.config",
            "EngineContent/Config/InterfaceScale.config",
            "../EngineContent/Config/InterfaceScale.config",
        };

        for (const char* Path : CandidatePaths)
        {
            std::ifstream File(Path);
            if (!File.is_open())
            {
                continue;
            }
            std::string Line;
            while (std::getline(File, Line))
            {
                ApplyConfigLine(Line, Metrics);
            }
            return;   // 📝 First file found wins; later candidates are only fallbacks.
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolve the built-in metrics. A denser / touch profile is a sibling Resolve* returning the SAME struct.
ThemeMetrics ResolveDefaultMetrics()
{
    ThemeMetrics Metrics = {};

    Metrics.PanelPadding     =  8.0f;
    Metrics.ControlSpacing   =  6.0f;
    Metrics.ControlHeight    = 22.0f;
    Metrics.RowHeight        = 20.0f;
    Metrics.IndentWidth      = 14.0f;
    Metrics.CornerRounding   =  4.0f;
    Metrics.BorderThickness  =  1.0f;
    Metrics.LabelColumnRatio =  0.40f;

    // 📝 Pill-row defaults — a middle ground between the HTML's chunky 50px rows and the compact panels. Bump UiScale/RowHeight
    //    in InterfaceScale.config to move toward the ControlsPreview.html proportions without touching code.
    Metrics.UiScale          =  1.0f;
    Metrics.PillRowHeight    = 30.0f;
    Metrics.PillRounding     = 999.0f;   // fully rounded
    Metrics.SideSegmentWidth = 30.0f;
    Metrics.NumericFontScale =  1.15f;
    Metrics.SegmentFontScale =  0.95f;

    OverlayScaleConfig(Metrics);

    // 📝 Fold UiScale into the pixel metrics so downstream controls read final sizes directly (one multiply, one place).
    const float Scale = Metrics.UiScale > 0.01f ? Metrics.UiScale : 1.0f;
    Metrics.PillRowHeight    *= Scale;
    Metrics.SideSegmentWidth *= Scale;

    return Metrics;
}

}   // namespace Frontier
