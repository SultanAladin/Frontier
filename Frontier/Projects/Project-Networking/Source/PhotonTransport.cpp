//============================================================================================================================================
//                                                         PHOTONTRANSPORT.CPP
//============================================================================================================================================
// 📦 Project-owned Photon facade over the engine PhotonAdapter (Frontier/Extensions/Networking):
// Epic-auth connect, join-or-create room, replicated packets. One implementation serves every
// workspace; without the official SDK libs the adapter reports honestly that Photon is unavailable
// and the router keeps players on the EOS path. Diagnostics carry states, counts and numeric
// causes only. Tokens are never logged.

#include "PhotonTransport.h"
#include <cstdio>
#include <cstring>

namespace Networking
{
namespace
{
Frontier::PhotonAdapter Adapter;
DiagnosticReception Reception = nullptr;

PhotonState MapPhase(Frontier::PhotonPhase Phase) noexcept
{
    switch (Phase)
    {
    case Frontier::PhotonPhase::Idle: return PhotonState::Idle;
    case Frontier::PhotonPhase::Connecting: return PhotonState::Connecting;
    case Frontier::PhotonPhase::Joining: return PhotonState::Joining;
    case Frontier::PhotonPhase::Joined: return PhotonState::Joined;
    case Frontier::PhotonPhase::Failed: return PhotonState::Failed;
    case Frontier::PhotonPhase::Disconnected: return PhotonState::Disconnected;
    }
    return PhotonState::Failed;
}

void CopySetting(const char* From, char* To, std::size_t Capacity) noexcept
{
    std::snprintf(To, Capacity, "%s", From ? From : "");
}
}

bool PhotonLinkAvailable() noexcept { return Frontier::PhotonAdapter::QuerySupported(); }

bool StartPhotonTransport(const PhotonCredentials& Credentials, DiagnosticReception ActiveReception) noexcept
{
    Reception = ActiveReception;
    Frontier::PhotonLinkConfiguration Link;
    CopySetting(Credentials.AppId, Link.AppId, sizeof(Link.AppId));
    CopySetting(Credentials.AppVersion, Link.AppVersion, sizeof(Link.AppVersion));
    CopySetting(Credentials.RoomName, Link.Room, sizeof(Link.Room));
    CopySetting(Credentials.IdTokenJwt, Link.AuthToken, sizeof(Link.AuthToken));
    CopySetting(Credentials.OwnershipTokenOrNull, Link.OwnershipToken, sizeof(Link.OwnershipToken));
    Link.Diagnostic = Reception;
    Adapter.Configure(Link);
    return Adapter.Begin();
}

void TickPhotonTransport() noexcept { Adapter.Tick(); }

void StopPhotonTransport() noexcept
{
    Adapter.End();
    Reception = nullptr;
}

bool SendPhotonPacket(const std::uint8_t* Bytes, std::size_t Length, bool /*Reliable*/) noexcept
{
    return Adapter.SendFrame(Bytes, Length);
}

PhotonStatus InspectPhotonStatus() noexcept
{
    PhotonStatus Status;
    if (!Frontier::PhotonAdapter::QuerySupported())
    {
        Status.State = PhotonState::Unlinked;
        std::snprintf(Status.Reading, sizeof(Status.Reading), "Photon: SDK libs not linked yet");
        return Status;
    }
    Status.State = MapPhase(Adapter.QueryPhase());
    std::snprintf(Status.Reading, sizeof(Status.Reading), "%.255s", Adapter.QueryReading());
    Status.PacketsSent = static_cast<unsigned>(Adapter.QueryFramesSent());
    Status.PacketsReceived = static_cast<unsigned>(Adapter.QueryFramesReceived());
    Status.FirstPacketSent = Adapter.QueryFirstPacketSent();
    return Status;
}

Frontier::PhotonAdapter& QueryPhotonLink() noexcept { return Adapter; }
}
