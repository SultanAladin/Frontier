/*==============================================================================================================================================
                                                           SHADOWTILESTORE.CPP
==============================================================================================================================================*/
// The virtual tile table behind the sun shadows: the per-tile bit words, the per-image demand reset, and the CPU mirrors of the S1/S2/S3 marking
// chain. See the header for the two-bit ruling, the atomic-or encoding constraint, and the propagation direction.

#include "Graphics/Shadow/ShadowTileStore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace Frontier
{

namespace
{

// 📝 First memory type allowed by the requirement bitmask carrying every required property bit — mirrors ShadowPageAtlas / HierarchicalDepthPyramid.
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

// 📝 One buffer + its backing allocation. Returns false with both handles null on any failure, so the caller's cleanup path is uniform.
bool ProvisionBuffer(VulkanHost&           Host,
                     VkDeviceSize          ByteSize,
                     VkBufferUsageFlags    Usage,
                     VkMemoryPropertyFlags MemoryProperties,
                     VkBuffer&             OutBuffer,
                     VkDeviceMemory&       OutMemory)
{
    VkBufferCreateInfo BufferInfo = {};
    BufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufferInfo.size        = ByteSize;
    BufferInfo.usage       = Usage;
    BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(Host.Device, &BufferInfo, nullptr, &OutBuffer) != VK_SUCCESS)
    {
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements Requirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &Requirements);

    bool           TypeFound = false;
    const uint32_t TypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, Requirements.memoryTypeBits, MemoryProperties, TypeFound);
    if (!TypeFound)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, nullptr);
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInfo = {};
    AllocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocateInfo.allocationSize  = Requirements.size;
    AllocateInfo.memoryTypeIndex = TypeIndex;

    if (vkAllocateMemory(Host.Device, &AllocateInfo, nullptr, &OutMemory) != VK_SUCCESS)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, nullptr);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(Host.Device, OutMemory, nullptr);
        vkDestroyBuffer(Host.Device, OutBuffer, nullptr);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

// 📝 The tile at ONE COARSER level covering a given fine light tile. Each coarser level doubles the world extent per tile, so the covering tile is a
//    floor-halving of the coordinate — and it must be an ARITHMETIC floor, not a truncating divide.
// 🔴 `/ 2` truncates toward zero, so it maps both -1 and 0 onto 0 and every negative tile lands one tile too high. Light tiles are routinely negative
//    (the lattice is centred on the observer, not on the origin), so a truncating divide would mis-propagate the entire half of the window left of and
//    below the origin — a defect that hides completely in any test whose tiles are all positive.
TileCoordinate ResolveCoarserTile(TileCoordinate FineTile)
{
    TileCoordinate Coarse;
    Coarse.XTile = (FineTile.XTile >= 0) ? (FineTile.XTile / 2) : -((-FineTile.XTile + 1) / 2);
    Coarse.YTile = (FineTile.YTile >= 0) ? (FineTile.YTile / 2) : -((-FineTile.YTile + 1) / 2);
    return Coarse;
}

// 📝 Read a whole text file. Used only by the layout validator, which is probe-only.
bool ReadTextFile(const char* Path, std::string& OutText)
{
    if (Path == nullptr)
        return false;

    std::FILE* Handle = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&Handle, Path, "rb") != 0 || Handle == nullptr)
        return false;
#else
    Handle = std::fopen(Path, "rb");
    if (Handle == nullptr)
        return false;
#endif

    std::fseek(Handle, 0, SEEK_END);
    const long ByteCount = std::ftell(Handle);
    std::fseek(Handle, 0, SEEK_SET);

    if (ByteCount <= 0)
    {
        std::fclose(Handle);
        return false;
    }

    OutText.resize(static_cast<size_t>(ByteCount));
    const size_t ReadCount = std::fread(&OutText[0], 1, static_cast<size_t>(ByteCount), Handle);
    std::fclose(Handle);

    OutText.resize(ReadCount);
    return ReadCount > 0;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIFETIME
