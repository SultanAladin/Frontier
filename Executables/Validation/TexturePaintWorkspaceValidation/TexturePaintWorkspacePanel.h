/*==============================================================================================================================================
                                                         TEXTUREPAINTWORKSPACEPANEL.H
==============================================================================================================================================*/
// Minimal texture-paint workspace shell. Tab summons the blank two-slide shared panel; right-click retains the existing paint-tool summon.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_TEXTUREPAINTWORKSPACEVALIDATION_TEXTUREPAINTWORKSPACEPANEL_H
#define FRONTIER_EXECUTABLES_VALIDATION_TEXTUREPAINTWORKSPACEVALIDATION_TEXTUREPAINTWORKSPACEPANEL_H

#include "TexturePaintSummonedCard.h"
#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

namespace Frontier { struct SvgIconRegistry; struct PaintIconStore; }

namespace TexturePaintWorkspaceValidation
{

struct TexturePaintWorkspaceState
{
    TexturePaintValidation::TexturePaintSummonedState PaintTools;

    bool  PanelOpen       = false;
    bool  PanelRequested  = false;
    bool  SlideForward    = false;
    float PanelX          = 0.0f;
    float PanelY          = 0.0f;
    float RequestX        = 0.0f;
    float RequestY        = 0.0f;
    float SlideTravel     = 0.0f;
    float OpenAge         = 0.0f;
};

void InitializeTexturePaintWorkspaceSample(TexturePaintWorkspaceState& State);

void ConstructTexturePaintWorkspacePanel(const Frontier::ThemeConfiguration& Theme,
                                         TexturePaintWorkspaceState&        State,
                                         Frontier::SvgIconRegistry*         Icons,
                                         Frontier::PaintIconStore*          StripStore);

} // namespace TexturePaintWorkspaceValidation

#endif
