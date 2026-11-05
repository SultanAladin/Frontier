/*==========================================================================================================================================
                                                               EOSADAPTER.CPP
==========================================================================================================================================*/
// 🧩 The EOS peer-to-peer relay behind NetworkTransport; an honest stub when the EOS SDK is not linked.

#include "Frontier/Extensions/Networking/EosAdapter.h"

#if defined(FRONTIER_EOS_SDK)
#include <eos_sdk.h>
#endif

#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

constexpr uint8_t RelayChannel       = 0;      // [-] - P2P channel the relay owns
constexpr size_t  ReceiveCeiling     = 256;    // [-] - packets drained per Receive, at most
constexpr size_t  RelaySocketExtent  = 33;     // [-] - EOS socket-name extent with the terminator

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      EOS ADAPTER
//------------------------------------------------------------------------------------------------------------------------

bool EosAdapter::QuerySupported() noexcept
{
#if defined(FRONTIER_EOS_SDK)
    return true;
#else
    return false;
#endif
}

bool EosAdapter::Configure(const EosRelayConfiguration& Configuration_)
{
    if (Configuration_.SocketName[0] == '\0') return false;
    Configuration = Configuration_;
    Configuration.SocketName[RelaySocketExtent - 1] = '\0';
    Assembly.Reset();
    if (QuerySupported())
    {
        std::snprintf(Reading, sizeof(Reading), "EOS: relay armed on socket %s for %zu peers", Configuration.SocketName,
                      Configuration.Peers.size());
    }
    else
    {
        std::snprintf(Reading, sizeof(Reading), "EOS: SDK not linked; relay unavailable");
    }
    if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic(Reading);
    return true;
}

bool EosAdapter::Send(const NetworkMessage& Message)
{
#if defined(FRONTIER_EOS_SDK)
    if (Configuration.Platform == nullptr || Configuration.LocalUser == nullptr) return false;
    const std::string Wire = EncodeNetworkMessage(Message);
    std::vector<std::vector<uint8_t>> Fragments;
    const uint16_t                    Id = NextMessage++;
    if (!ReplicationCodec::SplitPacket(ReplicationCategory::Snapshot, reinterpret_cast<const uint8_t*>(Wire.data()), Wire.size(), Id,
                                       Fragments))
    {
        ++PacketsRefused;
        return false;
    }
    // 📝 To == 0 reaches every configured peer; otherwise only the addressed one.
    std::vector<EosRelayPeer> Targets;
    for (const EosRelayPeer& Peer : Configuration.Peers)
    {
        if (Peer.User != nullptr && (Message.To == 0 || Message.To == Peer.Peer)) Targets.push_back(Peer);
    }
    if (Targets.empty()) return false;

    EOS_P2P_SocketId Socket = {};
    Socket.ApiVersion       = EOS_P2P_SOCKETID_API_LATEST;
    std::memcpy(Socket.SocketName, Configuration.SocketName, RelaySocketExtent);
    EOS_HP2P Relay = EOS_Platform_GetP2PInterface(static_cast<EOS_HPlatform>(Configuration.Platform));
    bool           Accepted = true;
    for (const EosRelayPeer& Target : Targets)
    {
        for (const std::vector<uint8_t>& Fragment : Fragments)
        {
            EOS_P2P_SendPacketOptions Options = {};
            Options.ApiVersion                = EOS_P2P_SENDPACKET_API_LATEST;
            Options.LocalUserId               = static_cast<EOS_ProductUserId>(Configuration.LocalUser);
            Options.RemoteUserId              = static_cast<EOS_ProductUserId>(Target.User);
            Options.SocketId                  = &Socket;
            Options.Channel                   = RelayChannel;
            Options.DataLengthBytes           = static_cast<uint32_t>(Fragment.size());
            Options.Data                      = Fragment.data();
            Options.Reliability               = EOS_PR_ReliableOrdered;
            if (EOS_P2P_SendPacket(Relay, &Options) != EOS_Success)
            {
                Accepted = false;
                break;
            }
            ++FragmentsSent;
        }
    }
    std::snprintf(Reading, sizeof(Reading), "EOS: relay on socket %s; fragments sent %llu", Configuration.SocketName,
                  static_cast<unsigned long long>(FragmentsSent));
    return Accepted;
#else
    (void)Message;
    return false;
#endif
}

