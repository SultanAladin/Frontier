/*==============================================================================================================================================
                                                              CONTENTSECTION.H
==============================================================================================================================================*/
// 🧩 A scrollable/fixed parameter area: the indented, padded body that sits under a SectionHeader and holds a stack of controls. Opened and
//    closed as a pair. Stateless — one definition wraps every card body in every panel, so all cards share one padding + indent rhythm.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CARDS_CONTENTSECTION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CARDS_CONTENTSECTION_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ContentSectionDescriptor
{
    const char* Identifier;    // [-]  - Unique id for the body region
    float       FixedHeight;   // [px] - >0 makes the body a scrollable child of this height; <=0 grows to content
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Open a card body: apply the theme indent + padding. Pair with EndContentSection. Records controls in between.
void BeginContentSection(const ThemeConfiguration& Theme, const ContentSectionDescriptor& Descriptor);

// 📝 Close a card body opened by BeginContentSection.
void EndContentSection(const ThemeConfiguration& Theme);

}   // namespace Frontier

#endif
