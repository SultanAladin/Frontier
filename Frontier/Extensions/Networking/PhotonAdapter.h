/*==========================================================================================================================================
                                                              PHOTONADAPTER.H
==========================================================================================================================================*/
// 🧩 A NetworkTransport over Photon Realtime room events: engine messages ride as FP1 Snapshot frames in custom events.

#pragma once

#include "Frontier/Extensions/Networking/NetworkingExtension.h"
#include "Frontier/Extensions/Networking/ReplicationCodec.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    LINK CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Plain strings the login side fills (the Epic identity token authenticates the Photon cloud session); accepted but never logged.
struct PhotonLinkConfiguration
{
    char AppId[64]     = {};   // [-] - empty: the built-in Realtime AppId
    char AppVersion[32] = {};  // [-] - empty: the built-in dev version tag
    char Room[128]     = {};   // [-] - empty: the built-in dev room
    char AuthToken[8192] = {}; // [-] - EOS Auth ID token
    char OwnershipToken[8192] = {};   // [-] - Ecom ownership token, or empty while dormant
    void (*Diagnostic)(const char*) = nullptr;   // [-] - redacted states and counts only, never tokens
};

enum class PhotonPhase : uint8_t
{
    Idle,
    Connecting,
    Joining,
    Joined,
    Failed,
    Disconnected,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHOTON ADAPTER
//------------------------------------------------------------------------------------------------------------------------

// 💡 Without the Photon SDK linked (no FRONTIER_PHOTON_SDK) the adapter still compiles and reports honestly: Begin refuses,
//    sends refuse, receives arrive empty. Tokens are accepted but never logged. One implementation serves every workspace.
class PhotonAdapter final : public NetworkTransport
{
  public:
    PhotonAdapter();
    ~PhotonAdapter() override;

    PhotonAdapter(const PhotonAdapter&)            = delete;
    PhotonAdapter& operator=(const PhotonAdapter&) = delete;

    // 📝 True when the Photon SDK backs this build and cloud packets can flow.
    [[nodiscard]] static bool QuerySupported() noexcept;

    // 📝 Stores the link configuration (tokens stay in the adapter, never in diagnostics). Always true; Begin connects.
    bool Configure(const PhotonLinkConfiguration& Configuration);
    // 📝 Connects with Epic authentication and joins (or creates) the room. False when unsupported or already leaving.
    bool Begin();
    // 📝 Services the cloud session; called from Send/Receive and safe to call directly. Pumps the join handshake.
    void Tick();
    // 📝 Leaves the room and disconnects; safe to call twice.
    void End();

    bool                                      Send(const NetworkMessage& Message) override;
    [[nodiscard]] std::vector<NetworkMessage> Receive() override;
    [[nodiscard]] const char*                 QueryName() const noexcept override { return "Photon"; }
    [[nodiscard]] const char*                 QueryReading() const noexcept { return Reading; }
    [[nodiscard]] PhotonPhase                 QueryPhase() const noexcept { return Phase; }

    [[nodiscard]] uint64_t QueryFramesSent() const noexcept     { return FramesSent; }
    [[nodiscard]] uint64_t QueryFramesReceived() const noexcept { return FramesReceived; }
    [[nodiscard]] uint64_t QueryPacketsRefused() const noexcept { return PacketsRefused; }
    [[nodiscard]] bool     QueryFirstPacketSent() const noexcept { return FirstPacketSent; }

    // 📝 The raw channel the diagnostics probes use: one FP1 frame out, decoded FP1 payloads (category + bytes) back.
    //    Valid only while Joined; refused otherwise, exactly like the message path.
    bool SendFrame(const uint8_t* Bytes, size_t Length);
    struct DecodedFrame
    {
        ReplicationCategory      Category{};
        std::vector<uint8_t>     Payload{};
    };
    [[nodiscard]] std::vector<DecodedFrame> TakeFrames();

    // 📝 The cloud listener's intake: decodes one event payload and queues it. Public for the listener alone.
    void AcceptPhotonEvent(int Slot, const uint8_t* Bytes, size_t Length);

  private:
    friend struct PhotonListener;   // [-] - the cloud listener in the companion unit reaches the handshake below
#if defined(FRONTIER_PHOTON_SDK)
    void FailConnect(int Cause, const char* What);
    void SendFirstPacket();
#endif

    PhotonLinkConfiguration Configuration{};
    void*                   Session          = nullptr;   // [-] - the Photon client, opaque without the SDK
    void*                   Listener         = nullptr;   // [-] - the Photon listener, opaque without the SDK
    PhotonPhase             Phase            = PhotonPhase::Idle;
    std::deque<DecodedFrame> Incoming{};                 // [-] - decoded FP1 payloads awaiting TakeFrames/Receive
    uint64_t                FramesSent       = 0;
    uint64_t                FramesReceived   = 0;
    uint64_t                PacketsRefused   = 0;
    bool                    FirstPacketSent  = false;     // [-] - the join probe left the room
    int                     LastCause        = 0;         // [-] - numeric Photon disconnect cause
    uint64_t                ProgressAt       = 0;         // [ns] - handshake progress on the steady clock
    char                    PendingRoom[128] = {};        // [-] - room the join handshake targets
    char                    Reading[256]     = "Photon: idle";
};

} // namespace Frontier