//------------------------------------------------------------------------------------------------------------------------

bool InitializeShadowTileStore(ShadowTileStore& Store, VulkanHost& Host, uint32_t LevelCount, uint32_t TileResolution)
{
    FinalizeShadowTileStore(Store);

    if (Host.Device == VK_NULL_HANDLE || LevelCount == 0 || TileResolution == 0)
        return false;

    Store.Host           = &Host;
    Store.LevelCount     = std::min(LevelCount, ShadowTilemapLodCount);
    Store.TileResolution = TileResolution;

    const size_t       WordCount = static_cast<size_t>(Store.LevelCount) * TileResolution * TileResolution;
    const VkDeviceSize ByteSize  = static_cast<VkDeviceSize>(WordCount) * sizeof(uint32_t);

    // 📝 Every word starts zero: no demand, no staleness, no page. A tile with no page is never sampled, so zero is the honest initial state — unlike
    //    the page atlas, where a fresh page holds the clear identity and must therefore start STALE.
    Store.TileWords.assign(WordCount, 0u);

    if (!ProvisionBuffer(Host,
                         ByteSize,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         Store.TableBuffer,
                         Store.TableMemory))
    {
        FinalizeShadowTileStore(Store);
        return false;
    }

    // ⚠️ HOST_COHERENT as well as HOST_VISIBLE, so neither upload nor readback needs an explicit flush/invalidate. The table is 24 KiB — the coherent
    //    memory type's cost is irrelevant at this size, and the omitted flush is a bug class avoided outright.
    if (!ProvisionBuffer(Host,
                         ByteSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         Store.StagingBuffer,
                         Store.StagingMemory))
    {
        FinalizeShadowTileStore(Store);
        return false;
    }

    if (vkMapMemory(Host.Device, Store.StagingMemory, 0, ByteSize, 0, &Store.StagingMapping) != VK_SUCCESS)
    {
        Store.StagingMapping = nullptr;
        FinalizeShadowTileStore(Store);
        return false;
    }

    std::memset(Store.StagingMapping, 0, static_cast<size_t>(ByteSize));

    // 🔴 The readback ring is allocated SEPARATELY from the upload buffer above, never aliased onto it — see the struct's note. Each slot is a pure copy
    //    DESTINATION (TRANSFER_DST only), which makes the one-way direction a type-level property rather than a convention a later edit can quietly break.
    for (uint32_t Slot = 0; Slot < ShadowTileReadbackSlots; ++Slot)
    {
        if (!ProvisionBuffer(Host,
                             ByteSize,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             Store.ReadbackBuffer[Slot],
                             Store.ReadbackMemory[Slot]))
        {
            FinalizeShadowTileStore(Store);
            return false;
        }

        if (vkMapMemory(Host.Device, Store.ReadbackMemory[Slot], 0, ByteSize, 0, &Store.ReadbackMapping[Slot]) != VK_SUCCESS)
        {
            Store.ReadbackMapping[Slot] = nullptr;
            FinalizeShadowTileStore(Store);
            return false;
        }

        std::memset(Store.ReadbackMapping[Slot], 0, static_cast<size_t>(ByteSize));
    }

    Store.ReadbackCursor = 0;
    Store.ReadbackFilled = 0;
    Store.Tally          = ShadowTileTally{};
    Store.ReadyCondition = true;
    return true;
}

