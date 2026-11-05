/*==========================================================================================================================================
                                                             NETWORKINGSPACE.H
==========================================================================================================================================*/
// 🧩 NetworkingSpace, the abstract world networking lives in: the peers of one session, their roles and the messages between
//    them. It owns no socket: a transport (NetworkingExtension's loopback today, Epic Online Services later) carries the messages.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{

enum class NetworkRole : uint8_t
{
    Host,     // owns the authoritative world and replicates it
    Client,   // receives the host's world
};

// 📝 What a message carries: a greeting, one record's configuration, a record's removal, a farewell.
enum class NetworkTopic : uint8_t
{
    Greeting,
    RecordConfiguration,
    RecordRemoval,
    Farewell,
};

struct NetworkPeer
{
    uint32_t    Identifier = 0;
    std::string Name;
    NetworkRole Role       = NetworkRole::Client;
    bool        Connected  = true;
};

struct NetworkMessage
{
    uint32_t     From     = 0;    // [-] peer identifier
    uint32_t     To       = 0;    // [-] peer identifier; 0 = every peer
    NetworkTopic Topic    = NetworkTopic::Greeting;
    uint64_t     Sequence = 0;    // [-] per sender, from 1
    std::string  Payload;         // [-] TOML text
};

struct NetworkTally
{
    uint64_t MessagesSent     = 0;
    uint64_t MessagesReceived = 0;
    uint64_t BytesSent        = 0;   // [B] payloads
    uint64_t BytesReceived    = 0;   // [B]
};

class NetworkingSpace
{
  public:
    // 📝 This side of the session; identifiers start at 1.
    uint32_t ConfigureLocalPeer(std::string Name, NetworkRole Role);
    uint32_t AdmitPeer(std::string Name, NetworkRole Role);
    void     DismissPeer(uint32_t Identifier);

    [[nodiscard]] const NetworkPeer*              QueryLocalPeer() const noexcept;
    [[nodiscard]] const std::vector<NetworkPeer>& QueryPeers() const noexcept { return Peers; }
    [[nodiscard]] bool                            QueryHosting() const noexcept;

    // 📝 A peer's provider identity (an EOS product-user string its greeting carried); null when unregistered.
    void                            RegisterPeerIdentity(uint32_t Identifier, std::string Identity);
    [[nodiscard]] const std::string* QueryPeerIdentity(uint32_t Identifier) const noexcept;

    // 📝 Queues a message from the local peer (sequence and sender filled in).
    void Post(NetworkTopic Topic, std::string Payload, uint32_t To = 0);
    // 📝 The queued outgoing messages, emptied.
    [[nodiscard]] std::vector<NetworkMessage> TakeOutgoing();
    // 📝 A message the transport received; messages addressed to another peer are dropped.
    void Deliver(NetworkMessage Message);
    // 📝 The received messages in arrival order, emptied.
    [[nodiscard]] std::vector<NetworkMessage> TakeIncoming();

    [[nodiscard]] const NetworkTally& QueryTally() const noexcept { return Tally; }

  private:
    std::vector<NetworkPeer>                 Peers;
    std::vector<NetworkMessage>              Outgoing;
    std::vector<NetworkMessage>              Incoming;
    std::unordered_map<uint32_t, std::string> Identities;   // peer identifier → the identity its greeting carried
    uint32_t                    LocalIdentifier = 0;
    uint32_t                    NextIdentifier  = 1;
    uint64_t                    NextSequence    = 1;
    NetworkTally                Tally;
};

} // namespace Frontier
