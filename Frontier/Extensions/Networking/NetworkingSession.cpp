/*==========================================================================================================================================
                                                            NETWORKINGSESSION.CPP
==========================================================================================================================================*/
// 🧩 The session advance: replicate-or-absorb around the transport exchange, silent while disabled.

#include "Frontier/Extensions/Networking/NetworkingSession.h"

#include "Frontier/Engine/Scene/WorldSpace.h"

namespace Frontier
{

const char* TransportCategoryName(TransportCategory Category) noexcept
{
    switch (Category)
    {
        case TransportCategory::Loopback: return "Loopback";
        case TransportCategory::Eos:      return "EOS";
        case TransportCategory::Photon:   return "Photon";
    }
    return "Unknown";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    NETWORKING SESSION
//------------------------------------------------------------------------------------------------------------------------

NetworkingSession::NetworkingSession()
{
    Owned = std::make_unique<LoopbackTransport>();
    Space = std::make_unique<NetworkingSpace>();
    RebuildExtension();
}

void NetworkingSession::RebuildExtension()
{
    Extension = std::make_unique<NetworkingExtension>(*Space, QueryTransportRef());
}

uint32_t NetworkingSession::ConfigureLocalPeer(std::string Name, NetworkRole Role)
{
    return Space->ConfigureLocalPeer(std::move(Name), Role);
}

void NetworkingSession::Greet()
{
    Extension->Greet();
}

bool NetworkingSession::QueryTransportAvailable(TransportCategory Category) noexcept
{
    switch (Category)
    {
        case TransportCategory::Loopback: return true;
        case TransportCategory::Eos:      return EosAdapter::QuerySupported();
        case TransportCategory::Photon:   return PhotonAdapter::QuerySupported();
    }
    return false;
}

bool NetworkingSession::SelectLoopback()
{
    Borrowed = nullptr;
    Owned    = std::make_unique<LoopbackTransport>();
    Selected = TransportCategory::Loopback;
    RebuildExtension();
    return true;
}

bool NetworkingSession::SelectEos(const EosRelayConfiguration& Configuration)
{
    if (!EosAdapter::QuerySupported()) return false;
    auto Adapter = std::make_unique<EosAdapter>();
    if (!Adapter->Configure(Configuration)) return false;
    Borrowed = nullptr;
    Owned    = std::move(Adapter);
    Selected = TransportCategory::Eos;
    RebuildExtension();
    return true;
}

bool NetworkingSession::SelectPhoton(const PhotonLinkConfiguration& Configuration)
{
    if (!PhotonAdapter::QuerySupported()) return false;
    auto Adapter = std::make_unique<PhotonAdapter>();
    if (!Adapter->Configure(Configuration)) return false;
    Borrowed = nullptr;
    Owned    = std::move(Adapter);
    Selected = TransportCategory::Photon;
    RebuildExtension();
    return true;
}

void NetworkingSession::AttachTransport(TransportCategory Category, NetworkTransport& Transport_)
{
    Borrowed = &Transport_;
    Selected = Category;
    RebuildExtension();
}

LoopbackTransport* NetworkingSession::QueryLoopback() noexcept
{
    if (Borrowed != nullptr || Selected != TransportCategory::Loopback) return nullptr;
    return static_cast<LoopbackTransport*>(Owned.get());
}

size_t NetworkingSession::Advance(WorldSpace& World)
{
    if (!Enabled) return 0;
    size_t Moved = 0;
    if (Space->QueryHosting()) Moved += Extension->Replicate(World);
    Extension->Exchange();
    if (!Space->QueryHosting()) Moved += Extension->Absorb(World);
    return Moved;
}

std::string NetworkingSession::QueryReading() const
{
    const NetworkPeer* Local = Space->QueryLocalPeer();
    char               Line[256] = {};
    std::snprintf(Line, sizeof(Line), "Networking: %s · %s · %s · %zu peers · sent %llu / received %llu",
                  Enabled ? "Enabled" : "Disabled", TransportCategoryName(Selected),
                  Space->QueryHosting() ? "host" : (Local != nullptr ? "client" : "unconfigured"), Space->QueryPeers().size(),
                  static_cast<unsigned long long>(Space->QueryTally().MessagesSent),
                  static_cast<unsigned long long>(Space->QueryTally().MessagesReceived));
    return Line;
}

} // namespace Frontier