void FinalizeShadowTileStore(ShadowTileStore& Store)
{
    if (Store.Host != nullptr && Store.Host->Device != VK_NULL_HANDLE)
    {
        if (Store.StagingMapping != nullptr)
            vkUnmapMemory(Store.Host->Device, Store.StagingMemory);

        if (Store.StagingBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(Store.Host->Device, Store.StagingBuffer, nullptr);
        if (Store.StagingMemory != VK_NULL_HANDLE)
            vkFreeMemory(Store.Host->Device, Store.StagingMemory, nullptr);
        if (Store.TableBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(Store.Host->Device, Store.TableBuffer, nullptr);
        if (Store.TableMemory != VK_NULL_HANDLE)
            vkFreeMemory(Store.Host->Device, Store.TableMemory, nullptr);

        // ⚠️ Tolerates a PARTIALLY built ring: Initialize calls straight back into here on the first slot that fails to allocate or map, so every handle
        //    is tested individually rather than assuming the loop ran to completion.
        for (uint32_t Slot = 0; Slot < ShadowTileReadbackSlots; ++Slot)
        {
            if (Store.ReadbackMapping[Slot] != nullptr)
                vkUnmapMemory(Store.Host->Device, Store.ReadbackMemory[Slot]);
            if (Store.ReadbackBuffer[Slot] != VK_NULL_HANDLE)
                vkDestroyBuffer(Store.Host->Device, Store.ReadbackBuffer[Slot], nullptr);
            if (Store.ReadbackMemory[Slot] != VK_NULL_HANDLE)
                vkFreeMemory(Store.Host->Device, Store.ReadbackMemory[Slot], nullptr);
        }
    }

    Store.Host           = nullptr;
    Store.TableBuffer    = VK_NULL_HANDLE;
    Store.TableMemory    = VK_NULL_HANDLE;
    Store.StagingBuffer  = VK_NULL_HANDLE;
    Store.StagingMemory  = VK_NULL_HANDLE;
    Store.StagingMapping = nullptr;
    for (uint32_t Slot = 0; Slot < ShadowTileReadbackSlots; ++Slot)
    {
        Store.ReadbackBuffer[Slot]  = VK_NULL_HANDLE;
        Store.ReadbackMemory[Slot]  = VK_NULL_HANDLE;
        Store.ReadbackMapping[Slot] = nullptr;
    }
    Store.ReadbackCursor = 0;
    Store.ReadbackFilled = 0;
    Store.TileWords.clear();
    Store.LevelCount     = 0;
    Store.TileResolution = 0;
    Store.Tally          = ShadowTileTally{};
    Store.ReadyCondition = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

uint32_t ResolveShadowTileWordIndex(const ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    if (Level >= Store.LevelCount || Store.TileResolution == 0)
        return UINT32_MAX;

    // 📝 Addressed through the CLIPMAP's wrap for the same reason ResolveShadowTileSlot is: the window scrolls, so a bare modulo of the light tile
    //    would disagree with both the GPU and the page atlas about which slot a tile occupies.
    const uint32_t WithinLevel = ResolveSunShadowTileIndex(Clipmap, Level, LightTile);
    return Level * Store.TileResolution * Store.TileResolution + WithinLevel;
}

uint32_t ResolveShadowTileWord(const ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    const uint32_t WordIndex = ResolveShadowTileWordIndex(Store, Clipmap, Level, LightTile);
    if (WordIndex >= Store.TileWords.size())
        return 0u;
    return Store.TileWords[WordIndex];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PER-IMAGE RESET
//------------------------------------------------------------------------------------------------------------------------

void ResetShadowTileDemand(ShadowTileStore& Store)
{
    // 🔴 The mask names what SURVIVES, not what is cleared, and the asymmetry is the correctness argument. Demand (Used / Coarse / Masked) is a claim
    //    about THIS image and must not persist. Update is a claim about the CONTENT of a page and must survive every image boundary until a render
    //    clears it — clearing it here would let a moved caster's tile read clean next image with nothing having redrawn it, which is exactly the
    //    defect ContentStale exists to prevent, reintroduced one level up.
    constexpr uint32_t SurvivingBits = ShadowTileUpdateBit | ShadowTilePageMask;

    for (uint32_t& Word : Store.TileWords)
        Word &= SurvivingBits;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          S1 / S2 / S3  (CPU mirrors)
//------------------------------------------------------------------------------------------------------------------------

void MarkShadowTileUsed(ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    const uint32_t WordIndex = ResolveShadowTileWordIndex(Store, Clipmap, Level, LightTile);
    if (WordIndex >= Store.TileWords.size())
        return;

    // 📝 An OR, matching the GPU's atomicOr exactly. Marking is idempotent by construction, so two receivers landing in one tile cost one bit each.
    // 🔴 Raises Direct as well as Used, because this is a RECEIVER asking. Propagation raises Used without Direct, and masking relies on exactly that
    //    distinction to avoid dropping demand something is actually reading.
    Store.TileWords[WordIndex] |= (ShadowTileUsedBit | ShadowTileDirectBit);
}

void MarkShadowTileUpdate(ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    const uint32_t WordIndex = ResolveShadowTileWordIndex(Store, Clipmap, Level, LightTile);
    if (WordIndex >= Store.TileWords.size())
        return;

    Store.TileWords[WordIndex] |= ShadowTileUpdateBit;
}

void PropagateShadowTileDemand(ShadowTileStore& Store, const SunShadowClipmap& Clipmap)
{
    if (Store.LevelCount < 2 || Store.TileResolution == 0)
        return;

    const int32_t Resolution = static_cast<int32_t>(Store.TileResolution);

    // 🔴 FINE -> COARSE, level by level in ascending order, and the ORDER matters as much as the direction: level N+1 is built from level N's demand
    //    INCLUDING what level N inherited from N-1, so a single ascending sweep carries LOD 0's demand all the way to LOD 5. A descending sweep, or
    //    one that read only directly-marked bits, would leave the coarsest levels backing nothing and the tracer's fallback would sample unmapped
    //    pages. This is why the loop reads Used (either origin) rather than only the S1 bit.
    for (uint32_t FineLevel = 0; FineLevel + 1 < Store.LevelCount; ++FineLevel)
    {
        const uint32_t CoarseLevel = FineLevel + 1;

        // ⚠️ Iterated over the level's PHYSICAL slots, then converted back to a light tile through the level's own origin. Walking light tiles
        //    directly would need a scan range that depends on where the window happens to sit; the physical grid is always exactly Resolution².
        //
        // 🔴 THE CONVERSION IS `Slot - Origin`, NOT `Origin + Slot`, because ToroidalOrigin is stored in ADD form: the wrap is
        //    `physical = wrap(LightTile + Origin)` (SunShadowClipmap.cpp:123), so the resident light tiles are `Slot - Origin`. Getting this
        //    backwards is INVISIBLE to any addressing or bijection test — `Origin + Slot` still enumerates all 32 physical slots exactly once, so
        //    the table looks perfectly addressed. It simply names the WRONG 32 LIGHT TILES (here [16..47] instead of [-16..15]), and the halved
        //    coarse tiles of that wrong set fall outside the coarse window, so propagation reaches nothing. Measured: 0/32 coverage with the
        //    wrong form, 32/32 with this one.
        const TileCoordinate FineOrigin = Clipmap.Levels[FineLevel].ToroidalOrigin;

        for (int32_t SlotY = 0; SlotY < Resolution; ++SlotY)
        for (int32_t SlotX = 0; SlotX < Resolution; ++SlotX)
        {
            TileCoordinate FineTile;
            FineTile.XTile = SlotX - FineOrigin.XTile;
            FineTile.YTile = SlotY - FineOrigin.YTile;

            const uint32_t FineWordIndex = ResolveShadowTileWordIndex(Store, Clipmap, FineLevel, FineTile);
            if (FineWordIndex >= Store.TileWords.size())
                continue;

            if ((Store.TileWords[FineWordIndex] & ShadowTileUsedBit) == 0)
                continue;

            const TileCoordinate CoarseTile     = ResolveCoarserTile(FineTile);
            const uint32_t       CoarseWordIndex = ResolveShadowTileWordIndex(Store, Clipmap, CoarseLevel, CoarseTile);
            if (CoarseWordIndex >= Store.TileWords.size())
                continue;

            // 📝 Coarse marks demand AND records that the demand is inherited, so masking can later distinguish it from a receiver that genuinely
            //    samples at this LOD. Both bits raised with one OR — the GPU pass does the same in a single atomicOr.
            Store.TileWords[CoarseWordIndex] |= (ShadowTileUsedBit | ShadowTileCoarseBit);
        }
    }
}

uint32_t MaskRedundantShadowTiles(ShadowTileStore& Store, const SunShadowClipmap& Clipmap)
{
    if (Store.LevelCount < 2 || Store.TileResolution == 0)
        return 0;

    uint32_t      MaskedCount = 0;
    const int32_t Resolution  = static_cast<int32_t>(Store.TileResolution);

    // 🔴 Walked COARSE -> FINE (descending), the opposite of propagation, because masking asks a question about a coarse tile's CHILDREN while
    //    propagation answered one about a fine tile's PARENT. Running masking in the same ascending order would let a level be masked before the
    //    level below it had been, so a chain of inherited demand would be judged against incomplete information.
    for (uint32_t CoarseLevel = Store.LevelCount - 1; CoarseLevel >= 1; --CoarseLevel)
    {
        const uint32_t       FineLevel    = CoarseLevel - 1;
        const TileCoordinate CoarseOrigin = Clipmap.Levels[CoarseLevel].ToroidalOrigin;

        // 🔴 `Slot - Origin`, for the ADD-form reason documented in PropagateShadowTileDemand above.
        for (int32_t SlotY = 0; SlotY < Resolution; ++SlotY)
        for (int32_t SlotX = 0; SlotX < Resolution; ++SlotX)
        {
            TileCoordinate CoarseTile;
            CoarseTile.XTile = SlotX - CoarseOrigin.XTile;
            CoarseTile.YTile = SlotY - CoarseOrigin.YTile;

            const uint32_t CoarseWordIndex = ResolveShadowTileWordIndex(Store, Clipmap, CoarseLevel, CoarseTile);
            if (CoarseWordIndex >= Store.TileWords.size())
                continue;

            const uint32_t CoarseWord = Store.TileWords[CoarseWordIndex];

            // 🔴 Only PURELY INHERITED demand may be dropped: Coarse AND NOT Direct. Testing Coarse alone is not enough — the two bits coexist when a
            //    distant receiver samples a coarse tile itself while nearer geometry also propagates into it, and masking that tile removes a page
            //    something is actively reading. That produces a hole at exactly the distance where coarse LODs serve the image, which reads as a
            //    shadow-distance bug rather than a masking one.
            if ((CoarseWord & ShadowTileUsedBit) == 0 || (CoarseWord & ShadowTileCoarseBit) == 0)
                continue;
            if ((CoarseWord & ShadowTileDirectBit) != 0)
                continue;
            if ((CoarseWord & ShadowTileMaskedBit) != 0)
                continue;

            // 🔴 ALL FOUR children must carry demand that itself survived masking. A partially-covered coarse tile is NOT redundant: the fine level
            //    covers only part of its footprint, and the rest is served by exactly the coarse fallback this tile provides. Masking it would punch
            //    a hole precisely where the fine level stopped — which is also what happens when the fine level runs out of pages, so the bug would
            //    surface as intermittent shadow loss under memory pressure rather than as a masking error.
            bool EveryChildCovered = true;
            for (int32_t ChildY = 0; ChildY < 2 && EveryChildCovered; ++ChildY)
            for (int32_t ChildX = 0; ChildX < 2 && EveryChildCovered; ++ChildX)
            {
                TileCoordinate ChildTile;
                ChildTile.XTile = CoarseTile.XTile * 2 + ChildX;
                ChildTile.YTile = CoarseTile.YTile * 2 + ChildY;

                const uint32_t ChildWordIndex = ResolveShadowTileWordIndex(Store, Clipmap, FineLevel, ChildTile);
                if (ChildWordIndex >= Store.TileWords.size())
                {
                    EveryChildCovered = false;
                    break;
                }

                const uint32_t ChildWord = Store.TileWords[ChildWordIndex];
                const bool     ChildLive = (ChildWord & ShadowTileUsedBit) != 0 && (ChildWord & ShadowTileMaskedBit) == 0;
                if (!ChildLive)
                    EveryChildCovered = false;
            }

            if (EveryChildCovered)
            {
                Store.TileWords[CoarseWordIndex] |= ShadowTileMaskedBit;
                ++MaskedCount;
            }
        }
    }

    return MaskedCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool ShadowTileRequestsPage(const ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    const uint32_t Word = ResolveShadowTileWord(Store, Clipmap, Level, LightTile);
    return (Word & ShadowTileUsedBit) != 0 && (Word & ShadowTileMaskedBit) == 0;
}

bool ShadowTileNeedsRender(const ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    const uint32_t Word = ResolveShadowTileWord(Store, Clipmap, Level, LightTile);

    // 🔴 The conjunction, not either bit alone. Used without Update redraws depth that is already correct (no caching at all); Update without Used
    //    redraws tiles nothing samples. Masked tiles are excluded because a masked tile gets no page, and rendering into a page it does not own would
    //    corrupt whichever tile actually holds it.
    const bool Wanted = (Word & ShadowTileUsedBit) != 0 && (Word & ShadowTileMaskedBit) == 0;
    return Wanted && (Word & ShadowTileUpdateBit) != 0;
}

void MarkShadowTileRendered(ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile)
{
    const uint32_t WordIndex = ResolveShadowTileWordIndex(Store, Clipmap, Level, LightTile);
    if (WordIndex >= Store.TileWords.size())
        return;

    // 🔴 The ONLY place Update is lowered, and S7 is the only legitimate caller. A clear not backed by a real rasterization leaves a tile claiming
    //    correct depth it does not have — the silent-wrong-shadow bug, which no later pass can detect.
    Store.TileWords[WordIndex] &= ~ShadowTileUpdateBit;
}

void RefreshShadowTileTally(ShadowTileStore& Store)
{
    ShadowTileTally Tally;

    for (const uint32_t Word : Store.TileWords)
    {
        const bool Used   = (Word & ShadowTileUsedBit) != 0;
        const bool Update = (Word & ShadowTileUpdateBit) != 0;
        const bool Masked = (Word & ShadowTileMaskedBit) != 0;

        if (Used)
            ++Tally.TileUsedCount;
        if (Update)
            ++Tally.TileUpdateCount;
        if ((Word & ShadowTileDirectBit) != 0)
            ++Tally.TileDirectCount;
        // 📝 Counts INHERITED-ONLY demand, matching what masking is allowed to consider — a tile carrying both Direct and Coarse is a receiver's own
        //    tile that propagation also reached, and reporting it as inherited would overstate how much masking could ever save.
        if ((Word & ShadowTileCoarseBit) != 0 && (Word & ShadowTileDirectBit) == 0)
            ++Tally.TileCoarseCount;
        if (Masked)
            ++Tally.TileMaskedCount;

        // 📝 The same conjunction ShadowTileNeedsRender applies, so the tally and the dispatch gate can never disagree about the workload.
        if (Used && Update && !Masked)
            ++Tally.TileRenderCount;
    }

    Store.Tally = Tally;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TRANSFER
//------------------------------------------------------------------------------------------------------------------------

void UploadShadowTileStore(ShadowTileStore& Store, VkCommandBuffer CommandBuffer)
{
    if (!Store.ReadyCondition || CommandBuffer == VK_NULL_HANDLE || Store.StagingMapping == nullptr)
        return;

    const VkDeviceSize ByteSize = static_cast<VkDeviceSize>(Store.TileWords.size()) * sizeof(uint32_t);
    std::memcpy(Store.StagingMapping, Store.TileWords.data(), static_cast<size_t>(ByteSize));

    VkBufferCopy CopyRegion = {};
    CopyRegion.size = ByteSize;
    vkCmdCopyBuffer(CommandBuffer, Store.StagingBuffer, Store.TableBuffer, 1, &CopyRegion);

    // ⚠️ The upload must be visible to the marking shaders' atomics, which read AND write the same words. TRANSFER_WRITE -> SHADER_READ|SHADER_WRITE,
    //    because an atomicOr is both.
    VkBufferMemoryBarrier Barrier = {};
    Barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    Barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    Barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.buffer              = Store.TableBuffer;
    Barrier.offset              = 0;
    Barrier.size                = ByteSize;

    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0, nullptr,
                         1, &Barrier,
                         0, nullptr);
}

void DownloadShadowTileStore(ShadowTileStore& Store, VkCommandBuffer CommandBuffer)
{
    if (!Store.ReadyCondition || CommandBuffer == VK_NULL_HANDLE)
        return;

    const VkDeviceSize ByteSize = static_cast<VkDeviceSize>(Store.TileWords.size()) * sizeof(uint32_t);

    // ⚠️ The marking shaders' writes must land before the copy reads them. SHADER_WRITE -> TRANSFER_READ; the fragment stage is included because S2
    //    is a raster pass, not compute.
    VkBufferMemoryBarrier Barrier = {};
    Barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    Barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.buffer              = Store.TableBuffer;
    Barrier.offset              = 0;
    Barrier.size                = ByteSize;

    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         1, &Barrier,
                         0, nullptr);

    // 🔴 Into the RING, never into StagingBuffer. The upload's CPU memcpy would otherwise clobber these bytes before this copy executes.
    VkBufferCopy CopyRegion = {};
    CopyRegion.size = ByteSize;
    vkCmdCopyBuffer(CommandBuffer, Store.TableBuffer, Store.ReadbackBuffer[Store.ReadbackCursor], 1, &CopyRegion);

    Store.ReadbackCursor = (Store.ReadbackCursor + 1u) % ShadowTileReadbackSlots;
    if (Store.ReadbackFilled < ShadowTileReadbackSlots)
        ++Store.ReadbackFilled;
}

void ResolveShadowTileDownload(ShadowTileStore& Store)
{
    if (!Store.ReadyCondition || Store.TileWords.empty())
        return;

    // 📝 The slot about to be overwritten next is the OLDEST, hence the one whose copy has certainly completed — the same argument ObjectPickReadback's
    //    ring rests on. With 3 slots and 2 frames in flight, that slot is 3 images old, so no fence of its own is needed.
    // ⚠️ Reads nothing until the ring has actually been written. Before the first download the slots hold the zero-fill from Initialize, which would
    //    read as a legitimate "no demand" rather than as "no data yet" — and would wipe the mirror's Update bits on the way past.
    if (Store.ReadbackFilled < ShadowTileReadbackSlots)
        return;

    const uint32_t OldestSlot = Store.ReadbackCursor;
    if (Store.ReadbackMapping[OldestSlot] == nullptr)
        return;

    const size_t ByteSize = Store.TileWords.size() * sizeof(uint32_t);
    std::memcpy(Store.TileWords.data(), Store.ReadbackMapping[OldestSlot], ByteSize);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LAYOUT VALIDATION
//------------------------------------------------------------------------------------------------------------------------

bool ValidateShadowTileStoreLayout(const char* ShaderSourcePath, std::vector<const char*>& OutReasons)
{
    OutReasons.clear();

    std::string Source;
    if (!ReadTextFile(ShaderSourcePath, Source))
    {
        OutReasons.push_back("shared shader source could not be read");
        return false;
    }

    // 📝 Each entry is the exact `#define` line the GLSL must contain for the C++ constant it mirrors. Compared as literal text rather than parsed:
    //    a parser would accept an equivalent-but-differently-written value, and the point here is to force the two files to be edited together.
    struct LayoutExpectation
    {
        const char* DefineText;
        const char* FailureReason;
    };

    const LayoutExpectation Expectations[] = {
        { "#define ShadowTileUsedBit    1u",       "ShadowTileUsedBit disagrees with the header"     },
        { "#define ShadowTileDirectBit  2u",       "ShadowTileDirectBit disagrees with the header"   },
        { "#define ShadowTileUpdateBit  4u",       "ShadowTileUpdateBit disagrees with the header"   },
        { "#define ShadowTileCoarseBit  8u",       "ShadowTileCoarseBit disagrees with the header"   },
        { "#define ShadowTileMaskedBit  16u",      "ShadowTileMaskedBit disagrees with the header"   },
        { "#define ShadowTileFlagMask   0xFFu",    "ShadowTileFlagMask disagrees with the header"    },
        { "#define ShadowTilePageShift  8",        "ShadowTilePageShift disagrees with the header"   },
        { "#define ShadowTilemapResolution 32",    "tilemap resolution disagrees with the header"    },
        { "#define ShadowTilemapLodCount   6",     "LOD count disagrees with the header"             },
    };

    // 🔴 Guard the C++ side too. If someone renumbers a constant in the header the literals above become wrong, and a validator that only checked the
    //    GLSL would then happily confirm the shader matches a header it no longer matches.
    if (ShadowTileUsedBit != 1u)                       OutReasons.push_back("header ShadowTileUsedBit is no longer 1");
    if (ShadowTileDirectBit != 2u)                     OutReasons.push_back("header ShadowTileDirectBit is no longer 2");
    if (ShadowTileUpdateBit != 4u)                     OutReasons.push_back("header ShadowTileUpdateBit is no longer 4");
    if (ShadowTileCoarseBit != 8u)                     OutReasons.push_back("header ShadowTileCoarseBit is no longer 8");
    if (ShadowTileMaskedBit != 16u)                    OutReasons.push_back("header ShadowTileMaskedBit is no longer 16");
    if (ShadowTileFlagMask != 0x000000FFu)             OutReasons.push_back("header ShadowTileFlagMask is no longer 0xFF");
    if (ShadowTilePageShift != 8u)                     OutReasons.push_back("header ShadowTilePageShift is no longer 8");
    if (ShadowTilemapResolution != 32u)                OutReasons.push_back("header tilemap resolution is no longer 32");
    if (ShadowTilemapLodCount != 6u)                   OutReasons.push_back("header LOD count is no longer 6");

    // ⚠️ The flag mask and the page mask must partition the word with no overlap: an overlapping bit would let a marking atomicOr corrupt a page index.
    constexpr uint32_t EveryMarkingBit =
        ShadowTileUsedBit | ShadowTileDirectBit | ShadowTileUpdateBit | ShadowTileCoarseBit | ShadowTileMaskedBit;

    if ((ShadowTileFlagMask & ShadowTilePageMask) != 0)
        OutReasons.push_back("flag mask and page mask overlap — marking could corrupt a page index");
    if ((ShadowTileFlagMask | ShadowTilePageMask) != 0xFFFFFFFFu)
        OutReasons.push_back("flag mask and page mask leave a gap in the word");
    if ((ShadowTileFlagMask & EveryMarkingBit) != EveryMarkingBit)
        OutReasons.push_back("a marking bit falls outside the marking-writable mask");

    for (const LayoutExpectation& Expectation : Expectations)
        if (Source.find(Expectation.DefineText) == std::string::npos)
            OutReasons.push_back(Expectation.FailureReason);

    // 🔴 The ADD-form sign, checked as text because it is the whole GPU/CPU addressing contract and no compiler can see it. ToroidalOrigin is the
    //    NEGATED window corner, so the shader must ADD it; subtracting compiles, stays in range, and maps distinct tiles to distinct slots, which
    //    means every structural check still passes while the GPU and CPU disagree about which tile owns which slot.
    if (Source.find("LightTile + ToroidalOrigin") == std::string::npos)
        OutReasons.push_back("shader does not ADD ToroidalOrigin — the ADD-form wrap contract is broken");
    if (Source.find("LightTile - ToroidalOrigin") != std::string::npos)
        OutReasons.push_back("shader SUBTRACTS ToroidalOrigin — disagrees with ResolveSunShadowPhysicalTile");

    return OutReasons.empty();
}

} // namespace Frontier
