# Networking

Evidence for the networking scaffold: `Frontier/Extensions/Networking/` (`NetworkingSpace`, `NetworkingExtension`).

## What it is

| Part                  | Role                                                                                     |
|-----------------------|------------------------------------------------------------------------------------------|
| `NetworkingSpace`     | The abstract world of one session: peers, roles (host, client), messages, tallies        |
| `NetworkTransport`    | The interface every transport implements (send, receive, name)                           |
| `LoopbackTransport`   | Both ends in one process, framed and unframed as a socket would carry them               |
| `NetworkingExtension` | Host: replicates changed `WorldSpace` records as their level TOML; client: absorbs them  |
| `ReplicationCodec`    | The FP1 wire codec (Probe, Snapshot, Input) plus MTU fragmentation for small transports  |
| `EosAdapter`          | `NetworkTransport` over EOS peer-to-peer packets (fragmented Snapshot frames)            |
| `PhotonAdapter`       | `NetworkTransport` over Photon Realtime room events (whole Snapshot frames)              |
| `NetworkingSession`   | One session's lifetime: space, selected transport, the Enabled toggle, per-frame advance |

A message travels as a 24-byte little-endian header (sender, receiver, topic, sequence, payload length) plus a TOML
payload. A host record is matched to the client's record of the same type and name, or constructed on the client.

On a provider, one engine message rides as one FP1 Snapshot frame (`'F' 'P' '1'`, version, category, u16BE length);
the EOS relay fragments frames into 1024 B fragments (`'F' 'P' 'F'` + message/index/count/total) under the P2P ceiling,
while Photon rooms carry whole frames as custom-event byte arrays. Greetings carry the local provider identity (an EOS
product-user string) so the far side addresses the peer on the relay. Without their SDKs linked both adapters compile
as honest stubs: sends refuse, receives arrive empty, availability reports false.

## Later

- Interest management and rates per record, and authority beyond host and client.
- The login side (`Project-Networking`) configures the adapters from its EOS session and lobby; the game side borrows
  them (`AttachTransport`) and refreshes the relay peer table (`RefreshEosRelayPeers`) as greetings admit peers.

## Test

`NetworkingSpaceValidation` (ctest) joins a host level and a client level through the loopback transport:

- The first replication sends 3 records, and the client's level then matches the host's.
- An unchanged level sends nothing; moving the sun sends one message.
- Removing a record removes it on the client.
- Damaged frames are refused, and the tallies on both sides agree.

The ledger is in `Results/NetworkingSpaceValidation.md`.

`NetworkingProviderValidation` (ctest) covers the providers without their SDKs:

- FP1 codec vectors: exact header bytes, round-trip, truncated/bad-magic/future-version/unknown-category refused,
  the 65535 B ceiling accepted and anything past it refused.
- Fragmentation: a 5000 B packet splits into 5 chunks, out-of-order fragments assemble to the exact packet,
  redeliveries absorb once, damaged fragments are refused.
- Adapter honesty: both adapters report their SDK missing, still configure, refuse sends and receive nothing.
- The session toggle: disabled sessions stay silent, enabling replicates and applies, disabling mid-change silences,
  re-enabling resumes; tallies agree; the reading names the toggle and the transport.
- The identity handshake, attached transports, and per-provider availability.

The ledger is in `Results/NetworkingProviderValidation.md`.