std::vector<NetworkMessage> EosAdapter::Receive()
{
    std::vector<NetworkMessage> Messages;
#if defined(FRONTIER_EOS_SDK)
    if (Configuration.Platform == nullptr || Configuration.LocalUser == nullptr) return Messages;
    EOS_HP2P Relay = EOS_Platform_GetP2PInterface(static_cast<EOS_HPlatform>(Configuration.Platform));
    // ⚠️ Assumes the pinned SDK's P2P options (ApiVersion, LocalUserId, RequestedChannel pointer, SocketId ApiVersion);
    //    the SDK-present path compiles only where the EOS SDK is staged and is not exercised in this environment.
    for (size_t Drained = 0; Drained < ReceiveCeiling; ++Drained)
    {
        EOS_P2P_GetNextReceivedPacketSizeOptions SizeOptions = {};
        SizeOptions.ApiVersion                               = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
        SizeOptions.LocalUserId                              = static_cast<EOS_ProductUserId>(Configuration.LocalUser);
        uint8_t Channel                                      = RelayChannel;
        SizeOptions.RequestedChannel                         = &Channel;
        uint32_t Extent                                      = 0;
        if (EOS_P2P_GetNextReceivedPacketSize(Relay, &SizeOptions, &Extent) != EOS_Success || Extent == 0) break;
        std::vector<uint8_t>   Packet(Extent);
        EOS_P2P_SocketId       FromSocket = {};
        EOS_ProductUserId      FromUser   = nullptr;
        uint32_t               Written    = 0;
        EOS_P2P_ReceivePacketOptions ReceiveOptions = {};
        ReceiveOptions.ApiVersion                   = EOS_P2P_RECEIVEPACKET_API_LATEST;
        ReceiveOptions.LocalUserId                  = static_cast<EOS_ProductUserId>(Configuration.LocalUser);
        ReceiveOptions.MaxDataSizeBytes             = Extent;
        ReceiveOptions.RequestedChannel             = &Channel;
        if (EOS_P2P_ReceivePacket(Relay, &ReceiveOptions, &FromSocket, &FromUser, Packet.data(), &Written) != EOS_Success)
        {
            ++PacketsRefused;
            continue;
        }
        Packet.resize(Written);
        ++FragmentsReceived;
        if (std::strncmp(FromSocket.SocketName, Configuration.SocketName, RelaySocketExtent) != 0)
        {
            ++PacketsRefused;
            continue;
        }
        uint32_t Sender  = 0;
        bool     Known   = false;
        for (const EosRelayPeer& Peer : Configuration.Peers)
        {
            if (Peer.User == FromUser)
            {
                Sender = Peer.Peer;
                Known  = true;
                break;
            }
        }
        if (!Known)
        {
            ++PacketsRefused;
            continue;
        }
        const bool Fragment = Packet.size() >= 3 && Packet[0] == 'F' && Packet[1] == 'P' && Packet[2] == 'F';
        const bool Frame    = Packet.size() >= 3 && Packet[0] == 'F' && Packet[1] == 'P' && Packet[2] == '1';
        if (!Fragment && !Frame)
        {
            ++PacketsRefused;
            continue;
        }
        ReplicationCategory      Category      = ReplicationCategory::Probe;
        std::vector<uint8_t>     Assembled{};
        const uint8_t*           Payload       = nullptr;
        size_t                   PayloadLength = 0;
        bool                     Complete      = false;
        if (Fragment)
        {
            if (!Assembly.AbsorbFragment(Sender, Packet.data(), Packet.size(), Category, Assembled))
            {
                ++PacketsRefused;
                continue;
            }
            Complete = !Assembled.empty();
            if (Complete)
            {
                Payload       = Assembled.data();
                PayloadLength = Assembled.size();
            }
        }
        else
        {
            Complete = ReplicationCodec::DecodeFrame(Packet.data(), Packet.size(), Category, Payload, PayloadLength);
            if (!Complete)
            {
                ++PacketsRefused;
                continue;
            }
        }
        if (!Complete || Category != ReplicationCategory::Snapshot) continue;   // [-] - probes land nowhere
        NetworkMessage Message;
        const std::string_view Wire(reinterpret_cast<const char*>(Payload), PayloadLength);
        if (!DecodeNetworkMessage(Wire, Message))
        {
            ++PacketsRefused;
            continue;
        }
        Messages.push_back(std::move(Message));
    }
    std::snprintf(Reading, sizeof(Reading), "EOS: relay on socket %s; fragments received %llu, refused %llu",
                  Configuration.SocketName, static_cast<unsigned long long>(FragmentsReceived),
                  static_cast<unsigned long long>(PacketsRefused));
#endif
    return Messages;
}

} // namespace Frontier
