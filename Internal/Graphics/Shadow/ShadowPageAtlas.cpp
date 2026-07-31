/*==============================================================================================================================================
                                                           SHADOWPAGEATLAS.CPP
==============================================================================================================================================*/
// The physical page pool behind the sun shadows: the R32_UINT atlas image, the three-state ownership records, and the coarsest-LOD-first
// reclaim. See the header for the oversubscription and eviction rulings.

#include "Graphics/Shadow/ShadowPageAtlas.h"

#include <algorithm>
#include <cstring>

namespace Frontier
{

namespace
{

// 📝 First memory type allowed by the requirement bitmask carrying every required property bit — mirrors HierarchicalDepthPyramid / VisibilityDepth.
uint32_t SelectMemoryTypeIndex(VkPhysicalDevice      PhysicalDevice,
                               uint32_t              CompatibleTypesBitmask,
                               VkMemoryPropertyFlags RequiredProperties,
                               bool&                 FoundEnabled)
{
    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);

    for (uint32_t TypeIndex = 0; TypeIndex < MemoryProperties.memoryTypeCount; ++TypeIndex)
    {
        const bool Compatible = (CompatibleTypesBitmask & (1u << TypeIndex)) != 0;
        const bool Qualified  = (MemoryProperties.memoryTypes[TypeIndex].propertyFlags & RequiredProperties) == RequiredProperties;
        if (Compatible && Qualified)
        {
            FoundEnabled = true;
            return TypeIndex;
        }
    }

    FoundEnabled = false;
    return 0;
}

// Break a page's tile mapping and mark it Free, without touching the free list (callers differ on whether the page is being recycled immediately).
void DetachShadowPage(ShadowPageAtlas& Atlas, uint32_t PageIndex)
{
    if (PageIndex >= Atlas.PageRecords.size())
        return;

    ShadowPageRecord& Record = Atlas.PageRecords[PageIndex];

    // 🔴 The slot is read from the RECORD, never recomputed from OwnerTile. The clipmap origin scrolls between the image that filed this page and
    //    the image that evicts it, so a fresh derivation can name a different slot — clearing an innocent mapping and leaving this page's real one
    //    dangling. Two tiles would then point at one page and one of them would read another region's depth.
    if (Record.OwnerSlot < Atlas.TilePageMapping.size() && Atlas.TilePageMapping[Record.OwnerSlot] == PageIndex)
        Atlas.TilePageMapping[Record.OwnerSlot] = ShadowPageUnmapped;

    Record.Ownership    = ShadowPageOwnership::Free;
    Record.OwnerLevel   = 0;
    Record.OwnerTile    = TileCoordinate{};
    Record.OwnerSlot    = UINT32_MAX;
    Record.LastUsedImage = 0;

    // 🔴 A detached page still physically holds the PREVIOUS owner's depth, so it is stale by construction. This is the most dangerous page in the
    //    pool: handed to a new tile in a different world region, its leftover depth is not merely out of date but describes somewhere else entirely.
    //    Forgetting this line is worse than never having the flag, because the new owner would inherit a page asserting "my depth is valid".
    Record.ContentStale = true;
}

// Allocate one buffer + its memory. Mirrors ShadowTileStore's helper; kept local rather than shared because the two units have no other coupling.
bool ProvisionMappingBuffer(VulkanHost&           Host,
                            VkDeviceSize          ByteLength,
                            VkBufferUsageFlags    Usage,
                            VkMemoryPropertyFlags Properties,
                            VkBuffer&             OutBuffer,
                            VkDeviceMemory&       OutMemory)
{
    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteLength;
    BufferInformation.usage       = Usage;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
    {
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits, Properties, MemoryTypeFound);
    if (!MemoryTypeFound)
        return false;

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS)
    {
        OutMemory = VK_NULL_HANDLE;
        return false;
    }

    return vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) == VK_SUCCESS;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

