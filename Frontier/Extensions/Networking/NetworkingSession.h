/*==========================================================================================================================================
                                                             NETWORKINGSESSION.H
==========================================================================================================================================*/
// 🧩 One session's replication lifetime: the space, the selected transport and the per-frame advance over the level.

#pragma once

#include "Frontier/Extensions/Networking/NetworkingExtension.h"
#include "Frontier/Extensions/Networking/EosAdapter.h"
#include "Frontier/Extensions/Networking/PhotonAdapter.h"

#include <cstdio>
#include <memory>

namespace Frontier
{

class WorldSpace;

// 📝 The transport a session replicates over: in-process loopback, the EOS peer relay, or the Photon cloud room.
enum class TransportCategory : uint8_t
{
    Loopback,
    Eos,
    Photon,
};

[[nodiscard]] const char* TransportCategoryName(TransportCategory Category) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    NETWORKING SESSION
//------------------------------------------------------------------------------------------------------------------------

// 💡 The host replicates its level into the space, the transport carries the messages, the client absorbs them into its
//    own level — Advance runs that triangle once per frame while Enabled. Disabled sessions stay silent but keep their
//    peers and transport selection. One session serves one level; two sessions pair over loopback for local validation.
class NetworkingSession
{
  public:
    NetworkingSession();

    // 📝 Introduces this side of the session; identifiers start at 1. Greet announces it to the peers.
    uint32_t ConfigureLocalPeer(std::string Name, NetworkRole Role);
    void     Greet();

    // 📝 True when the category's transport can flow in this build (EOS and Photon need their SDKs linked).
    [[nodiscard]] static bool QueryTransportAvailable(TransportCategory Category) noexcept;
    [[nodiscard]] TransportCategory QueryTransport() const noexcept { return Selected; }

    // 📝 Selects the transport, replacing the previous one (its queued messages are dropped). False when unavailable.
    bool SelectLoopback();
    bool SelectEos(const EosRelayConfiguration& Configuration);
    bool SelectPhoton(const PhotonLinkConfiguration& Configuration);
    // 📝 Pairs two sessions' loopback transports for local validation; null when attached or not loopback.
    [[nodiscard]] LoopbackTransport* QueryLoopback() noexcept;

    // ⚠️ Replicates over a transport the session does not own (a provider adapter owned by the login side): the
    //    transport must outlive the session's use, and Select* detaches back to an owned transport.
    void AttachTransport(TransportCategory Category, NetworkTransport& Transport_);
    [[nodiscard]] bool QueryAttached() const noexcept { return Borrowed != nullptr; }

    // 📝 The "Networking" toggle: enabled sessions replicate, disabled ones stay silent. Starts disabled.
    void ConfigureEnabled(bool Enabled_) noexcept { Enabled = Enabled_; }
    [[nodiscard]] bool QueryEnabled() const noexcept { return Enabled; }

    // 📝 Host: replicates the level's changed records, then exchanges. Client: exchanges, then absorbs into the level.
    //    Returns the records replicated plus the messages applied. Silent (0) while disabled.
    size_t Advance(WorldSpace& World);

    // 📝 One status line for the session UI: toggle, transport, role, peers, tallies.
    [[nodiscard]] std::string QueryReading() const;

    [[nodiscard]] NetworkingSpace&         QuerySpace() noexcept { return *Space; }
    [[nodiscard]] const NetworkingSpace&   QuerySpace() const noexcept { return *Space; }
    [[nodiscard]] NetworkingExtension&     QueryExtension() noexcept { return *Extension; }
    [[nodiscard]] NetworkTransport&        QueryTransportRef() noexcept { return Borrowed != nullptr ? *Borrowed : *Owned; }

  private:
    void RebuildExtension();

    std::unique_ptr<NetworkTransport>    Owned{};      // [-] - the selected owned transport, if any
    NetworkTransport*                    Borrowed = nullptr;   // [-] - attached outside transport, if any
    std::unique_ptr<NetworkingSpace>     Space{};
    std::unique_ptr<NetworkingExtension> Extension{};
    TransportCategory                    Selected = TransportCategory::Loopback;
    bool                                 Enabled  = false;
};

} // namespace Frontier
