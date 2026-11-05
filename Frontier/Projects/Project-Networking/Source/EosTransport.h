//============================================================================================================================================
//                                                            EOSTRANSPORT.H
//============================================================================================================================================
// 📦 Project-owned EOS relay over the engine EosAdapter (Frontier/Extensions/Networking):
// engine messages ride as FP1 Snapshot frames (fragmented to the P2P ceiling) between the lobby's
// product users. Lobby, voice and match sessions already run over EOS; this is the packet relay.

#pragma once
#include "EpicExchange.h"
#include "Frontier/Extensions/Networking/EosAdapter.h"

namespace Frontier { class NetworkingSession; }

namespace Networking
{
struct EosStatus
{
    bool Active = false;
    char Reading[256]{};
};

bool StartEosTransport(DiagnosticReception Reception) noexcept;
void TickEosTransport() noexcept;
void StopEosTransport() noexcept;
EosStatus InspectEosStatus() noexcept;
// The one EOS relay adapter the game session borrows (AttachTransport) to replicate over P2P.
Frontier::EosAdapter& QueryEosRelay() noexcept;
// Rebuilds the relay's peer table from the greeted session peers (identity = product-user string)
// matched against the lobby members. Call after Absorb admits peers; cheap when unchanged.
void RefreshEosRelayPeers(Frontier::NetworkingSession& Session) noexcept;
}
