/*==============================================================================================================================================
                                                            LAYERSTACKPANEL.H
==============================================================================================================================================*/
// 🧩 3D Texture paint layer stack panel: 1:1 port of LayerStackR2.html mockup using shared theme tokens + interface controls.

#pragma once
#ifndef FRONTIER_INTERFACE_LAYERSTACKPANEL_H
#define FRONTIER_INTERFACE_LAYERSTACKPANEL_H

#include "../Theme/ThemeConfiguration.h"

#include <cstdint>
#include <vector>
#include <string>

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class LayerTagCategory : uint8_t
{
    Material = 0, // Violet tag (#8b5cf6)
    Generator = 1, // Green tag (#10b981)
    Paint = 2,     // Orange tag (#f97316)
    Fill = 3       // Blue tag (#3b82f6)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct LayerMaskComponentRecord
{
    std::string ComponentName;       // [-] - Name of the mask component (e.g. "AO Cavity")
    std::string ComponentType;       // [-] - Generator / Filter designation (e.g. "Generator")
};

struct LayerMaskRecord
{
    bool                                  MaskEnabled;          // [-] - True if layer has an active mask attached
    std::string                           MaskName;             // [-] - Mask title (e.g. "Layer Mask")
    int                                   MaskFillModeIndex;    // [-] - 0: Black, 1: White, 2: Invert
    float                                 MaskOpacityFraction;  // [0-1] - Mask opacity fraction (0.0 .. 1.0)
    std::vector<LayerMaskComponentRecord> MaskComponents;       // [-] - Attached mask generator components
};

struct LayerRecord
{
    std::string     LayerTitle;           // [-] - Layer display title (e.g. "Detail", "Base")
    LayerTagCategory CategoryTag;         // [-] - Tag color designation
    std::string     LayerTypeTitle;       // [-] - Layer type name (e.g. "Paint", "Material")
    int             BlendModeIndex;       // [-] - Index into blend modes array (Normal, Overlay, Multiply, Screen, Add, Subtract)
    int             ChannelCount;         // [-] - Channel count (e.g. 4 ch)
    bool            VisibilityEnabled;    // [-] - Layer visibility toggle
    float           OpacityFraction;      // [0-1] - Layer opacity fraction (0.0 .. 1.0)
    int             ResolutionIndex;      // [-] - Resolution index (0: 512, 1: 1K, 2: 2K, 3: 4K, 4: 8K)
    bool            CardExpanded;         // [-] - True if layer detail card is open/fused
    bool            PropertiesExpanded;   // [-] - True if PROPERTIES section is open
    bool            ColourExpanded;       // [-] - True if COLOUR section is open
    bool            MaskExpanded;         // [-] - True if MASK section is open
    LayerMaskRecord LayerMask;            // [-] - Layer mask state
};

struct LayerStackModel
{
    std::vector<LayerRecord> LayerRecords;       // [-] - Active layers in the stack (top to bottom)
    int                      ActiveLayerIndex;   // [-] - Currently selected active layer index
    char                     SearchFilter[128];  // [-] - Filter text query for layer search
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Initialize a default LayerStackModel with baseline prototype layers matching LayerStackR2.html.
void InitializeLayerStackModel(LayerStackModel& Model);

// 📝 Record and render the 1:1 LayerStack UI panel using the resolved ThemeConfiguration and caller-owned LayerStackModel.
void ConstructLayerStackPanel(const ThemeConfiguration& Theme, LayerStackModel& Model);

#endif
