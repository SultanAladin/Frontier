/*==========================================================================================================================================
                                                      NETWORKINGPROVIDERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The provider transports without their SDKs: the FP1 codec vectors, MTU fragmentation round-trips, the adapters' honest
//    unavailability, and two sessions paired over loopback with the Networking toggle enabling and silencing replication.
//
//     NetworkingProviderValidation [ledger.md]

#include "Frontier/Extensions/Networking/NetworkingSession.h"

#include "Frontier/Engine/Scene/WorldSpace.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

int                      Failures   = 0;
int                      Assertions = 0;
std::vector<std::string> Ledger;

void Expect(bool Condition, const std::string& Claim)
{
    ++Assertions;
    if (!Condition) ++Failures;
    Ledger.push_back(std::string("| ") + (Condition ? "🟢" : "🔴") + " | " + Claim + " |");
    if (!Condition) std::printf("  🔴 %s\n", Claim.c_str());
}

std::vector<uint8_t> PatternBytes(size_t Length)
{
    std::vector<uint8_t> Bytes(Length);
    for (size_t Index = 0; Index < Length; ++Index) Bytes[Index] = static_cast<uint8_t>((Index * 31 + 7) & 0xffu);
    return Bytes;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    // ① the FP1 codec vectors
    const std::vector<uint8_t> Payload = PatternBytes(9);
    std::vector<uint8_t>       Frame;
    Expect(ReplicationCodec::EncodeFrame(ReplicationCategory::Snapshot, Payload.data(), Payload.size(), Frame),
           "a snapshot frame encodes");
    const uint8_t Wire[] = { 'F', 'P', '1', 1, 1, 0, 9 };
    Expect(Frame.size() == 16 && std::equal(std::begin(Wire), std::end(Wire), Frame.begin()),
           "the frame header is magic, version 1, snapshot, length 9");
    ReplicationCategory  Category      = ReplicationCategory::Probe;
    const uint8_t*       Decoded       = nullptr;
    size_t               DecodedLength = 0;
    Expect(ReplicationCodec::DecodeFrame(Frame.data(), Frame.size(), Category, Decoded, DecodedLength) &&
               Category == ReplicationCategory::Snapshot && DecodedLength == 9 &&
               std::equal(Payload.begin(), Payload.end(), Decoded),
           "the frame decodes to its category and payload");
    std::vector<uint8_t> Truncated = Frame;
    Truncated.pop_back();
    Expect(!ReplicationCodec::DecodeFrame(Truncated.data(), Truncated.size(), Category, Decoded, DecodedLength),
           "a truncated frame is refused");
    std::vector<uint8_t> Corrupt = Frame;
    Corrupt[0]                   = 'X';
    Expect(!ReplicationCodec::DecodeFrame(Corrupt.data(), Corrupt.size(), Category, Decoded, DecodedLength), "bad magic is refused");
    Corrupt         = Frame;
    Corrupt[3]      = 2;
    Expect(!ReplicationCodec::DecodeFrame(Corrupt.data(), Corrupt.size(), Category, Decoded, DecodedLength),
           "a future version is refused");
    Corrupt    = Frame;
    Corrupt[4] = 9;
    Expect(!ReplicationCodec::DecodeFrame(Corrupt.data(), Corrupt.size(), Category, Decoded, DecodedLength),
           "an unknown category is refused");
    const std::vector<uint8_t> Oversized = PatternBytes(ReplicationCodec::MaxPayload + 1);
    Expect(!ReplicationCodec::EncodeFrame(ReplicationCategory::Snapshot, Oversized.data(), Oversized.size(), Frame),
           "a payload past the 65535 B ceiling is refused");
    const std::vector<uint8_t> Ceiling = PatternBytes(ReplicationCodec::MaxPayload);
    const bool                 CeilingAccepted =
        ReplicationCodec::EncodeFrame(ReplicationCategory::Snapshot, Ceiling.data(), Ceiling.size(), Frame);
    Expect(CeilingAccepted && Frame.size() == ReplicationCodec::MaxPacket,
           "a payload at the ceiling encodes (" + std::to_string(Frame.size()) + " B)");

    // ② MTU fragmentation
    const std::vector<uint8_t> Packet = PatternBytes(5000);
    std::vector<std::vector<uint8_t>> Fragments;
    Expect(ReplicationCodec::SplitPacket(ReplicationCategory::Snapshot, Packet.data(), Packet.size(), 0x1234, Fragments) &&
               Fragments.size() == 5,
           "a 5000 B packet splits into 5 fragments");
    Expect(Fragments[0].size() == 14 + 1024 && Fragments[4].size() == 14 + 904, "chunks fill 1024 B but the last (904 B)");
    FragmentAssembly          Assembly;
    ReplicationCategory       AssembledCategory = ReplicationCategory::Probe;
    std::vector<uint8_t>      Assembled;
    bool                      Accepted = true;
    for (size_t Order : { 3u, 0u, 4u, 1u, 2u })
    {
        Assembled.clear();
        Accepted = Assembly.AbsorbFragment(7, Fragments[Order].data(), Fragments[Order].size(), AssembledCategory, Assembled) && Accepted;
    }
    Expect(Accepted && AssembledCategory == ReplicationCategory::Snapshot && Assembled == Packet,
           "out-of-order fragments assemble to the exact packet");
    Expect(Assembly.QueryIncomplete() == 0, "no assembly lingers past completion");
    Assembled.clear();
    Accepted = Assembly.AbsorbFragment(7, Fragments[1].data(), Fragments[1].size(), AssembledCategory, Assembled);
    Expect(Accepted && Assembled.empty() && Assembly.QueryIncomplete() == 1, "one fragment buffers without completing");
    Assembled.clear();
    Accepted = Assembly.AbsorbFragment(7, Fragments[1].data(), Fragments[1].size(), AssembledCategory, Assembled);
    Expect(Accepted && Assembled.empty() && Assembly.QueryIncomplete() == 1, "a redelivery absorbs once");
    std::vector<uint8_t> Small;
    Expect(ReplicationCodec::SplitPacket(ReplicationCategory::Probe, Payload.data(), Payload.size(), 9, Fragments) &&
               Fragments.size() == 1,
           "a small packet is one fragment");
    Assembled.clear();
    Expect(Assembly.AbsorbFragment(7, Fragments[0].data(), Fragments[0].size(), AssembledCategory, Assembled) &&
               Assembled == Payload && AssembledCategory == ReplicationCategory::Probe,
           "a lone fragment completes at once");
    std::vector<uint8_t> Damaged = Fragments[0];
    Damaged[2]                   = 'X';
    Assembled.clear();
    Expect(!Assembly.AbsorbFragment(7, Damaged.data(), Damaged.size(), AssembledCategory, Assembled), "bad fragment magic is refused");
    Assembly.Reset();
    Expect(Assembly.QueryIncomplete() == 0, "reset drops the buffered fragment");

    // ③ the adapters without their SDKs
    EosAdapter Eos;
    Expect(!EosAdapter::QuerySupported(), "the EOS adapter reports its SDK missing");
    EosRelayConfiguration Relay;
    Expect(Eos.Configure(Relay), "the EOS adapter still configures");
    Expect(std::string(Eos.QueryReading()).find("not linked") != std::string::npos, "its reading names the missing SDK");
    Expect(!Eos.Send(NetworkMessage{}) && Eos.Receive().empty(), "its sends refuse and its receives arrive empty");
    PhotonAdapter Photon;
    Expect(!PhotonAdapter::QuerySupported(), "the Photon adapter reports its SDK missing");
    PhotonLinkConfiguration Link;
    Expect(Photon.Configure(Link) && !Photon.Begin(), "it configures but refuses to connect");
    Expect(!Photon.Send(NetworkMessage{}) && Photon.Receive().empty() && !Photon.SendFrame(Payload.data(), Payload.size()) &&
               Photon.TakeFrames().empty(),
           "its message and frame channels stay shut");

    // ④ two sessions over loopback with the Networking toggle
    WorldSpace Host, Client;
    const RecordToken Sun   = Host.ConstructRecord("Solar", "Sun");
    (void)Host.ConstructRecord("RigidBody", "Crate");
    (void)Host.ConstructRecord("Clouds", "Cumulus");
    (void)Client.ConstructRecord("Solar", "Sun");
    NetworkingSession HostSession, ClientSession;
    (void)HostSession.ConfigureLocalPeer("Host", NetworkRole::Host);
    (void)ClientSession.ConfigureLocalPeer("Client", NetworkRole::Client);
    LoopbackTransport::Connect(*HostSession.QueryLoopback(), *ClientSession.QueryLoopback());
    Expect(!HostSession.QueryEnabled() && !ClientSession.QueryEnabled(), "both sessions start with Networking disabled");
    Expect(HostSession.Advance(Host) == 0 && ClientSession.Advance(Client) == 0, "disabled sessions stay silent (0 moved)");
    Expect(Client.QueryRecordCount() == 1, "nothing reaches the client while disabled");
    HostSession.ConfigureEnabled(true);
    ClientSession.ConfigureEnabled(true);
    HostSession.Greet();
    ClientSession.Greet();
    const size_t First = HostSession.Advance(Host);
    Expect(First == 3, "the enabled host replicates its 3 records (" + std::to_string(First) + ")");
    const size_t Applied = ClientSession.Advance(Client);
    Expect(Applied == 4, "the enabled client applies the greeting and the 3 records (" + std::to_string(Applied) + ")");
    Expect(Client.QueryRecordCount() == 3, "the client level holds 3 records");
    Expect(HostSession.Advance(Host) == 0, "an unchanged level sends nothing");
    if (auto* Solar = dynamic_cast<SolarRecord*>(Host.FindRecord(Sun)))
    {
        Solar->AccessConfiguration().ElevationDegrees = 12.5f;
        Solar->AccessConfiguration().AdvanceRevision();
    }
    HostSession.ConfigureEnabled(false);
    Expect(HostSession.Advance(Host) == 0, "disabling the host silences the moved sun");
    HostSession.ConfigureEnabled(true);
    Expect(HostSession.Advance(Host) == 1 && ClientSession.Advance(Client) == 1, "re-enabling sends and applies the move");
    const NetworkTally& Sent     = HostSession.QuerySpace().QueryTally();
    const NetworkTally& Received = ClientSession.QuerySpace().QueryTally();
    Expect(Sent.MessagesSent == Received.MessagesReceived && Sent.BytesSent == Received.BytesReceived,
           "the client received every message and byte the host sent (" + std::to_string(Sent.MessagesSent) + " messages, " +
               std::to_string(Sent.BytesSent) + " B)");
    Expect(HostSession.QueryReading().find("Enabled") != std::string::npos &&
               HostSession.QueryReading().find("Loopback") != std::string::npos,
           "the session reading names the toggle and the transport");

    // ⑤ the provider identity handshake
    NetworkingSession IdentifiedHost, IdentifiedClient;
    (void)IdentifiedHost.ConfigureLocalPeer("Host", NetworkRole::Host);
    (void)IdentifiedClient.ConfigureLocalPeer("Client", NetworkRole::Client);
    IdentifiedHost.QueryExtension().ConfigureLocalIdentity("0002ffffhostpuid0000000000000001");
    LoopbackTransport::Connect(*IdentifiedHost.QueryLoopback(), *IdentifiedClient.QueryLoopback());
    IdentifiedHost.ConfigureEnabled(true);
    IdentifiedClient.ConfigureEnabled(true);
    IdentifiedHost.Greet();
    (void)IdentifiedHost.Advance(Host);
    (void)IdentifiedClient.Advance(Client);
    const std::string* Carried = nullptr;
    for (const NetworkPeer& Peer : IdentifiedClient.QuerySpace().QueryPeers())
    {
        if (Peer.Name == "Host") Carried = IdentifiedClient.QuerySpace().QueryPeerIdentity(Peer.Identifier);
    }
    Expect(Carried != nullptr && *Carried == "0002ffffhostpuid0000000000000001", "the greeting carries the provider identity");
    Expect(IdentifiedClient.QuerySpace().QueryPeerIdentity(9999) == nullptr, "an unknown peer has no identity");

    // ⑥ attached transports and availability
    LoopbackTransport Borrowed;
    IdentifiedHost.AttachTransport(TransportCategory::Loopback, Borrowed);
    Expect(IdentifiedHost.QueryAttached() && IdentifiedHost.QueryLoopback() == nullptr, "an attached session hides pairing");
    Expect(IdentifiedHost.SelectLoopback() && !IdentifiedHost.QueryAttached() && IdentifiedHost.QueryLoopback() != nullptr,
           "selecting loopback detaches back to the owned transport");
    Expect(NetworkingSession::QueryTransportAvailable(TransportCategory::Loopback), "loopback is always available");
    Expect(NetworkingSession::QueryTransportAvailable(TransportCategory::Eos) == EosAdapter::QuerySupported(),
           "EOS availability follows its SDK");
    Expect(NetworkingSession::QueryTransportAvailable(TransportCategory::Photon) == PhotonAdapter::QuerySupported(),
           "Photon availability follows its SDK");
    Expect(!IdentifiedHost.SelectEos(Relay) && !IdentifiedHost.SelectPhoton(Link), "unavailable providers refuse selection");

    std::printf("%s %d / %d assertions\n", Failures == 0 ? "🟢" : "🔴", Assertions - Failures, Assertions);
    if (ArgumentCount > 1)
    {
        std::ofstream Out(Arguments[1]);
        Out << "# NetworkingProviderValidation\n\nCodec vectors, fragmentation, adapter honesty and the session toggle.\n\n| Result | Claim |\n|---|---|\n";
        for (const std::string& Line : Ledger) Out << Line << '\n';
        Out << "\n" << Assertions - Failures << " / " << Assertions << " assertions.\n";
    }
    return Failures == 0 ? 0 : 1;
}
