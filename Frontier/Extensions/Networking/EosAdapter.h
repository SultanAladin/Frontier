/*==========================================================================================================================================
                                                                EOSADAPTER.H
==========================================================================================================================================*/
// 🧩 A NetworkTransport over EOS peer-to-peer packets: engine messages ride as FP1 Snapshot frames, fragmented to the P2P ceiling.

#pragma once

#include "Frontier/Extensions/Networking/NetworkingExtension.h"
#include "Frontier/Extensions/Networking/ReplicationCodec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    RELAY CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 One addressed peer: the engine peer identifier with its EOS user handle behind void* (this header never includes
//    the EOS SDK; the project side, which owns the login and the lobby, fills these from its EOS session).
struct EosRelayPeer
{
    uint32_t Peer = 0;         // [-] - engine peer identifier (NetworkPeer.Identifier)
    void*    User = nullptr;   // [-] - EOS_ProductUserId, opaque here
};

struct EosRelayConfiguration
{
    void*                   Platform  = nullptr;   // [-] - EOS_HPlatform, opaque here
    void*                   LocalUser = nullptr;   // [-] - EOS_ProductUserId, opaque here
    std::vector<EosRelayPeer> Peers{};             // [-] - addressed remotes; empty: nothing leaves
    char                    SocketName[33] = "FrontierReplication";
    void (*Diagnostic)(const char*) = nullptr;     // [-] - redacted states and counts only, never tokens
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      EOS ADAPTER
//------------------------------------------------------------------------------------------------------------------------

// 💡 Without the EOS SDK linked (no FRONTIER_EOS_SDK) the adapter still compiles and reports honestly: sends refuse,
//    receives arrive empty, and the reading names the missing SDK. One implementation serves every workspace.
class EosAdapter final : public NetworkTransport
{
  public:
    // 📝 True when the EOS SDK backs this build and relay packets can flow.
    [[nodiscard]] static bool QuerySupported() noexcept;

    // 📝 Stores the relay configuration (handles stay opaque until the SDK calls). False when the socket name is empty.
    bool Configure(const EosRelayConfiguration& Configuration);

    bool                                      Send(const NetworkMessage& Message) override;
    [[nodiscard]] std::vector<NetworkMessage> Receive() override;
    [[nodiscard]] const char*                 QueryName() const noexcept override { return "EOS"; }
    [[nodiscard]] const char*                 QueryReading() const noexcept { return Reading; }

    [[nodiscard]] uint64_t QueryFragmentsSent() const noexcept     { return FragmentsSent; }
    [[nodiscard]] uint64_t QueryFragmentsReceived() const noexcept { return FragmentsReceived; }
    [[nodiscard]] uint64_t QueryPacketsRefused() const noexcept    { return PacketsRefused; }

  private:
    EosRelayConfiguration Configuration{};
    FragmentAssembly      Assembly{};
    uint16_t              NextMessage      = 1;   // [-] - rolling fragment message identifier
    uint64_t              FragmentsSent    = 0;
    uint64_t              FragmentsReceived = 0;
    uint64_t              PacketsRefused   = 0;
    char                  Reading[256]     = "EOS: idle";
};

} // namespace Frontier
