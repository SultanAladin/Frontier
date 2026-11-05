/*==========================================================================================================================================
                                                           NETWORKINGEXTENSION.H
==========================================================================================================================================*/
// 🧩 The bridge between a level and its peers: the host replicates its WorldSpace records into the NetworkingSpace, a transport
//    carries the messages, and a client absorbs them into its own WorldSpace. The transport is an interface: the loopback here
//    (one process, both ends) now; an EOSExtension / EpicAdapter (Epic Online Services) implements it later.

#pragma once

#include "Frontier/Extensions/Networking/NetworkingSpace.h"

#include "Frontier/Engine/Scene/RecordToken.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier
{

class WorldSpace;

// 📝 The wire form of a message: a 24-byte little-endian header (sender, receiver, topic, sequence, payload length) and the
//    payload. Every transport sends messages in this form.
[[nodiscard]] std::string EncodeNetworkMessage(const NetworkMessage& Message);
bool                      DecodeNetworkMessage(std::string_view Bytes, NetworkMessage& Message);

class NetworkTransport
{
  public:
    virtual ~NetworkTransport() = default;

    virtual bool                                      Send(const NetworkMessage& Message) = 0;
    [[nodiscard]] virtual std::vector<NetworkMessage> Receive()                           = 0;
    [[nodiscard]] virtual const char*                 QueryName() const noexcept          = 0;
};

// 📝 Two ends in one program: what one sends, the other receives, in the wire form a socket would carry.
class LoopbackTransport final : public NetworkTransport
{
  public:
    static void Connect(LoopbackTransport& First, LoopbackTransport& Second) noexcept;

    bool                                      Send(const NetworkMessage& Message) override;
    [[nodiscard]] std::vector<NetworkMessage> Receive() override;
    [[nodiscard]] const char*                 QueryName() const noexcept override { return "Loopback"; }

  private:
    LoopbackTransport*       Partner = nullptr;
    std::vector<std::string> Packets;
};

class NetworkingExtension
{
  public:
    NetworkingExtension(NetworkingSpace& Space, NetworkTransport& Transport) noexcept : Space(Space), Transport(Transport) {}

    // 📝 The local peer's provider identity (an EOS product-user string): Greet carries it so the far side can address
    //    this peer on the provider relay. Empty: greetings carry name and role only, as before.
    void ConfigureLocalIdentity(std::string Identity) { LocalIdentity = std::move(Identity); }

    // 📝 Introduces the local peer to the others (name, role, and the configured identity).
    void Greet();

    // 📝 Host: every record whose level TOML changed since the last call leaves as one RecordConfiguration message; records gone from
    //    the level leave as RecordRemoval. Returns the messages posted.
    size_t Replicate(const WorldSpace& World);

    // 📝 Moves posted messages to the transport and received ones into the space.
    void Exchange();

    // 📝 Client: applies the received messages. A host record is matched to the local record of the same type and name (both
    //    sides opened the same level) or constructed. Returns the messages applied.
    size_t Absorb(WorldSpace& World);

    [[nodiscard]] size_t QueryMatchedRecords() const noexcept { return Matched.size(); }

    // 📝 The provider identity a peer's greeting carried (null when it carried none or never greeted).
    [[nodiscard]] const std::string* QueryPeerIdentity(uint32_t Peer) const noexcept { return Space.QueryPeerIdentity(Peer); }

  private:
    NetworkingSpace&                          Space;
    NetworkTransport&                         Transport;
    std::string                               LocalIdentity{};   // [-] - carried by Greet when non-empty
    std::unordered_map<uint64_t, std::string> Sent;      // host record token → the payload last sent
    std::unordered_map<uint64_t, RecordToken> Matched;   // host record token → local record
};

} // namespace Frontier
