# NetworkingProviderValidation

Codec vectors, fragmentation, adapter honesty and the session toggle.

| Result | Claim |
|---|---|
| 🟢 | a snapshot frame encodes |
| 🟢 | the frame header is magic, version 1, snapshot, length 9 |
| 🟢 | the frame decodes to its category and payload |
| 🟢 | a truncated frame is refused |
| 🟢 | bad magic is refused |
| 🟢 | a future version is refused |
| 🟢 | an unknown category is refused |
| 🟢 | a payload past the 65535 B ceiling is refused |
| 🟢 | a payload at the ceiling encodes (65542 B) |
| 🟢 | a 5000 B packet splits into 5 fragments |
| 🟢 | chunks fill 1024 B but the last (904 B) |
| 🟢 | out-of-order fragments assemble to the exact packet |
| 🟢 | no assembly lingers past completion |
| 🟢 | one fragment buffers without completing |
| 🟢 | a redelivery absorbs once |
| 🟢 | a small packet is one fragment |
| 🟢 | a lone fragment completes at once |
| 🟢 | bad fragment magic is refused |
| 🟢 | reset drops the buffered fragment |
| 🟢 | the EOS adapter reports its SDK missing |
| 🟢 | the EOS adapter still configures |
| 🟢 | its reading names the missing SDK |
| 🟢 | its sends refuse and its receives arrive empty |
| 🟢 | the Photon adapter reports its SDK missing |
| 🟢 | it configures but refuses to connect |
| 🟢 | its message and frame channels stay shut |
| 🟢 | both sessions start with Networking disabled |
| 🟢 | disabled sessions stay silent (0 moved) |
| 🟢 | nothing reaches the client while disabled |
| 🟢 | the enabled host replicates its 3 records (3) |
| 🟢 | the enabled client applies the greeting and the 3 records (4) |
| 🟢 | the client level holds 3 records |
| 🟢 | an unchanged level sends nothing |
| 🟢 | disabling the host silences the moved sun |
| 🟢 | re-enabling sends and applies the move |
| 🟢 | the client received every message and byte the host sent (5 messages, 2666 B) |
| 🟢 | the session reading names the toggle and the transport |
| 🟢 | the greeting carries the provider identity |
| 🟢 | an unknown peer has no identity |
| 🟢 | an attached session hides pairing |
| 🟢 | selecting loopback detaches back to the owned transport |
| 🟢 | loopback is always available |
| 🟢 | EOS availability follows its SDK |
| 🟢 | Photon availability follows its SDK |
| 🟢 | unavailable providers refuse selection |

45 / 45 assertions.
