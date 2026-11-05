/*==========================================================================================================================================
                                                             REPLICATIONCODEC.H
==========================================================================================================================================*/
// 🧩 The multiplayer wire codec: FP1 frames (Probe, Snapshot, Input) and their fragmentation into small-transport fragments.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WIRE CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 What a frame carries: a handshake probe, one engine message (Snapshot: engine-defined bytes), a future input stream.
enum class ReplicationCategory : uint8_t
{
    Probe    = 0,
    Snapshot = 1,
    Input    = 2,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      FRAME CODEC
//------------------------------------------------------------------------------------------------------------------------

// 📝 Wire form: 'F' 'P' '1' | version u8 | category u8 | length u16BE | payload[length]. Bounds-checked both ways;
//    oversized or truncated input is refused, never read past.
struct ReplicationCodec
{
    static constexpr uint8_t WireVersion        = 1;
    static constexpr size_t  FrameHeaderLength  = 7;
    static constexpr size_t  MaxPayload         = 65535;                    // [B] - one engine message; u16BE length bound
    static constexpr size_t  MaxPacket          = FrameHeaderLength + MaxPayload;

    static bool ValidateCategory(uint8_t Category) noexcept;

    // 📝 Encodes into Out (replaced). False when the payload is too large.
    static bool EncodeFrame(ReplicationCategory Category, const uint8_t* Payload, size_t Length, std::vector<uint8_t>& Out);
    // 📝 Decodes in place: Category + Payload/Length point into the caller's buffer. False on bad magic, version,
    //    category, or length mismatch.
    static bool DecodeFrame(const uint8_t* Bytes, size_t Length, ReplicationCategory& Category, const uint8_t*& Payload,
                            size_t& PayloadLength) noexcept;

    // 📝 Fragment wire form: 'F' 'P' 'F' | version u8 | category u8 | message u16BE | index u16BE | count u16BE |
    //    total u16BE | chunk. Total is the full engine-packet length; every chunk but possibly the last fills
    //    FragmentPayload. Small-MTU transports (EOS peer packets) carry fragments; large ones carry whole frames.
    static constexpr size_t FragmentHeaderLength = 14;
    static constexpr size_t FragmentPayload      = 1024;                   // [B] - under the EOS peer-packet ceiling

    // 📝 Splits one engine packet into MTU fragments (replaced into Out, at least one). False when oversized or empty-handed.
    static bool SplitPacket(ReplicationCategory Category, const uint8_t* Payload, size_t Length, uint16_t Message,
                            std::vector<std::vector<uint8_t>>& Out);
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAGMENT ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

// 💡 Fragments arrive per sender (a peer identifier the transport supplies); a (sender, message) pair names one packet.
//    Redelivered fragments are absorbed once; at most IncompleteCeiling packets assemble at once and the oldest yields.
class FragmentAssembly
{
  public:
    static constexpr size_t IncompleteCeiling = 32;   // [-] - packets assembling at once, at most

    // 📝 Absorbs one fragment. True when accepted: OutPacket carries the completed engine packet (and OutCategory its
    //    category) exactly when the last missing fragment lands, and stays empty while incomplete. False on malformed input.
    bool AbsorbFragment(uint32_t Sender, const uint8_t* Bytes, size_t Length, ReplicationCategory& OutCategory,
                        std::vector<uint8_t>& OutPacket);
    void Reset();

    [[nodiscard]] size_t QueryIncomplete() const noexcept { return Incomplete.size(); }

  private:
    struct IncompletePacket
    {
        ReplicationCategory              Category{};
        uint16_t                         Count    = 0;   // [-]
        uint32_t                         Total    = 0;   // [B]
        uint16_t                         Received = 0;   // [-]
        uint64_t                         Sequence = 0;   // [-] - arrival order for eviction
        std::vector<std::vector<uint8_t>> Chunks{};
    };

    std::map<std::pair<uint32_t, uint16_t>, IncompletePacket> Incomplete{};
    uint64_t                                                 NextSequence = 1;
};

} // namespace Frontier
