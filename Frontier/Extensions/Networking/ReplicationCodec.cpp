/*==========================================================================================================================================
                                                            REPLICATIONCODEC.CPP
==========================================================================================================================================*/
// 🧩 FP1 frame encoding and MTU fragmentation for the multiplayer transports, bounds-checked both ways.

#include "Frontier/Extensions/Networking/ReplicationCodec.h"

#include <cstring>

namespace Frontier
{

bool ReplicationCodec::ValidateCategory(uint8_t Category) noexcept
{
    return Category <= static_cast<uint8_t>(ReplicationCategory::Input);
}

bool ReplicationCodec::EncodeFrame(ReplicationCategory Category, const uint8_t* Payload, size_t Length, std::vector<uint8_t>& Out)
{
    if (Length > MaxPayload || (Length > 0 && Payload == nullptr)) return false;
    Out.resize(FrameHeaderLength + Length);
    Out[0] = 'F';
    Out[1] = 'P';
    Out[2] = '1';
    Out[3] = WireVersion;
    Out[4] = static_cast<uint8_t>(Category);
    Out[5] = static_cast<uint8_t>((Length >> 8) & 0xffu);
    Out[6] = static_cast<uint8_t>(Length & 0xffu);
    if (Length > 0) std::memcpy(Out.data() + FrameHeaderLength, Payload, Length);
    return true;
}

bool ReplicationCodec::DecodeFrame(const uint8_t* Bytes, size_t Length, ReplicationCategory& Category, const uint8_t*& Payload,
                                   size_t& PayloadLength) noexcept
{
    Category      = ReplicationCategory::Probe;
    Payload       = nullptr;
    PayloadLength = 0;
    if (Bytes == nullptr || Length < FrameHeaderLength || Length > MaxPacket) return false;
    if (Bytes[0] != 'F' || Bytes[1] != 'P' || Bytes[2] != '1') return false;
    if (Bytes[3] != WireVersion || !ValidateCategory(Bytes[4])) return false;
    const size_t Declared = (static_cast<size_t>(Bytes[5]) << 8) | Bytes[6];
    if (Declared > MaxPayload || FrameHeaderLength + Declared != Length) return false;
    Category      = static_cast<ReplicationCategory>(Bytes[4]);
    Payload       = Bytes + FrameHeaderLength;
    PayloadLength = Declared;
    return true;
}

bool ReplicationCodec::SplitPacket(ReplicationCategory Category, const uint8_t* Payload, size_t Length, uint16_t Message,
                                   std::vector<std::vector<uint8_t>>& Out)
{
    if (Length == 0 || Length > MaxPayload || Payload == nullptr) return false;
    const size_t Count = (Length + FragmentPayload - 1) / FragmentPayload;
    if (Count > 0xffffu) return false;
    Out.clear();
    Out.reserve(Count);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const size_t         Offset = Index * FragmentPayload;
        const size_t         Extent = Length - Offset > FragmentPayload ? FragmentPayload : Length - Offset;
        std::vector<uint8_t> Fragment(FragmentHeaderLength + Extent);
        Fragment[0]  = 'F';
        Fragment[1]  = 'P';
        Fragment[2]  = 'F';
        Fragment[3]  = WireVersion;
        Fragment[4]  = static_cast<uint8_t>(Category);
        Fragment[5]  = static_cast<uint8_t>((Message >> 8) & 0xffu);
        Fragment[6]  = static_cast<uint8_t>(Message & 0xffu);
        Fragment[7]  = static_cast<uint8_t>((Index >> 8) & 0xffu);
        Fragment[8]  = static_cast<uint8_t>(Index & 0xffu);
        Fragment[9]  = static_cast<uint8_t>((Count >> 8) & 0xffu);
        Fragment[10] = static_cast<uint8_t>(Count & 0xffu);
        Fragment[11] = static_cast<uint8_t>((Length >> 8) & 0xffu);
        Fragment[12] = static_cast<uint8_t>(Length & 0xffu);
        Fragment[13] = 0;
        std::memcpy(Fragment.data() + FragmentHeaderLength, Payload + Offset, Extent);
        Out.push_back(std::move(Fragment));
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAGMENT ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

bool FragmentAssembly::AbsorbFragment(uint32_t Sender, const uint8_t* Bytes, size_t Length, ReplicationCategory& OutCategory,
                                      std::vector<uint8_t>& OutPacket)
{
    OutPacket.clear();
    if (Bytes == nullptr || Length < ReplicationCodec::FragmentHeaderLength) return false;
    if (Bytes[0] != 'F' || Bytes[1] != 'P' || Bytes[2] != 'F') return false;
    if (Bytes[3] != ReplicationCodec::WireVersion || !ReplicationCodec::ValidateCategory(Bytes[4])) return false;
    const uint16_t Message = static_cast<uint16_t>((static_cast<size_t>(Bytes[5]) << 8) | Bytes[6]);
    const uint16_t Index   = static_cast<uint16_t>((static_cast<size_t>(Bytes[7]) << 8) | Bytes[8]);
    const uint16_t Count   = static_cast<uint16_t>((static_cast<size_t>(Bytes[9]) << 8) | Bytes[10]);
    const uint32_t Total   = static_cast<uint32_t>((static_cast<size_t>(Bytes[11]) << 8) | Bytes[12]);
    const size_t   Extent  = Length - ReplicationCodec::FragmentHeaderLength;
    if (Count == 0 || Index >= Count || Total == 0 || Total > ReplicationCodec::MaxPayload) return false;
    if (Extent == 0 || Extent > ReplicationCodec::FragmentPayload) return false;
    // 📝 Every chunk but the last fills the payload; the last fills only what the total leaves.
    const size_t Expect = Index + 1 < Count ? ReplicationCodec::FragmentPayload
                                            : Total - static_cast<size_t>(Count - 1) * ReplicationCodec::FragmentPayload;
    if (Expect == 0 || Expect > ReplicationCodec::FragmentPayload || Extent != Expect) return false;

    const std::pair<uint32_t, uint16_t> Key{ Sender, Message };
    if (Incomplete.find(Key) == Incomplete.end())
    {
        // 📝 A fresh packet: evict the oldest assembly at the ceiling, then claim its slots.
        if (Incomplete.size() >= IncompleteCeiling)
        {
            auto Oldest = Incomplete.begin();
            for (auto Entry = Incomplete.begin(); Entry != Incomplete.end(); ++Entry)
            {
                if (Entry->second.Sequence < Oldest->second.Sequence) Oldest = Entry;
            }
            Incomplete.erase(Oldest);
        }
        IncompletePacket Fresh;
        Fresh.Category = static_cast<ReplicationCategory>(Bytes[4]);
        Fresh.Count    = Count;
        Fresh.Total    = Total;
        Fresh.Sequence = NextSequence++;
        Fresh.Chunks.resize(Count);
        Incomplete.emplace(Key, std::move(Fresh));
    }
    IncompletePacket& Packet = Incomplete[Key];
    if (static_cast<uint8_t>(Packet.Category) != Bytes[4] || Packet.Count != Count || Packet.Total != Total) return false;
    if (!Packet.Chunks[Index].empty()) return true;   // [-] - a redelivery: absorbed once
    Packet.Chunks[Index].assign(Bytes + ReplicationCodec::FragmentHeaderLength, Bytes + Length);
    ++Packet.Received;
    if (Packet.Received < Packet.Count) return true;

    OutCategory = Packet.Category;
    OutPacket.reserve(Packet.Total);
    for (const std::vector<uint8_t>& Chunk : Packet.Chunks) OutPacket.insert(OutPacket.end(), Chunk.begin(), Chunk.end());
    Incomplete.erase(Key);
    return OutPacket.size() == Packet.Total;
}

void FragmentAssembly::Reset()
{
    Incomplete.clear();
}

} // namespace Frontier
