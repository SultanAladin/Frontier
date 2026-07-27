/*==============================================================================================================================================
                                                              PROPERTYPANELBASE.H
==============================================================================================================================================*/
// 🧩 The shared scaffolding every workspace's property panel is built from: a scrolling column that hosts collapsible cards, each card a
//    SectionHeader over a ContentSection full of Controls. A per-workspace property panel (ModelingPropertyPanel, UVPropertyPanel, ...) is a
//    thin composition over these calls — it never re-implements card chrome or scrolling (the "shared, not duplicated" rule). This is the
//    "properties panel" half of each workspace's right column.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_PROPERTYPANELBASE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_PROPERTYPANELBASE_H

#include "../Theme/ThemeConfiguration.h"

#include "Cards/SectionHeader.h"

#include "Cards/ContentSection.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Open the scrolling properties column. Pair with EndPropertyPanel. A workspace records its cards in between.
void BeginPropertyPanel(const ThemeConfiguration& Theme, const char* Identifier);

// 📝 Close the properties column opened by BeginPropertyPanel.
void EndPropertyPanel(const ThemeConfiguration& Theme);

// 📝 Open one collapsible property card (header + indented body). Returns true when the card is expanded (record controls only if so).
//    Pair the true-return with EndPropertyCard. Expanded is caller-owned so collapse survives across cycles.
bool BeginPropertyCard(const ThemeConfiguration& Theme, const char* Title, bool* Expanded);

// 📝 Close a property card opened by BeginPropertyCard (only call when BeginPropertyCard returned true).
void EndPropertyCard(const ThemeConfiguration& Theme);

}   // namespace Frontier

#endif
