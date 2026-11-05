/*==========================================================================================================================================
                                                           NETWORKINGTRANSPORT.CPP
==========================================================================================================================================*/
// 🧩 The messages' wire form and the loopback transport: light enough for standalone builds (no level, no TOML).

#include "Frontier/Extensions/Networking/NetworkingExtension.h"

namespace Frontier
{

namespace
{

constexpr size_t HeaderBytes = 24;

void WriteNumber(std::string& Bytes, uint64_t Value, size_t Width)
{
    for (size_t Index = 0; Index < Width; ++Index) Bytes.push_back(static_cast<char>((Value >> (8 * Index)) & 0xffu));
}

uint64_t ReadNumber(std::string_view Bytes, size_t Offset, size_t Width)
{
    uint64_t Value = 0;
    for (size_t Index = 0; Index < Width; ++Index) Value |= static_cast<uint64_t>(static_cast<unsigned char>(Bytes[Offset + Index])) << (8 * Index);
    return Value;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      WIRE FORM
//------------------------------------------------------------------------------------------------------------------------

std::string EncodeNetworkMessage(const NetworkMessage& Message)
{
    std::string Bytes;
    Bytes.reserve(HeaderBytes + Message.Payload.size());
    WriteNumber(Bytes, Message.From, 4);
    WriteNumber(Bytes, Message.To, 4);
    WriteNumber(Bytes, static_cast<uint64_t>(Message.Topic), 4);
    WriteNumber(Bytes, Message.Sequence, 8);
    WriteNumber(Bytes, Message.Payload.size(), 4);
    Bytes += Message.Payload;
    return Bytes;
}

bool DecodeNetworkMessage(std::string_view Bytes, NetworkMessage& Message)
{
    if (Bytes.size() < HeaderBytes) return false;
    const uint64_t Length = ReadNumber(Bytes, 20, 4);
    const uint64_t Topic  = ReadNumber(Bytes, 8, 4);
    if (Bytes.size() != HeaderBytes + Length || Topic > static_cast<uint64_t>(NetworkTopic::Farewell)) return false;
    Message.From     = static_cast<uint32_t>(ReadNumber(Bytes, 0, 4));
    Message.To       = static_cast<uint32_t>(ReadNumber(Bytes, 4, 4));
    Message.Topic    = static_cast<NetworkTopic>(Topic);
    Message.Sequence = ReadNumber(Bytes, 12, 8);
    Message.Payload  = std::string(Bytes.substr(HeaderBytes));
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        LOOPBACK
//------------------------------------------------------------------------------------------------------------------------

void LoopbackTransport::Connect(LoopbackTransport& First, LoopbackTransport& Second) noexcept
{
    First.Partner  = &Second;
    Second.Partner = &First;
}

bool LoopbackTransport::Send(const NetworkMessage& Message)
{
    if (Partner == nullptr) return false;
    Partner->Packets.push_back(EncodeNetworkMessage(Message));
    return true;
}

std::vector<NetworkMessage> LoopbackTransport::Receive()
{
    std::vector<NetworkMessage> Messages;
    for (const std::string& Packet : Packets)
    {
        NetworkMessage Message;
        if (DecodeNetworkMessage(Packet, Message)) Messages.push_back(std::move(Message));
    }
    Packets.clear();
    return Messages;
}

} // namespace Frontier