bool InitializeShadowPageAtlas(ShadowPageAtlas& Atlas, VulkanHost& Host, uint32_t LevelCount, uint32_t TileResolution)
{
    FinalizeShadowPageAtlas(Atlas);

    if (LevelCount == 0 || TileResolution == 0)
        return false;

    Atlas.Host           = &Host;
    Atlas.LevelCount     = std::min(LevelCount, ShadowTilemapLodCount);
    Atlas.TileResolution = TileResolution;
    Atlas.ImageOrdinal   = 0;

    VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
    ImageInformation.format        = ShadowPageAtlasFormat;
    ImageInformation.extent        = { ShadowPageAtlasEdge, ShadowPageAtlasEdge, 1 };
    ImageInformation.mipLevels     = 1;
    ImageInformation.arrayLayers   = 1;
    ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInformation.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ImageInformation.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInformation.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Host.Device, &ImageInformation, Host.Allocator, &Atlas.AtlasImage) != VK_SUCCESS)
    {
        Atlas.AtlasImage = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetImageMemoryRequirements(Host.Device, Atlas.AtlasImage, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                           MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                           MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        FinalizeShadowPageAtlas(Atlas);
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &Atlas.AtlasMemory) != VK_SUCCESS ||
        vkBindImageMemory(Host.Device, Atlas.AtlasImage, Atlas.AtlasMemory, 0) != VK_SUCCESS)
    {
        FinalizeShadowPageAtlas(Atlas);
        return false;
    }

    VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInformation.image                       = Atlas.AtlasImage;
    ViewInformation.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    ViewInformation.format                      = ShadowPageAtlasFormat;
    ViewInformation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInformation.subresourceRange.levelCount = 1;
    ViewInformation.subresourceRange.layerCount = 1;
    if (vkCreateImageView(Host.Device, &ViewInformation, Host.Allocator, &Atlas.AtlasStorageView) != VK_SUCCESS ||
        vkCreateImageView(Host.Device, &ViewInformation, Host.Allocator, &Atlas.AtlasSampledView) != VK_SUCCESS)
    {
        FinalizeShadowPageAtlas(Atlas);
        return false;
    }

    Atlas.PageRecords.assign(ShadowPageCapacity, ShadowPageRecord{});

    // Free list is popped from the back, so seeding it in reverse hands out page 0 first — which keeps the atlas's occupied region contiguous from
    // the top-left while the pool is under-subscribed, and makes a debug capture readable.
    Atlas.FreeList.clear();
    Atlas.FreeList.reserve(ShadowPageCapacity);
    for (uint32_t PageIterator = ShadowPageCapacity; PageIterator > 0; --PageIterator)
        Atlas.FreeList.push_back(PageIterator - 1);

    Atlas.TilePageMapping.assign((size_t)Atlas.LevelCount * TileResolution * TileResolution, ShadowPageUnmapped);

    // The device mirror S6/S7 address through. Same element count as the CPU vector, so one memcpy per image with no repacking.
    const VkDeviceSize MappingBytes = (VkDeviceSize)Atlas.TilePageMapping.size() * sizeof(uint32_t);
    if (!ProvisionMappingBuffer(Host, MappingBytes,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                Atlas.MappingBuffer, Atlas.MappingMemory) ||
        !ProvisionMappingBuffer(Host, MappingBytes,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                Atlas.MappingStaging, Atlas.MappingStagingMemory) ||
        vkMapMemory(Host.Device, Atlas.MappingStagingMemory, 0, MappingBytes, 0, &Atlas.MappingStagingMapping) != VK_SUCCESS)
    {
        FinalizeShadowPageAtlas(Atlas);
        return false;
    }

    // 📝 Seed staging with the unmapped sentinel so the very first image reads "no page" rather than uninitialized host memory — the device buffer is
    //    undefined until the first upload, and a shader that read a garbage page index would write depth into an arbitrary corner of the atlas.
    std::memcpy(Atlas.MappingStagingMapping, Atlas.TilePageMapping.data(), (size_t)MappingBytes);

    // One coverage word per PHYSICAL page (not per tile — the tracer already resolves tile -> page before it needs the bit). 1024 words = 4 KiB.
    // 📝 No staging pair: this buffer is filled to zero by the device and written only by the device, so it never needs a host-visible mirror.
    const VkDeviceSize CoverageBytes = (VkDeviceSize)ShadowPageCapacity * sizeof(uint32_t);
    if (!ProvisionMappingBuffer(Host, CoverageBytes,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                Atlas.CoverageBuffer, Atlas.CoverageMemory))
    {
        FinalizeShadowPageAtlas(Atlas);
        return false;
    }

    // One render-mask word per PHYSICAL page, matching the coverage buffer's shape. 1024 words = 4 KiB.
    // 📝 This one DOES need a staging pair: the mask is built on the host from ShadowPageNeedsRender (a CPU-side predicate over PageRecords) and pushed
    //    every image, exactly like TilePageMapping and unlike coverage, which the device both fills and writes.
    const VkDeviceSize RenderMaskBytes = (VkDeviceSize)ShadowPageCapacity * sizeof(uint32_t);
    if (!ProvisionMappingBuffer(Host, RenderMaskBytes,
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                Atlas.RenderMaskBuffer, Atlas.RenderMaskMemory) ||
        !ProvisionMappingBuffer(Host, RenderMaskBytes,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                Atlas.RenderMaskStaging, Atlas.RenderMaskStagingMemory) ||
        vkMapMemory(Host.Device, Atlas.RenderMaskStagingMemory, 0, RenderMaskBytes, 0, &Atlas.RenderMaskStagingMapping) != VK_SUCCESS)
    {
        FinalizeShadowPageAtlas(Atlas);
        return false;
    }

    // 📝 Seed the mask to zero — authorize nothing until the first UploadShadowPageRenderMask has run. The opposite seed would let the first image's S7
    //    write into every page the device buffer happens to describe, which is the very asymmetry this buffer exists to close.
    std::memset(Atlas.RenderMaskStagingMapping, 0, (size_t)RenderMaskBytes);

    Atlas.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Atlas.Census         = ShadowPageCensus{};
    RefreshShadowPageCensus(Atlas);
    Atlas.ReadyCondition = true;
    return true;
}

