//============================================================================================================================================
//                                                            EOSTRANSPORT.CPP
//============================================================================================================================================
// 📦 Project-owned EOS relay over the engine EosAdapter: engine messages ride as FP1 Snapshot
// frames (fragmented to the P2P ceiling) between the lobby's product users. The game session
// borrows the one relay adapter and refreshes its peer table as greetings admit peers.

#include "EosTransport.h"
#include "Frontier/Extensions/Networking/NetworkingSession.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Networking
{
// 📝 Declared here (not via RoomRuntime.h) so this unit compiles without the EOS SDK staged;
// defined in LobbyRuntime.cpp where the SDK is present.
bool FindLobbyMemberUser(const char* ProductUser, void*& OutUser) noexcept;
namespace
{
DiagnosticReception Reception = nullptr;
Frontier::EosAdapter Relay;
bool Active = false;
std::vector<std::string> ConfiguredIdentities;
void* ConfiguredPlatform = nullptr;
void* ConfiguredLocal = nullptr;

void Emit(const char* Text) noexcept
{
    if (Reception)
        Reception(Text);
}
}

bool StartEosTransport(DiagnosticReception ActiveReception) noexcept
{
    Reception = ActiveReception;
    Frontier::EosRelayConfiguration Configuration;
    Configuration.Platform = InspectEpicPlatformHandle();
    Configuration.LocalUser = InspectEpicProductUser();
    Configuration.Diagnostic = Reception;
    if (!Relay.Configure(Configuration))
    {
        Emit("transport=eos relay_configuration_refused");
        return false;
    }
    ConfiguredPlatform = Configuration.Platform;
    ConfiguredLocal = Configuration.LocalUser;
    ConfiguredIdentities.clear();
    Active = true;
    Emit(Relay.QueryReading());
    return true;
}

void TickEosTransport() noexcept
{
    // 📝 The relay polls the P2P queue on Receive; nothing to pump here.
}

void StopEosTransport() noexcept
{
    Active = false;
    ConfiguredPlatform = nullptr;
    ConfiguredLocal = nullptr;
    ConfiguredIdentities.clear();
    Reception = nullptr;
}

EosStatus InspectEosStatus() noexcept
{
    EosStatus Status;
    Status.Active = Active;
    std::snprintf(Status.Reading, sizeof(Status.Reading), "%.255s", Relay.QueryReading());
    return Status;
}

Frontier::EosAdapter& QueryEosRelay() noexcept { return Relay; }

void RefreshEosRelayPeers(Frontier::NetworkingSession& Session) noexcept
{
    if (!Active)
        return;
    const Frontier::NetworkPeer* Local = Session.QuerySpace().QueryLocalPeer();
    const std::uint32_t LocalId = Local ? Local->Identifier : 0;
    std::vector<std::pair<std::uint32_t, std::string>> Remotes;
    for (const Frontier::NetworkPeer& Peer : Session.QuerySpace().QueryPeers())
    {
        if (!Peer.Connected || Peer.Identifier == LocalId)
            continue;
        const std::string* Identity = Session.QuerySpace().QueryPeerIdentity(Peer.Identifier);
        if (!Identity || Identity->empty())
            continue;
        Remotes.emplace_back(Peer.Identifier, *Identity);
    }
    std::vector<std::string> Identities;
    Identities.reserve(Remotes.size());
    for (const auto& Remote : Remotes)
        Identities.push_back(Remote.second);
    void* Platform = InspectEpicPlatformHandle();
    void* ProductUser = InspectEpicProductUser();
    if (Platform == ConfiguredPlatform && ProductUser == ConfiguredLocal && Identities == ConfiguredIdentities)
        return;
    Frontier::EosRelayConfiguration Configuration;
    Configuration.Platform = Platform;
    Configuration.LocalUser = ProductUser;
    Configuration.Diagnostic = Reception;
    for (const auto& Remote : Remotes)
    {
        void* User = nullptr;
        if (!FindLobbyMemberUser(Remote.second.c_str(), User) || !User)
            continue;
        Frontier::EosRelayPeer Entry;
        Entry.Peer = Remote.first;
        Entry.User = User;
        Configuration.Peers.push_back(Entry);
    }
    if (!Relay.Configure(Configuration))
    {
        Emit("transport=eos relay_peer_refresh_refused");
        return;
    }
    ConfiguredPlatform = Platform;
    ConfiguredLocal = ProductUser;
    ConfiguredIdentities = std::move(Identities);
    Emit(Relay.QueryReading());
}
}
