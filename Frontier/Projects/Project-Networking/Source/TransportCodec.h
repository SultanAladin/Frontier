//============================================================================================================================================
//                                                          TRANSPORTODEC.H
//============================================================================================================================================
// 📦 Project-facing alias over the engine wire codec (Frontier/Extensions/Networking/ReplicationCodec):
// one FP1 implementation serves every workspace. Wire format unchanged: 'F' 'P' '1' | version u8 |
// kind u8 | length u16BE | payload[length]. Bounds-checked both ways; oversized or truncated input
// is refused, never read past.

#pragma once
#include "Frontier/Extensions/Networking/ReplicationCodec.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Networking
{
using ReplicationKind = Frontier::ReplicationCategory;

struct PacketCodec
{
    static constexpr std::uint8_t WireVersion = Frontier::ReplicationCodec::WireVersion;
    static constexpr std::size_t HeaderLength = Frontier::ReplicationCodec::FrameHeaderLength;
    static constexpr std::size_t MaxPayload = Frontier::ReplicationCodec::MaxPayload;
    static constexpr std::size_t MaxPacket = Frontier::ReplicationCodec::MaxPacket;

    static bool ValidKind(std::uint8_t Kind) noexcept
    {
        return Frontier::ReplicationCodec::ValidateCategory(Kind);
    }

    // Encodes into Out (replaced). Returns false when the payload is too large.
    static bool Encode(ReplicationKind Kind, const std::uint8_t* Payload, std::size_t Length,
        std::vector<std::uint8_t>& Out)
    {
        return Frontier::ReplicationCodec::EncodeFrame(Kind, Payload, Length, Out);
    }

    // Decodes in place: Kind + Payload/Length point into the caller's buffer.
    // Returns false on bad magic, version, kind, or length mismatch.
    static bool Decode(const std::uint8_t* Bytes, std::size_t Length, ReplicationKind& Kind,
        const std::uint8_t*& Payload, std::size_t& PayloadLength) noexcept
    {
        return Frontier::ReplicationCodec::DecodeFrame(Bytes, Length, Kind, Payload, PayloadLength);
    }
};
}