void FinalizeShadowPageAtlas(ShadowPageAtlas& Atlas)
{
    if (Atlas.Host != nullptr && Atlas.Host->Device != VK_NULL_HANDLE)
    {
        VulkanHost& Host = *Atlas.Host;
        if (Atlas.AtlasSampledView != VK_NULL_HANDLE) vkDestroyImageView(Host.Device, Atlas.AtlasSampledView, Host.Allocator);
        if (Atlas.AtlasStorageView != VK_NULL_HANDLE) vkDestroyImageView(Host.Device, Atlas.AtlasStorageView, Host.Allocator);
        if (Atlas.AtlasImage       != VK_NULL_HANDLE) vkDestroyImage(Host.Device, Atlas.AtlasImage, Host.Allocator);
        if (Atlas.AtlasMemory      != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Atlas.AtlasMemory, Host.Allocator);

        if (Atlas.MappingStagingMapping != nullptr)         vkUnmapMemory(Host.Device, Atlas.MappingStagingMemory);
        if (Atlas.MappingStaging        != VK_NULL_HANDLE)  vkDestroyBuffer(Host.Device, Atlas.MappingStaging, Host.Allocator);
        if (Atlas.MappingStagingMemory  != VK_NULL_HANDLE)  vkFreeMemory(Host.Device, Atlas.MappingStagingMemory, Host.Allocator);
        if (Atlas.MappingBuffer         != VK_NULL_HANDLE)  vkDestroyBuffer(Host.Device, Atlas.MappingBuffer, Host.Allocator);
        if (Atlas.MappingMemory         != VK_NULL_HANDLE)  vkFreeMemory(Host.Device, Atlas.MappingMemory, Host.Allocator);
        if (Atlas.CoverageBuffer        != VK_NULL_HANDLE)  vkDestroyBuffer(Host.Device, Atlas.CoverageBuffer, Host.Allocator);
        if (Atlas.CoverageMemory        != VK_NULL_HANDLE)  vkFreeMemory(Host.Device, Atlas.CoverageMemory, Host.Allocator);

        if (Atlas.RenderMaskStagingMapping != nullptr)         vkUnmapMemory(Host.Device, Atlas.RenderMaskStagingMemory);
        if (Atlas.RenderMaskStaging        != VK_NULL_HANDLE)  vkDestroyBuffer(Host.Device, Atlas.RenderMaskStaging, Host.Allocator);
        if (Atlas.RenderMaskStagingMemory  != VK_NULL_HANDLE)  vkFreeMemory(Host.Device, Atlas.RenderMaskStagingMemory, Host.Allocator);
        if (Atlas.RenderMaskBuffer         != VK_NULL_HANDLE)  vkDestroyBuffer(Host.Device, Atlas.RenderMaskBuffer, Host.Allocator);
        if (Atlas.RenderMaskMemory         != VK_NULL_HANDLE)  vkFreeMemory(Host.Device, Atlas.RenderMaskMemory, Host.Allocator);
    }

    Atlas.MappingStagingMapping = nullptr;
    Atlas.MappingStaging        = VK_NULL_HANDLE;
    Atlas.MappingStagingMemory  = VK_NULL_HANDLE;
    Atlas.MappingBuffer         = VK_NULL_HANDLE;
    Atlas.MappingMemory         = VK_NULL_HANDLE;
    Atlas.CoverageBuffer        = VK_NULL_HANDLE;
    Atlas.CoverageMemory        = VK_NULL_HANDLE;

    Atlas.RenderMaskStagingMapping = nullptr;
    Atlas.RenderMaskStaging        = VK_NULL_HANDLE;
    Atlas.RenderMaskStagingMemory  = VK_NULL_HANDLE;
    Atlas.RenderMaskBuffer         = VK_NULL_HANDLE;
    Atlas.RenderMaskMemory         = VK_NULL_HANDLE;

    Atlas.AtlasSampledView = VK_NULL_HANDLE;
    Atlas.AtlasStorageView = VK_NULL_HANDLE;
    Atlas.AtlasImage       = VK_NULL_HANDLE;
    Atlas.AtlasMemory      = VK_NULL_HANDLE;
    Atlas.CurrentLayout    = VK_IMAGE_LAYOUT_UNDEFINED;

    Atlas.PageRecords.clear();
    Atlas.FreeList.clear();
    Atlas.TilePageMapping.clear();
    Atlas.LevelCount     = 0;
    Atlas.TileResolution = 0;
    Atlas.ImageOrdinal   = 0;
    Atlas.Census         = ShadowPageCensus{};
    Atlas.ReadyCondition = false;
    Atlas.Host           = nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

uint32_t ResolveShadowTileSlot(const ShadowPageAtlas& Atlas, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    if (Level >= Atlas.LevelCount || Atlas.TileResolution == 0)
        return UINT32_MAX;

    // 📝 Addressed through the CLIPMAP's wrap, not a bare modulo of the light tile: the window has scrolled, so the same light tile maps to a
    //    different physical slot than it did last image. Using a bare modulo here would disagree with what the GPU computes.
    const uint32_t WithinLevel = ResolveSunShadowTileIndex(Clipmap, Level, LightTile);
    return Level * Atlas.TileResolution * Atlas.TileResolution + WithinLevel;
}

uint32_t ResolveShadowTilePage(const ShadowPageAtlas& Atlas, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    const uint32_t Slot = ResolveShadowTileSlot(Atlas, Clipmap, Level, LightTile);
    if (Slot >= Atlas.TilePageMapping.size())
        return ShadowPageUnmapped;
    return Atlas.TilePageMapping[Slot];
}

void ResolveShadowPageOrigin(uint32_t PageIndex, uint32_t& OutTexelX, uint32_t& OutTexelY)
{
    OutTexelX = (PageIndex % ShadowPageAtlasPageEdge) * ShadowPageResolution;
    OutTexelY = (PageIndex / ShadowPageAtlasPageEdge) * ShadowPageResolution;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

void OpenShadowPageImage(ShadowPageAtlas& Atlas)
{
    ++Atlas.ImageOrdinal;

    // 🔴 Every Used page demotes to Cached at the top of the image. Without this, a page claimed once would stay Used forever and become
    //    permanently unreclaimable — the pool would leak until it exhausted and shadows would fail in blocks that never recover.
    for (ShadowPageRecord& Record : Atlas.PageRecords)
        if (Record.Ownership == ShadowPageOwnership::Used)
            Record.Ownership = ShadowPageOwnership::Cached;

    Atlas.Census.PageRequestCount = 0;
    Atlas.Census.PageStarvedCount = 0;
    Atlas.Census.PageEvictedCount = 0;
}

uint32_t RequestShadowPage(ShadowPageAtlas& Atlas, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    if (!Atlas.ReadyCondition || Level >= Atlas.LevelCount)
        return ShadowPageUnmapped;

    const uint32_t Slot = ResolveShadowTileSlot(Atlas, Clipmap, Level, LightTile);
    if (Slot >= Atlas.TilePageMapping.size())
        return ShadowPageUnmapped;

    ++Atlas.Census.PageRequestCount;

    // ---- 1. The cache hit that makes a scrolling camera cheap -------------------------------------------------------
    const uint32_t Existing = Atlas.TilePageMapping[Slot];
    if (Existing != ShadowPageUnmapped && Existing < Atlas.PageRecords.size())
    {
        ShadowPageRecord& Record = Atlas.PageRecords[Existing];

        // 🔴 A SLOT MATCH IS NOT A TILE MATCH, AND TRUSTING IT SHIPS ANOTHER REGION'S DEPTH AS THIS TILE'S. The slot is derived from the CURRENT
        //    toroidal origin, but the page at that slot was filed under whatever origin was live when it was granted. The window scrolls with the
        //    observer, so after a scroll this same light tile hashes to a DIFFERENT slot, and the page sitting there belongs to a different light
        //    tile entirely — one the mapping has not been told about yet. Claiming it here (as the code did until 2026-07-30) overwrote OwnerTile
        //    with the requester's and left ContentStale FALSE, so ShadowPageNeedsRender returned false, S6 never cleared it and S7 never redrew it:
        //    the tracer then read the previous tile's caster depth through a mapping that confidently named this one. On screen that is PAGE-SIZED
        //    AXIS-ALIGNED BLOCKS of shadow scattered across the floor, far from any caster, with no shadow under the caster itself — which is not
        //    read as an addressing fault because every count in the census stays healthy. InvalidateShadowLevelPages documents the same scrolled-
        //    origin hazard and sweeps a whole level to escape it; this is the per-image form of it.
        // ⚠️ The identity is the RECORD's own OwnerTile/OwnerLevel, never the slot: the record is the only thing that remembers which tile's depth is
        //    physically in the page. A mismatch is not a cache hit at all — fall through and let the page be re-granted stale below.
        const bool IdentityMatches = (Record.Ownership != ShadowPageOwnership::Free) &&
                                     (Record.OwnerLevel == Level) &&
                                     (Record.OwnerTile.XTile == LightTile.XTile) &&
                                     (Record.OwnerTile.YTile == LightTile.YTile);
        if (!IdentityMatches)
        {
            // The slot is lying about this page. Break the mapping and fall through to a fresh grant, which sets ContentStale by construction.
            DetachShadowPage(Atlas, Existing);
            Atlas.TilePageMapping[Slot] = ShadowPageUnmapped;
            Atlas.FreeList.push_back(Existing);
        }
        else
        {
            Record.Ownership     = ShadowPageOwnership::Used;
            Record.OwnerSlot     = Slot;
            Record.LastUsedImage = Atlas.ImageOrdinal;
            // 📝 OwnerLevel and OwnerTile are NOT reassigned: the identity test above already proved they equal Level/LightTile, so writing them
            //    would be a no-op that reads as if the record's identity were the requester's to redefine. Only OwnerSlot can legitimately move,
            //    because the slot a tile hashes to changes with the scrolling origin while the tile itself does not.
            // 🔴 ContentStale IS DELIBERATELY NOT TOUCHED HERE. Re-requesting a tile means "I still want this page", not "its depth is fine" — the
            //    request comes from the clipmap, which knows nothing about what moved inside the tile. Clearing the flag here would restore the exact
            //    defect the flag was added to fix (a moved caster's page taken as valid forever); setting it would re-render every cached page every
            //    image and throw away the cache the whole pool exists to provide. Only a real render clears it, via MarkShadowPageRendered.
            return Existing;
        }
    }

    // ---- 2. Free list ------------------------------------------------------------------------------------------------
    if (!Atlas.FreeList.empty())
    {
        const uint32_t PageIndex = Atlas.FreeList.back();
        Atlas.FreeList.pop_back();

        ShadowPageRecord& Record = Atlas.PageRecords[PageIndex];
        Record.Ownership    = ShadowPageOwnership::Used;
        Record.OwnerLevel   = Level;
        Record.OwnerTile    = LightTile;
        Record.OwnerSlot    = Slot;
        Record.LastUsedImage = Atlas.ImageOrdinal;
        // A page arriving from the free list holds either the clear identity or a previous owner's leftover depth. Either way nothing has drawn THIS
        // tile into it. Set explicitly rather than inheriting Detach's flag, so the invariant holds without the reader having to trace it.
        Record.ContentStale = true;
        Atlas.TilePageMapping[Slot] = PageIndex;
        return PageIndex;
    }

    // ---- 3. Evict — COARSEST LOD FIRST, oldest-first within a level ------------------------------------------------
    // 🔴 The reclaim scans from the coarsest level DOWN to (but excluding) the requesting level, so detail is surrendered at distance where a
    //    missing 128x128 shadow block reads as haze, and LOD 0's near-field contact shadows are given up last. Strict LOD-agnostic LRU was
    //    rejected precisely because it can take a near page mid-pan and punch a visible hole in front of the viewer. (User ruling, 2026-07-30.)
    // ⚠️ The scan STOPS at Level rather than running to 0, so this scan alone reaches only levels COARSER than the requester. That is a PREFERENCE,
    //    not a limit on what the pool may recycle: scan 4 adds the requester's own level and scan 5 adds every remaining level once both ordered
    //    passes have failed. Treating this bound as the last word is what produced the unreachable-band starvation documented on scan 5.
    uint32_t VictimPage = ShadowPageUnmapped;
    for (uint32_t Candidate = Atlas.LevelCount; Candidate > Level + 1 && VictimPage == ShadowPageUnmapped; --Candidate)
    {
        const uint32_t CandidateLevel = Candidate - 1;

        uint64_t OldestImage = UINT64_MAX;
        for (uint32_t PageIterator = 0; PageIterator < Atlas.PageRecords.size(); ++PageIterator)
        {
            const ShadowPageRecord& Record = Atlas.PageRecords[PageIterator];
            // Only CACHED pages are reclaimable. A Used page holds depth wanted THIS image, and no policy may take it.
            if (Record.Ownership != ShadowPageOwnership::Cached || Record.OwnerLevel != CandidateLevel)
                continue;
            if (Record.LastUsedImage < OldestImage)
            {
                OldestImage = Record.LastUsedImage;
                VictimPage  = PageIterator;
            }
        }
    }

    // ---- 4. Reclaim THIS level's own stale pages ----------------------------------------------------------------------
    // 🔴 Without this a walking camera exhausts the pool permanently — it abandons the tiles
    //    behind it every image, and those pages stay Cached and unreachable forever because the coarser-only scan above never looks at them. The
    //    steady state was used=72 cached=184 free=0, with every subsequent request starving. (Found by the P6.3b motion probe; the P6.2 churn
    //    probe missed it because it spread requests across ALL levels, so its fine-level requests always found a coarse victim.)
    // ⚠️ This does NOT reintroduce the LOD 4/5 thrash the bound above prevents, and the distinction is the whole reason it is safe: a page is only
    //    taken here when its LastUsedImage is OLDER than the current image, i.e. its tile was not requested this image. Two tiles contested within
    //    one image can never evict each other, because a page claimed this image is Used, not Cached.
    if (VictimPage == ShadowPageUnmapped)
    {
        uint64_t OldestImage = UINT64_MAX;
        for (uint32_t PageIterator = 0; PageIterator < Atlas.PageRecords.size(); ++PageIterator)
        {
            const ShadowPageRecord& Record = Atlas.PageRecords[PageIterator];
            if (Record.Ownership != ShadowPageOwnership::Cached || Record.OwnerLevel != Level)
                continue;
            if (Record.LastUsedImage < Atlas.ImageOrdinal && Record.LastUsedImage < OldestImage)
            {
                OldestImage = Record.LastUsedImage;
                VictimPage  = PageIterator;
            }
        }
    }

    // ---- 5. Last resort: the oldest cached page at ANY level -----------------------------------------------------------
    // 🔴 WITHOUT THIS THE POOL HAS AN UNREACHABLE BAND AND STARVES WITH 1020 OF 1024 PAGES IDLE. Scan 3 reaches levels strictly COARSER than the
    //    requester; scan 4 reaches the requester's OWN level. Nothing reaches levels FINER than the requester, so a coarse request could never
    //    reclaim a fine page no matter how long it had gone untouched. L5 is the degenerate case: scan 3's bound (Candidate > Level + 1) makes its
    //    loop body unreachable, so L5 could only ever take another L5 page. Measured steady state was `starve L5 | L0=733 L1=187 L2=73 L3=19 L4=8
    //    L5=0` — every page in the atlas cached, none of it reclaimable, every L5 request starving forever. Because pages are only ever recycled by
    //    eviction (OpenShadowPageImage demotes Used -> Cached; nothing returns them to the free list), that band is not a rare fallback — it is a
    //    permanent leak. On screen: tiles that never receive a shadow, casters missing entirely, and page-sized patches flickering as the window
    //    scrolls and the set of doomed requests changes.
    // ⚠️ THIS DOES NOT WEAKEN THE COARSEST-FIRST PREFERENCE, because it only runs after both ordered scans have already failed. The LOD ordering is
    //    still the policy; this is the escape hatch that keeps "no victim by preference" from meaning "no shadow at all".
    // 🔴 The `LastUsedImage < ImageOrdinal` guard is what makes a global scan safe, and it must not be dropped. Every grant path stamps
    //    LastUsedImage = ImageOrdinal, so a page claimed THIS image is both Used and current-stamped and can never be taken here. That is precisely
    //    the intra-image thrash (LOD 4 and LOD 5 trading pages every image) the level bound on scan 3 was originally introduced to prevent — the
    //    guard enforces it directly, by recency, instead of by forbidding whole levels.
    if (VictimPage == ShadowPageUnmapped)
    {
        uint64_t OldestImage = UINT64_MAX;
        for (uint32_t PageIterator = 0; PageIterator < Atlas.PageRecords.size(); ++PageIterator)
        {
            const ShadowPageRecord& Record = Atlas.PageRecords[PageIterator];
            if (Record.Ownership != ShadowPageOwnership::Cached)
                continue;
            if (Record.LastUsedImage < Atlas.ImageOrdinal && Record.LastUsedImage < OldestImage)
            {
                OldestImage = Record.LastUsedImage;
                VictimPage  = PageIterator;
            }
        }
    }

    if (VictimPage == ShadowPageUnmapped)
    {
        // Genuine exhaustion: every page in the pool was claimed THIS image. The tile gets no page and must not be sampled — the census carries it.
        ++Atlas.Census.PageStarvedCount;
        return ShadowPageUnmapped;
    }

    DetachShadowPage(Atlas, VictimPage);
    ++Atlas.Census.PageEvictedCount;

    ShadowPageRecord& Record = Atlas.PageRecords[VictimPage];
    Record.Ownership    = ShadowPageOwnership::Used;
    Record.OwnerLevel   = Level;
    Record.OwnerTile    = LightTile;
    Record.OwnerSlot    = Slot;
    Record.LastUsedImage = Atlas.ImageOrdinal;
    // An evicted page holds the victim tile's depth, from a different region and often a different LOD. Unconditionally stale.
    Record.ContentStale = true;
    Atlas.TilePageMapping[Slot] = VictimPage;
    return VictimPage;
}

void ReleaseShadowTilePage(ShadowPageAtlas& Atlas, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    const uint32_t Slot = ResolveShadowTileSlot(Atlas, Clipmap, Level, LightTile);
    if (Slot >= Atlas.TilePageMapping.size())
        return;

    const uint32_t PageIndex = Atlas.TilePageMapping[Slot];
    if (PageIndex == ShadowPageUnmapped || PageIndex >= Atlas.PageRecords.size())
        return;

    DetachShadowPage(Atlas, PageIndex);
    Atlas.TilePageMapping[Slot] = ShadowPageUnmapped;
    Atlas.FreeList.push_back(PageIndex);
}

void InvalidateShadowLevelPages(ShadowPageAtlas& Atlas, uint32_t Level)
{
    if (Level >= Atlas.LevelCount)
        return;

    for (uint32_t PageIterator = 0; PageIterator < Atlas.PageRecords.size(); ++PageIterator)
    {
        if (Atlas.PageRecords[PageIterator].Ownership == ShadowPageOwnership::Free ||
            Atlas.PageRecords[PageIterator].OwnerLevel != Level)
            continue;

        DetachShadowPage(Atlas, PageIterator);
        Atlas.FreeList.push_back(PageIterator);
    }

    // Any slot still pointing somewhere is stale by construction — DetachShadowPage clears only the slot it can derive, and a scrolled origin can
    // leave an older mapping behind. Sweeping the level's own range is what guarantees no slot outlives its page.
    const uint32_t Base = Level * Atlas.TileResolution * Atlas.TileResolution;
    const uint32_t Span = Atlas.TileResolution * Atlas.TileResolution;
    for (uint32_t SlotIterator = Base; SlotIterator < Base + Span && SlotIterator < Atlas.TilePageMapping.size(); ++SlotIterator)
        Atlas.TilePageMapping[SlotIterator] = ShadowPageUnmapped;
}

uint32_t InvalidateScrolledShadowPages(ShadowPageAtlas&             Atlas,
                                      const SunShadowClipmap&      Clipmap,
                                      const SunShadowScrollResult& Scroll)
{
    if (!Atlas.ReadyCondition || Scroll.Level >= Clipmap.Levels.size())
        return 0;

    // A rotated sun re-expresses the whole lattice in a new basis, so no cached page survives and the strips are meaningless.
    if (Scroll.WholeWindowDirtyCondition)
    {
        const uint32_t BeforeCount = (uint32_t)Atlas.FreeList.size();
        InvalidateShadowLevelPages(Atlas, Scroll.Level);
        return (uint32_t)Atlas.FreeList.size() - BeforeCount;
    }

    // Nothing moved: the common standing-still path, and the reason a static camera shows no artefact at all.
    if (Scroll.ExposedStripCount == 0)
        return 0;

    const SunShadowLevel& Window     = Clipmap.Levels[Scroll.Level];
    const int32_t         Resolution = (int32_t)Window.Resolution;

    // ⚠️ Enumerated EXACTLY as IntegrateSunShadowResidency does, against the post-scroll origin. The window spans [-Origin, -Origin + Resolution) on
    //    each axis and a strip narrows that range on its own axis; any divergence between the two enumerations would leave a tile marked vacant on the
    //    CPU while its page stayed mapped on the GPU, which is precisely the state this function exists to prevent.
    const int32_t WindowMinimumX = -Window.ToroidalOrigin.XTile;
    const int32_t WindowMinimumY = -Window.ToroidalOrigin.YTile;

    uint32_t ReleasedCount = 0;

    for (uint32_t StripIterator = 0; StripIterator < Scroll.ExposedStripCount; ++StripIterator)
    {
        const ToroidalStrip& Strip = Scroll.ExposedStrips[StripIterator];

        const int32_t MinimumX = (Strip.Axis == 0) ? Strip.MinimumCell : WindowMinimumX;
        const int32_t MaximumX = (Strip.Axis == 0) ? Strip.MaximumCell : WindowMinimumX + Resolution;
        const int32_t MinimumY = (Strip.Axis == 1) ? Strip.MinimumCell : WindowMinimumY;
        const int32_t MaximumY = (Strip.Axis == 1) ? Strip.MaximumCell : WindowMinimumY + Resolution;

        for (int32_t YTile = MinimumY; YTile < MaximumY; ++YTile)
        {
            for (int32_t XTile = MinimumX; XTile < MaximumX; ++XTile)
            {
                const TileCoordinate LightTile{ XTile, YTile };
                const uint32_t       Slot = ResolveShadowTileSlot(Atlas, Clipmap, Scroll.Level, LightTile);

                // Counted before the release so the tally reflects pages actually dropped, not tiles visited — an already-unmapped tile is free.
                if (Slot < Atlas.TilePageMapping.size() && Atlas.TilePageMapping[Slot] != ShadowPageUnmapped)
                {
                    ReleaseShadowTilePage(Atlas, Clipmap, Scroll.Level, LightTile);
                    ++ReleasedCount;
                }
            }
        }
    }

    return ReleasedCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   STALENESS (S2 prerequisite)
//------------------------------------------------------------------------------------------------------------------------

void InvalidateShadowTileContent(ShadowPageAtlas& Atlas, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    const uint32_t PageIndex = ResolveShadowTilePage(Atlas, Clipmap, Level, LightTile);

    // An unmapped tile needs no tag: it has no page to hold wrong depth, and whatever page it is later granted starts stale by construction.
    if (PageIndex == ShadowPageUnmapped || PageIndex >= Atlas.PageRecords.size())
        return;

    Atlas.PageRecords[PageIndex].ContentStale = true;
}

uint32_t InvalidateShadowPagesInSphere(ShadowPageAtlas&        Atlas,
                                      const SunShadowClipmap& Clipmap,
                                      Vector3f                WorldCentre,
                                      float                   WorldRadius)
{
    if (!Atlas.ReadyCondition || Atlas.TileResolution == 0)
        return 0;

    const float Radius = (WorldRadius > 0.0f) ? WorldRadius : 0.0f;

    uint32_t TaggedCount = 0;
    for (uint32_t Level = 0; Level < Atlas.LevelCount && Level < (uint32_t)Clipmap.Levels.size(); ++Level)
    {
        // 📝 The tile span is taken by projecting the sphere's extremes through the PUBLIC ResolveSunShadowTile rather than by re-deriving the light
        //    basis here. A sphere is rotation-invariant, so centre ± radius along each world axis bounds it in light space too, whatever the sun is
        //    doing — which is why this unit needs no knowledge of the light frame at all. (An OBB would not have that property: it would need the
        //    basis, and the projection would have to be done properly per corner.)
        const TileCoordinate MinTile = ResolveSunShadowTile(Clipmap, Level,
                                                            Vector3f{ WorldCentre.XCoord - Radius,
                                                                      WorldCentre.YCoord - Radius,
                                                                      WorldCentre.ZCoord - Radius });
        const TileCoordinate MaxTile = ResolveSunShadowTile(Clipmap, Level,
                                                            Vector3f{ WorldCentre.XCoord + Radius,
                                                                      WorldCentre.YCoord + Radius,
                                                                      WorldCentre.ZCoord + Radius });

        // The projection can invert either axis depending on the light basis's handedness, so the span is normalized rather than assumed ordered.
        const int32_t LowX  = std::min(MinTile.XTile, MaxTile.XTile);
        const int32_t HighX = std::max(MinTile.XTile, MaxTile.XTile);
        const int32_t LowY  = std::min(MinTile.YTile, MaxTile.YTile);
        const int32_t HighY = std::max(MinTile.YTile, MaxTile.YTile);

        // ⚠️ A span wider than the window means the caster covers the whole level; clamping the ITERATION (not the coordinates) keeps the cost bounded
        //    at Resolution² per level while still tagging every resident tile, because the toroidal wrap folds any window-sized run onto every slot.
        const int32_t Span = (int32_t)Atlas.TileResolution;
        const int32_t StopX = std::min(HighX, LowX + Span - 1);
        const int32_t StopY = std::min(HighY, LowY + Span - 1);

        for (int32_t YTile = LowY; YTile <= StopY; ++YTile)
        {
            for (int32_t XTile = LowX; XTile <= StopX; ++XTile)
            {
                const uint32_t PageIndex = ResolveShadowTilePage(Atlas, Clipmap, Level, TileCoordinate{ XTile, YTile });
                if (PageIndex == ShadowPageUnmapped || PageIndex >= Atlas.PageRecords.size())
                    continue;

                ShadowPageRecord& Record = Atlas.PageRecords[PageIndex];
                if (Record.ContentStale)
                    continue;                       // already tagged; not recounted, so the return value is "newly dirtied"

                Record.ContentStale = true;
                ++TaggedCount;
            }
        }
    }

    return TaggedCount;
}

void MarkShadowPageRendered(ShadowPageAtlas& Atlas, uint32_t PageIndex)
{
    if (PageIndex >= Atlas.PageRecords.size())
        return;

    // ⚠️ Only a page that actually exists may be declared clean. Clearing the flag on a Free page would let the next tile to receive it inherit a
    //    "depth is valid" claim covering a region nothing has ever rasterized.
    if (Atlas.PageRecords[PageIndex].Ownership == ShadowPageOwnership::Free)
        return;

    Atlas.PageRecords[PageIndex].ContentStale = false;
}

bool ShadowPageNeedsRender(const ShadowPageAtlas& Atlas, uint32_t PageIndex)
{
    if (PageIndex >= Atlas.PageRecords.size())
        return false;

    const ShadowPageRecord& Record = Atlas.PageRecords[PageIndex];

    // 🔴 BOTH conditions, which is what makes the flag a performance win rather than a cost. `Used` alone would redraw every wanted page every image
    //    (no caching at all); `ContentStale` alone would redraw cached pages nothing is looking at (wasted work off-screen). The conjunction is the
    //    minimum correct set: exactly the pages that are both wanted now and wrong now.
    return Record.Ownership == ShadowPageOwnership::Used && Record.ContentStale;
}

uint32_t MarkShadowDepthPagesRendered(ShadowPageAtlas& Atlas, ShadowTileStore& Store, const SunShadowClipmap& Clipmap)
{
    if (!Atlas.ReadyCondition)
        return 0;

    // 📝 Collected first, then lowered, because MarkShadowPageRendered mutates the very predicate the walk tests. Clearing inside the loop would be
    //    correct only by accident of iteration order — the page whose flag was just lowered stops matching, so a single pass happens to work, but a
    //    later reader of this code cannot see that it is safe. Two phases make it obvious and cost one small stack array.
    uint32_t MarkedPages[ShadowPageCapacity];
    uint32_t MarkedCount = 0;

    const uint32_t PageTotal = static_cast<uint32_t>(Atlas.PageRecords.size());
    for (uint32_t Page = 0; Page < PageTotal && MarkedCount < ShadowPageCapacity; ++Page)
    {
        // 🔴 The SAME predicate S6's clear list used, so the set marked clean is exactly the set that was primed and rasterized. Re-testing rather than
        //    taking S6's list keeps the two in step by construction instead of by the caller remembering to pass the right vector.
        if (ShadowPageNeedsRender(Atlas, Page))
            MarkedPages[MarkedCount++] = Page;
    }

    for (uint32_t Index = 0; Index < MarkedCount; ++Index)
    {
        const uint32_t          Page   = MarkedPages[Index];
        const ShadowPageRecord& Record = Atlas.PageRecords[Page];

        // 🔴 Lower the TILE's Update bit as well as the page's ContentStale, because the two flags gate different passes and leaving either raised keeps
        //    the redraw alive. ContentStale drives S6/S7 (via ShadowPageNeedsRender); Update drives ShadowTileNeedsRender, which the masking and the
        //    tracer consult. Clearing only one would make the pass look cached from one side and permanently dirty from the other.
        // ⚠️ Keyed off OwnerLevel/OwnerTile, which the record carries precisely so this reverse lookup needs no search. OwnerSlot is authoritative over
        //    OwnerTile per the header, but MarkShadowTileRendered takes a tile and re-derives the slot through the same addressing S7 used, so passing
        //    the tile keeps this on the one code path whose sign convention is already pinned.
        if (Store.ReadyCondition && Clipmap.ReadyCondition)
            MarkShadowTileRendered(Store, Clipmap, Record.OwnerLevel, Record.OwnerTile);

        MarkShadowPageRendered(Atlas, Page);
    }

    // 📝 The census is NOT recomputed here. It is read one image stale by design (§4 C6), and the next RefreshShadowPageCensus picks the cleared flags
    //    up. Refreshing it now would make this image's trace disagree with the S6 clear count it is supposed to be comparable against.
    return MarkedCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            S5 DRIVE + GPU MAPPING
//------------------------------------------------------------------------------------------------------------------------

uint32_t DriveShadowPageAllocation(ShadowPageAtlas& Atlas, const ShadowTileStore& Store, const SunShadowClipmap& Clipmap)
{
    if (!Atlas.ReadyCondition || !Store.ReadyCondition || !Clipmap.ReadyCondition)
        return 0;

    const uint32_t LevelCount = std::min(Atlas.LevelCount, (uint32_t)Clipmap.Levels.size());
    uint32_t       RequestCount = 0;

    // 🔴 COARSEST FIRST, so the few coarse pages that blanket the whole window are placed before the many fine ones compete for the pool. This
    //    ordering used to be load-bearing for a second reason that no longer holds: RequestShadowPage could only evict from levels COARSER than the
    //    requester, so serving fine levels first left coarse requests with nothing reclaimable. Its scan 5 now reaches any level, so the ordering is
    //    a priority policy rather than the thing standing between a coarse request and starvation.
    for (uint32_t Candidate = LevelCount; Candidate > 0; --Candidate)
    {
        const uint32_t        Level  = Candidate - 1;
        const SunShadowLevel& Window = Clipmap.Levels[Level];
        const TileCoordinate  Origin = Window.ToroidalOrigin;

        for (uint32_t SlotY = 0; SlotY < Window.Resolution; ++SlotY)
        {
            for (uint32_t SlotX = 0; SlotX < Window.Resolution; ++SlotX)
            {
                // 🔴 SLOT -> LIGHT TILE IS `Slot - Origin`, because ToroidalOrigin is stored in ADD form (the negated corner) — the convention
                //    ResolveSunShadowPhysicalTile adds through, now verified word-for-word against the shipped .spv (0/6144 mismatches, 1704 against
                //    the wrong sign). Getting it backwards still enumerates all 32 slots bijectively, so no count would change; it would simply name a
                //    different set of light tiles and allocate pages for a region nothing is looking at.
                const TileCoordinate LightTile{ (int32_t)SlotX - Origin.XTile, (int32_t)SlotY - Origin.YTile };

                // Only tiles whose demand survived masking get a page. ShadowTileRequestsPage is the single gate — testing Used directly here would
                // hand pages to tiles masking already declared redundant, spending the pool on depth nothing samples.
                if (!ShadowTileRequestsPage(Store, Clipmap, Level, LightTile))
                    continue;

                ++RequestCount;
                RequestShadowPage(Atlas, Clipmap, Level, LightTile);
            }
        }
    }

    return RequestCount;
}

void UploadShadowPageMapping(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer)
{
    if (!Atlas.ReadyCondition || Atlas.MappingStagingMapping == nullptr || CommandBuffer == VK_NULL_HANDLE)
        return;

    const VkDeviceSize MappingBytes = (VkDeviceSize)Atlas.TilePageMapping.size() * sizeof(uint32_t);
    if (MappingBytes == 0)
        return;

    std::memcpy(Atlas.MappingStagingMapping, Atlas.TilePageMapping.data(), (size_t)MappingBytes);

    VkBufferCopy CopyRegion = {};
    CopyRegion.size = MappingBytes;
    vkCmdCopyBuffer(CommandBuffer, Atlas.MappingStaging, Atlas.MappingBuffer, 1, &CopyRegion);

    // The copy must land before any shader reads a page index through it — otherwise S7 resolves a target texel from whatever the previous image left,
    // and rasterizes this image's depth into last image's page.
    VkBufferMemoryBarrier MappingBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    MappingBarrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    MappingBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    MappingBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    MappingBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    MappingBarrier.buffer              = Atlas.MappingBuffer;
    MappingBarrier.offset              = 0;
    MappingBarrier.size                = MappingBytes;

    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 1, &MappingBarrier, 0, nullptr);
}

uint32_t UploadShadowPageRenderMask(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer)
{
    if (!Atlas.ReadyCondition || Atlas.RenderMaskBuffer == VK_NULL_HANDLE || CommandBuffer == VK_NULL_HANDLE)
        return 0;

    uint32_t* MaskEntries = static_cast<uint32_t*>(Atlas.RenderMaskStagingMapping);
    if (MaskEntries == nullptr)
        return 0;

    // 🔴 THE SAME PREDICATE S6 CLEARS ON, page by page, rebuilt from scratch every image. This is the entire contract: S7 rasterizes whole tile windows
    //    and has no way to know which pages were primed, so a page whose depth is still valid must be refused here or the atomic-min blends this image's
    //    casters into depth that was already correct — and min never releases, so the stale silhouette is permanent.
    uint32_t       AuthorizedCount = 0;
    const uint32_t PageTotal       = (uint32_t)Atlas.PageRecords.size();
    for (uint32_t Page = 0; Page < PageTotal; ++Page)
    {
        const bool NeedsRender = ShadowPageNeedsRender(Atlas, Page);
        MaskEntries[Page] = NeedsRender ? 1u : 0u;
        AuthorizedCount += NeedsRender ? 1u : 0u;
    }

    // ⚠️ Zero the tail beyond the record count so a short PageRecords vector cannot leave a stale 1 authorizing a page that does not exist.
    for (uint32_t Page = PageTotal; Page < ShadowPageCapacity; ++Page)
        MaskEntries[Page] = 0u;

    const VkDeviceSize MaskBytes = (VkDeviceSize)ShadowPageCapacity * sizeof(uint32_t);

    VkBufferCopy CopyRegion = {};
    CopyRegion.size = MaskBytes;
    vkCmdCopyBuffer(CommandBuffer, Atlas.RenderMaskStaging, Atlas.RenderMaskBuffer, 1, &CopyRegion);

    // 🔴 The copy must land before S7's first fragment reads it. Read-only destination access here is correct and sufficient — unlike the coverage
    //    barrier, nothing in the pass writes this buffer, so there is no fill-versus-atomic race to order.
    VkBufferMemoryBarrier MaskBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    MaskBarrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    MaskBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    MaskBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    MaskBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    MaskBarrier.buffer              = Atlas.RenderMaskBuffer;
    MaskBarrier.offset              = 0;
    MaskBarrier.size                = MaskBytes;

    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 1, &MaskBarrier, 0, nullptr);

    return AuthorizedCount;
}

uint32_t ClearShadowPageCoverage(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer)
{
    if (!Atlas.ReadyCondition || Atlas.CoverageBuffer == VK_NULL_HANDLE || CommandBuffer == VK_NULL_HANDLE)
        return 0;

    // 🔴 EXACTLY THE PAGES S6 CLEARS, page by page — NEVER the whole buffer. Coverage describes the CONTENT of a page, so it has the atlas image's
    //    lifetime and not the image's: a page rasterized in image N and merely cached in N+1 still holds that depth, S7 skips it entirely (the census
    //    reports nothing to render, which is the cache working), and a whole-buffer fill would therefore zero a bit nothing is left to re-raise. Every
    //    cached page would read as never-drawn, the walk would fall out of all six levels, and the scene would come out FULLY LIT — a worse version of
    //    the bug this buffer replaces. Tying the reset to ShadowPageNeedsRender keeps the two facts in step by construction: the page whose depth is
    //    about to be discarded is the page whose coverage is about to be discarded.
    uint32_t       ClearedCount = 0;
    const uint32_t PageTotal    = (uint32_t)Atlas.PageRecords.size();
    for (uint32_t Page = 0; Page < PageTotal; ++Page)
    {
        if (!ShadowPageNeedsRender(Atlas, Page))
            continue;

        vkCmdFillBuffer(CommandBuffer, Atlas.CoverageBuffer, (VkDeviceSize)Page * sizeof(uint32_t), sizeof(uint32_t), 0u);
        ++ClearedCount;
    }

    // 📝 Nothing to order when nothing was reset, and on a static scene that is the steady state — the same early-out S6 takes for the same reason.
    if (ClearedCount == 0)
        return 0;

    // 🔴 The fills must land before S7's first fragment atomicOr, and the destination access is SHADER_WRITE, not SHADER_READ. A read-only barrier here
    //    compiles, validates, and lets a fill race the atomics — coverage bits raised by early caster fragments would be zeroed underneath them, and the
    //    resulting misses would look exactly like the probe bug this replaces.
    VkBufferMemoryBarrier CoverageBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    CoverageBarrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    CoverageBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    CoverageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    CoverageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    CoverageBarrier.buffer              = Atlas.CoverageBuffer;
    CoverageBarrier.offset              = 0;
    CoverageBarrier.size                = (VkDeviceSize)ShadowPageCapacity * sizeof(uint32_t);

    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 1, &CoverageBarrier, 0, nullptr);

    return ClearedCount;
}

void BarrierShadowPageCoverageForRead(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer)
{
    if (!Atlas.ReadyCondition || Atlas.CoverageBuffer == VK_NULL_HANDLE || CommandBuffer == VK_NULL_HANDLE)
        return;

    // S7's atomicOr -> the shading pass's read. Same edge the atlas image transition covers, but a buffer needs its own barrier: the image transition
    // says nothing about this allocation.
    VkBufferMemoryBarrier CoverageBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    CoverageBarrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    CoverageBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    CoverageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    CoverageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    CoverageBarrier.buffer              = Atlas.CoverageBuffer;
    CoverageBarrier.offset              = 0;
    CoverageBarrier.size                = (VkDeviceSize)ShadowPageCapacity * sizeof(uint32_t);

    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 1, &CoverageBarrier, 0, nullptr);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          CENSUS
//------------------------------------------------------------------------------------------------------------------------

void RefreshShadowPageCensus(ShadowPageAtlas& Atlas)
{
    uint32_t UsedCount   = 0;
    uint32_t CachedCount = 0;
    uint32_t FreeCount   = 0;
    uint32_t StaleCount  = 0;
    uint32_t RenderCount = 0;

    for (const ShadowPageRecord& Record : Atlas.PageRecords)
    {
        switch (Record.Ownership)
        {
            case ShadowPageOwnership::Used:   ++UsedCount;   break;
            case ShadowPageOwnership::Cached: ++CachedCount; break;
            default:                          ++FreeCount;   break;
        }

        // Staleness is only meaningful for a page that exists — a Free page's flag describes nothing.
        if (Record.Ownership == ShadowPageOwnership::Free)
            continue;

        if (Record.ContentStale)
            ++StaleCount;
        if (Record.Ownership == ShadowPageOwnership::Used && Record.ContentStale)
            ++RenderCount;
    }

    Atlas.Census.PageUsedCount   = UsedCount;
    Atlas.Census.PageCachedCount = CachedCount;
    Atlas.Census.PageFreeCount   = FreeCount;
    Atlas.Census.PageStaleCount  = StaleCount;
    Atlas.Census.PageRenderCount = RenderCount;
}

bool ShadowPageOverSubscribed(const ShadowPageAtlas& Atlas)
{
    // 📝 Starvation, not "used == capacity". A full pool that served every request is healthy; the failure is a request that got NOTHING, because
    //    that is the case where a shadow block is missing from the image.
    return Atlas.Census.PageStarvedCount > 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         TRANSITIONS
//------------------------------------------------------------------------------------------------------------------------

void TransitionShadowPageAtlas(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer, VkImageLayout TargetLayout)
{
    if (!Atlas.ReadyCondition || CommandBuffer == VK_NULL_HANDLE || Atlas.CurrentLayout == TargetLayout)
        return;

    VkImageMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    Barrier.oldLayout                       = Atlas.CurrentLayout;
    Barrier.newLayout                       = TargetLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = Atlas.AtlasImage;
    Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.levelCount     = 1;
    Barrier.subresourceRange.layerCount     = 1;

    const bool ToWrite = (TargetLayout == VK_IMAGE_LAYOUT_GENERAL);
    Barrier.srcAccessMask = ToWrite ? VK_ACCESS_SHADER_READ_BIT  : VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask = ToWrite ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;

    // The atlas is written from BOTH compute (S6 clear) and fragment (S7 raster via imageAtomicMin), so the write side of the barrier must cover
    // both stages — scoping it to compute alone would leave the raster's writes unsynchronized against the tracer's reads.
    const VkPipelineStageFlags WriteStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    const VkPipelineStageFlags SourceStage = (Atlas.CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                           ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                           : (ToWrite ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : WriteStages);
    const VkPipelineStageFlags TargetStage = ToWrite ? WriteStages : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    if (Atlas.CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        Barrier.srcAccessMask = 0;

    vkCmdPipelineBarrier(CommandBuffer, SourceStage, TargetStage, 0, 0, nullptr, 0, nullptr, 1, &Barrier);
    Atlas.CurrentLayout = TargetLayout;
}

} // namespace Frontier
