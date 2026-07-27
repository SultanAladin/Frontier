/*==============================================================================================================================================
                                                              THEMERESOLVER.H
==============================================================================================================================================*/
// 🧩 Assembles the active ThemeConfiguration (palette + metrics) and mirrors it into ImGui's global style so raw ImGui widgets nested inside
//    a panel inherit the same look. Every application resolves the theme once per cycle and threads the result through its panels.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_THEME_THEMERESOLVER_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_THEME_THEMERESOLVER_H

#include "ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Combine the dark palette + default metrics into the active theme. Swapping either half is a one-line edit here.
ThemeConfiguration ResolveActiveTheme();

// 📝 Push the theme into ImGui's global style so nested raw ImGui widgets inherit the palette + rounding.
void EnforceThemeStyle(const ThemeConfiguration& Theme);

}   // namespace Frontier

#endif
