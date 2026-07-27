/*==============================================================================================================================================
                                                              CONTENTCAROUSEL.H
==============================================================================================================================================*/
// 🧩 Scrollable horizontal item list: a strip of thumbnail tiles that scrolls sideways, for material / brush / asset pickers. The caller owns
//    the item set and the selected index. Stateless — one definition serves every gallery in every panel.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CARDS_CONTENTCAROUSEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CARDS_CONTENTCAROUSEL_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One tile. ThumbnailRasterId is a texture handle (SvgIconRaster or any uploaded preview); 0 draws a caption-only tile.
struct CarouselItem
{
    const char* Caption;            // [-] - Tile caption
    unsigned    ThumbnailRasterId;  // [-] - Preview texture handle (0 -> caption-only tile)
};


struct ContentCarouselDescriptor
{
    const char*         Identifier;    // [-]  - Unique id for the strip
    const CarouselItem* Items;         // [-]  - Caller-owned array of tiles
    int                 ItemCount;     // [-]  - Number of tiles
    int*                SelectedIndex; // [-]  - Caller-owned selection, edited in place (null -> non-selectable gallery)
    float               TileSize;      // [px] - Square tile edge (<=0 -> 4x theme row height)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the horizontal tile strip. Returns true on the cycle the selection changed. No-op-safe if Items is null.
bool ConstructContentCarousel(const ThemeConfiguration& Theme, const ContentCarouselDescriptor& Descriptor);

}   // namespace Frontier

#endif
