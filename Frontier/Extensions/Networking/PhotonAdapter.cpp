/*==========================================================================================================================================
                                                              PHOTONADAPTER.CPP
==========================================================================================================================================*/
// 🧩 The Photon Realtime room events behind NetworkTransport; an honest stub when the Photon SDK is not linked.

#include "Frontier/Extensions/Networking/PhotonAdapter.h"

#if defined(FRONTIER_PHOTON_SDK)
// 📝 The Photon headers do not detect the target; the consumer predefines it.
#if defined(_WIN32) && !defined(_EG_WINDOWS_PLATFORM)
#define _EG_WINDOWS_PLATFORM true
#endif
#include <LoadBalancing-cpp/inc/Client.h>
#include <LoadBalancing-cpp/inc/Enums/CustomAuthenticationType.h>
#include <LoadBalancing-cpp/inc/Enums/DisconnectCause.h>
#include <LoadBalancing-cpp/inc/Enums/PeerStates.h>
#include <Common-cpp/inc/Enums/TypeCode.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Frontier
{

namespace
{

constexpr char   BuiltInAppId[]        = "08ec2e74-b472-42d4-942e-27894128e8fb";
constexpr char   BuiltInAppVersion[]   = "1.0";
constexpr char   BuiltInRoom[]         = "FrontierDevRoom";
constexpr int    ConnectTimeoutSeconds = 30;     // [s]
constexpr size_t MaxTokenLength        = 8192;   // [B]

#if defined(FRONTIER_PHOTON_SDK)
constexpr nByte ReplicationEvent  = 1;   // [-] - custom event carrying FP1 frames
constexpr nByte DevRoomMaxPlayers = 8;
#endif

#if defined(FRONTIER_PHOTON_SDK)
uint64_t SteadyNanos() noexcept
{
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}
#endif

#if defined(FRONTIER_PHOTON_SDK)
bool ValidTokenChars(const char* Text) noexcept
{
    // 📝 JWTs are base64url plus dots; anything else would break the auth parameters.
    if (Text == nullptr || *Text == '\0' || std::strlen(Text) > MaxTokenLength) return false;
    for (const unsigned char* C = reinterpret_cast<const unsigned char*>(Text); *C != '\0'; ++C)
    {
        const bool Word = (*C >= 'A' && *C <= 'Z') || (*C >= 'a' && *C <= 'z') || (*C >= '0' && *C <= '9');
        if (!Word && *C != '-' && *C != '_' && *C != '.') return false;
    }
    return true;
}

bool ValidAppId(const char* Text) noexcept
{
    if (Text == nullptr || std::strlen(Text) != 36) return false;
    for (const unsigned char* C = reinterpret_cast<const unsigned char*>(Text); *C != '\0'; ++C)
    {
        const bool Hex = (*C >= '0' && *C <= '9') || (*C >= 'a' && *C <= 'f') || (*C >= 'A' && *C <= 'F');
        if (!Hex && *C != '-') return false;
    }
    return true;
}

const char* PickSetting(const char* Override, const char* Environment, const char* BuiltIn, char* Buffer, size_t Capacity) noexcept
{
    const char* Picked = (Override != nullptr && *Override != '\0') ? Override : BuiltIn;
    if (const char* FromEnvironment = std::getenv(Environment); FromEnvironment != nullptr && *FromEnvironment != '\0')
    {
        Picked = FromEnvironment;
    }
    std::snprintf(Buffer, Capacity, "%s", Picked);
    return Buffer;
}
#endif // FRONTIER_PHOTON_SDK

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHOTON ADAPTER
//------------------------------------------------------------------------------------------------------------------------

PhotonAdapter::PhotonAdapter()  = default;
PhotonAdapter::~PhotonAdapter()
{
    End();
}

bool PhotonAdapter::QuerySupported() noexcept
{
#if defined(FRONTIER_PHOTON_SDK)
    return true;
#else
    return false;
#endif
}

bool PhotonAdapter::Configure(const PhotonLinkConfiguration& Configuration_)
{
    Configuration = Configuration_;
    Configuration.AppId[sizeof(Configuration.AppId) - 1]                 = '\0';
    Configuration.AppVersion[sizeof(Configuration.AppVersion) - 1]       = '\0';
    Configuration.Room[sizeof(Configuration.Room) - 1]                   = '\0';
    Configuration.AuthToken[sizeof(Configuration.AuthToken) - 1]         = '\0';
    Configuration.OwnershipToken[sizeof(Configuration.OwnershipToken) - 1] = '\0';
    PendingRoom[0] = '\0';
    if (!QuerySupported()) std::snprintf(Reading, sizeof(Reading), "Photon: SDK not linked; cloud unavailable");
    else std::snprintf(Reading, sizeof(Reading), "Photon: idle");
    return true;
}

#if defined(FRONTIER_PHOTON_SDK)

// ⚠️ Assumes the pinned Photon Realtime Core C++ SDK (LoadBalancing client, Epic custom authentication); the SDK-present
//    path compiles only where the SDK headers and libraries are staged and is not exercised in this environment.
namespace
{

const char* DescribeCause(int Cause) noexcept
{
    namespace CauseOf = ExitGames::LoadBalancing::DisconnectCause;
    switch (Cause)
    {
        case CauseOf::NONE:                                        return "dispatch refused";
        case CauseOf::DISCONNECT_BY_SERVER_USER_LIMIT:             return "server user limit";
        case CauseOf::EXCEPTION_ON_CONNECT:                        return "server unreachable";
        case CauseOf::DISCONNECT_BY_SERVER:                        return "disconnected by server";
        case CauseOf::DISCONNECT_BY_SERVER_LOGIC:                  return "disconnected by server logic";
        case CauseOf::TIMEOUT_DISCONNECT:                          return "timeout";
        case CauseOf::EXCEPTION:                                   return "internal socket error";
        case CauseOf::INVALID_AUTHENTICATION:                      return "invalid AppId";
        case CauseOf::MAX_CCU_REACHED:                             return "CCU limit reached";
        case CauseOf::INVALID_REGION:                              return "region not allowed";
        case CauseOf::OPERATION_NOT_ALLOWED_IN_CURRENT_STATE:      return "operation not allowed";
        case CauseOf::CUSTOM_AUTHENTICATION_FAILED:                return "Epic authentication refused";
        case CauseOf::CLIENT_VERSION_TOO_OLD:                      return "client version too old";
        case CauseOf::CLIENT_VERSION_INVALID:                      return "client version invalid";
        case CauseOf::DASHBOARD_VERSION_INVALID:                   return "dashboard version invalid";
        case CauseOf::AUTHENTICATION_TICKET_EXPIRED:               return "auth ticket expired";
        case CauseOf::DISCONNECT_BY_OPERATION_LIMIT:               return "operation limit";
        default:                                                   return "unknown cause";
    }
}

} // namespace

// 📝 At engine scope (not anonymous) so the friend declaration names it; defined only in this unit.
struct PhotonListener : public ExitGames::LoadBalancing::Listener
{
    explicit PhotonListener(PhotonAdapter& Owner_) : Owner(Owner_) {}

    void debugReturn(int, const ExitGames::Common::JString&) override {}
    void connectionErrorReturn(int ErrorCode) override { EmitNumbered("photon_connection_error code=", ErrorCode); }
    void clientErrorReturn(int ErrorCode) override      { EmitNumbered("photon_client_error code=", ErrorCode); }
    void warningReturn(int WarningCode) override        { EmitNumbered("photon_warning code=", WarningCode); }
    void serverErrorReturn(int ErrorCode) override      { EmitNumbered("photon_server_error code=", ErrorCode); }

    void joinRoomEventAction(int PlayerNr, const ExitGames::Common::JVector<int>&, const ExitGames::LoadBalancing::Player&) override
    {
        EmitNumbered("photon_player_joined slot=", PlayerNr);
    }
    void leaveRoomEventAction(int PlayerNr, bool IsInactive) override
    {
        char Line[128] = {};
        std::snprintf(Line, sizeof(Line), "photon_player_left slot=%d inactive=%d", PlayerNr, IsInactive ? 1 : 0);
        Emit(Line);
    }
    void customEventAction(int PlayerNr, nByte EventCode, const ExitGames::Common::Object& Content) override
    {
        if (EventCode != ReplicationEvent) return;
        if (Content.getType() != ExitGames::Common::TypeCode::BYTEARRAY || Content.getDimensions() != 1)
        {
            Emit("photon_packet_refused unexpected_content_type");
            return;
        }
        const ExitGames::Common::ValueObject<nByte*> Array(Content);
        nByte* const*                                BytesAt = Array.getDataAddress();
        const int*                                   Sizes   = Array.getSizes();
        if (BytesAt == nullptr || *BytesAt == nullptr || Sizes == nullptr || Sizes[0] < 0)
        {
            Emit("photon_packet_refused unreadable_content");
            return;
        }
        Owner.AcceptPhotonEvent(PlayerNr, static_cast<const uint8_t*>(*BytesAt), static_cast<size_t>(Sizes[0]));
    }
    void connectReturn(int ErrorCode, const ExitGames::Common::JString&, const ExitGames::Common::JString&,
                       const ExitGames::Common::JString&) override
    {
        Owner.ProgressAt = SteadyNanos();
        if (Owner.Phase != PhotonPhase::Connecting) return;
        if (ErrorCode != 0)
        {
            Owner.FailConnect(ErrorCode, "photon_connect");
            return;
        }
        Emit("photon_auth=success; joining room");
        Owner.Phase = PhotonPhase::Joining;
        ExitGames::LoadBalancing::RoomOptions Options;
        Options.setIsVisible(true);
        Options.setIsOpen(true);
        Options.setMaxPlayers(DevRoomMaxPlayers);
        auto* Session = static_cast<ExitGames::LoadBalancing::Client*>(Owner.Session);
        if (Session == nullptr || !Session->opJoinOrCreateRoom(Owner.PendingRoom, Options)) Owner.FailConnect(0, "photon_join_room_dispatch");
    }
    void disconnectReturn() override
    {
        if (Owner.Phase == PhotonPhase::Idle || Owner.Phase == PhotonPhase::Failed || Owner.Phase == PhotonPhase::Disconnected) return;
        Owner.Phase = PhotonPhase::Disconnected;
        std::snprintf(Owner.Reading, sizeof(Owner.Reading), "Photon: disconnected");
        Emit("photon=disconnected");
    }
    void leaveRoomReturn(int ErrorCode) override { EmitNumbered("photon_leave_room code=", ErrorCode); }
    void joinOrCreateRoomReturn(int, const ExitGames::Common::Hashtable&, const ExitGames::Common::Hashtable&, int ErrorCode,
                                const ExitGames::Common::JString&) override
    {
        Owner.ProgressAt = SteadyNanos();
        if (Owner.Phase != PhotonPhase::Joining) return;
        if (ErrorCode != 0)
        {
            Owner.FailConnect(ErrorCode, "photon_join_room");
            return;
        }
        Owner.Phase = PhotonPhase::Joined;
        Emit("photon_room=joined");
        Owner.SendFirstPacket();
    }

    void Emit(const char* Text)
    {
        if (Owner.Configuration.Diagnostic != nullptr) Owner.Configuration.Diagnostic(Text);
    }
    void EmitNumbered(const char* Stem, int Number)
    {
        char Line[128] = {};
        std::snprintf(Line, sizeof(Line), "%s%d", Stem, Number);
        Emit(Line);
    }

    PhotonAdapter& Owner;
};

#endif // FRONTIER_PHOTON_SDK

bool PhotonAdapter::Begin()
{
#if defined(FRONTIER_PHOTON_SDK)
    End();
    if (Configuration.AuthToken[0] == '\0' || !ValidTokenChars(Configuration.AuthToken))
    {
        std::snprintf(Reading, sizeof(Reading), "Photon: refused (missing or malformed identity token)");
        if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic("photon=refused missing_or_malformed_identity_token");
        return false;
    }
    if (Configuration.OwnershipToken[0] != '\0' && !ValidTokenChars(Configuration.OwnershipToken))
    {
        std::snprintf(Reading, sizeof(Reading), "Photon: refused (malformed ownership token)");
        if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic("photon=refused malformed_ownership_token");
        return false;
    }
    char AppId[128]    = {};
    char AppVersion[64] = {};
    PickSetting(Configuration.AppId, "PHOTON_APP_ID", BuiltInAppId, AppId, sizeof(AppId));
    PickSetting(Configuration.AppVersion, "PHOTON_APP_VERSION", BuiltInAppVersion, AppVersion, sizeof(AppVersion));
    PickSetting(Configuration.Room, "PHOTON_ROOM", BuiltInRoom, PendingRoom, sizeof(PendingRoom));
    if (!ValidAppId(AppId))
    {
        std::snprintf(Reading, sizeof(Reading), "Photon: refused (malformed AppId)");
        if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic("photon=refused malformed_app_id");
        return false;
    }
    if (PendingRoom[0] == '\0')
    {
        std::snprintf(Reading, sizeof(Reading), "Photon: refused (empty room name)");
        if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic("photon=refused empty_room_name");
        return false;
    }
    Phase           = PhotonPhase::Connecting;
    LastCause       = 0;
    FirstPacketSent = false;
    ProgressAt      = SteadyNanos();
    std::snprintf(Reading, sizeof(Reading), "Photon: connecting");
    if (Configuration.Diagnostic != nullptr)
    {
        Configuration.Diagnostic("photon=connecting; Epic authentication in flight, tokens withheld from logs");
    }
    try
    {
        Listener = new PhotonListener(*this);
        Session  = new ExitGames::LoadBalancing::Client(*static_cast<PhotonListener*>(Listener), AppId, AppVersion);
    }
    catch (...)
    {
        delete static_cast<PhotonListener*>(Listener);
        Listener = nullptr;
        Session  = nullptr;
        Phase    = PhotonPhase::Failed;
        std::snprintf(Reading, sizeof(Reading), "Photon: refused (client construction failed)");
        if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic("photon=refused client_construction_failed");
        return false;
    }
    std::string Parameters = "token=";
    Parameters += Configuration.AuthToken;
    if (Configuration.OwnershipToken[0] != '\0')
    {
        Parameters += "&ownershipToken=";
        Parameters += Configuration.OwnershipToken;
    }
    ExitGames::LoadBalancing::AuthenticationValues Auth;
    Auth.setType(ExitGames::LoadBalancing::CustomAuthenticationType::EPIC);
    Auth.setParameters(Parameters.c_str());
    ExitGames::LoadBalancing::ConnectOptions Options;
    Options.setAuthenticationValues(Auth);
    Options.setUseBackgroundSendReceiveThread(false);
    if (!static_cast<ExitGames::LoadBalancing::Client*>(Session)->connect(Options))
    {
        FailConnect(0, "photon_connect_dispatch");
        return false;
    }
    return true;
#else
    std::snprintf(Reading, sizeof(Reading), "Photon: SDK not linked; cloud unavailable");
    return false;
#endif
}

void PhotonAdapter::Tick()
{
#if defined(FRONTIER_PHOTON_SDK)
    auto* Session_ = static_cast<ExitGames::LoadBalancing::Client*>(Session);
    if (Session_ == nullptr) return;
    if (Phase == PhotonPhase::Connecting || Phase == PhotonPhase::Joining)
    {
        const uint64_t Elapsed = SteadyNanos() - ProgressAt;   // [ns]
        if (Elapsed > static_cast<uint64_t>(ConnectTimeoutSeconds) * 1000000000ull)
        {
            Session_->disconnect();
            FailConnect(static_cast<int>(ExitGames::LoadBalancing::DisconnectCause::TIMEOUT_DISCONNECT), "photon_connect");
            return;
        }
    }
    Session_->service();
    if (Phase == PhotonPhase::Joined)
    {
        std::snprintf(Reading, sizeof(Reading), "Photon: in room (sent=%llu received=%llu)",
                      static_cast<unsigned long long>(FramesSent), static_cast<unsigned long long>(FramesReceived));
    }
#endif
}

void PhotonAdapter::End()
{
#if defined(FRONTIER_PHOTON_SDK)
    auto* Session_ = static_cast<ExitGames::LoadBalancing::Client*>(Session);
    if (Session_ != nullptr)
    {
        Session_->disconnect();
        Session_->service();
        delete Session_;
        Session = nullptr;
    }
    delete static_cast<PhotonListener*>(Listener);
    Listener = nullptr;
#endif
    if (Phase != PhotonPhase::Idle)
    {
        Phase = PhotonPhase::Idle;
        std::snprintf(Reading, sizeof(Reading), "Photon: idle");
    }
    PendingRoom[0] = '\0';
    FirstPacketSent = false;
    Incoming.clear();
}

void PhotonAdapter::AcceptPhotonEvent(int Slot, const uint8_t* Bytes, size_t Length)
{
    ReplicationCategory  Category      = ReplicationCategory::Probe;
    const uint8_t*       Payload       = nullptr;
    size_t               PayloadLength = 0;
    if (Bytes == nullptr || !ReplicationCodec::DecodeFrame(Bytes, Length, Category, Payload, PayloadLength))
    {
        ++PacketsRefused;
        if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic("photon_packet_refused bad_codec_frame");
        return;
    }
    ++FramesReceived;
    DecodedFrame Frame;
    Frame.Category = Category;
    Frame.Payload.assign(Payload, Payload + PayloadLength);
    Incoming.push_back(std::move(Frame));
    if (Configuration.Diagnostic != nullptr)
    {
        char Line[160] = {};
        std::snprintf(Line, sizeof(Line), "photon_packet_received bytes=%zu kind=%u slot=%d", Length,
                      static_cast<unsigned>(Category), Slot);
        Configuration.Diagnostic(Line);
    }
}

bool PhotonAdapter::Send(const NetworkMessage& Message)
{
    const std::string Wire = EncodeNetworkMessage(Message);
    std::vector<uint8_t> Frame;
    if (!ReplicationCodec::EncodeFrame(ReplicationCategory::Snapshot, reinterpret_cast<const uint8_t*>(Wire.data()), Wire.size(), Frame))
    {
        ++PacketsRefused;
        return false;
    }
    return SendFrame(Frame.data(), Frame.size());
}

std::vector<NetworkMessage> PhotonAdapter::Receive()
{
    Tick();
    std::vector<NetworkMessage> Messages;
    for (DecodedFrame& Frame : TakeFrames())
    {
        if (Frame.Category != ReplicationCategory::Snapshot) continue;   // [-] - probes land nowhere
        NetworkMessage       Message;
        const std::string_view Wire(reinterpret_cast<const char*>(Frame.Payload.data()), Frame.Payload.size());
        if (!DecodeNetworkMessage(Wire, Message))
        {
            ++PacketsRefused;
            continue;
        }
        Messages.push_back(std::move(Message));
    }
    return Messages;
}

bool PhotonAdapter::SendFrame(const uint8_t* Bytes, size_t Length)
{
#if defined(FRONTIER_PHOTON_SDK)
    Tick();
    auto* Session_ = static_cast<ExitGames::LoadBalancing::Client*>(Session);
    if (Session_ == nullptr || Phase != PhotonPhase::Joined || Bytes == nullptr || Length == 0 ||
        Length > ReplicationCodec::MaxPacket || Session_->getState() != ExitGames::LoadBalancing::PeerStates::Joined)
    {
        return false;
    }
    auto* Payload = reinterpret_cast<nByte*>(const_cast<uint8_t*>(Bytes));
    if (!Session_->opRaiseEvent(true, Payload, static_cast<int>(Length), ReplicationEvent)) return false;
    ++FramesSent;
    return true;
#else
    (void)Bytes;
    (void)Length;
    return false;
#endif
}

std::vector<PhotonAdapter::DecodedFrame> PhotonAdapter::TakeFrames()
{
#if defined(FRONTIER_PHOTON_SDK)
    Tick();
#endif
    std::vector<DecodedFrame> Frames;
    Frames.reserve(Incoming.size());
    while (!Incoming.empty())
    {
        Frames.push_back(std::move(Incoming.front()));
        Incoming.pop_front();
    }
    return Frames;
}

#if defined(FRONTIER_PHOTON_SDK)

void PhotonAdapter::FailConnect(int Cause, const char* What)
{
    LastCause = Cause;
    Phase     = PhotonPhase::Failed;
    std::snprintf(Reading, sizeof(Reading), "Photon: %s failed (%s, cause=%d)", What, DescribeCause(Cause), Cause);
    if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic(Reading);
}

void PhotonAdapter::SendFirstPacket()
{
    static constexpr char Tag[] = "frontier-photon-v1";
    std::vector<uint8_t> Frame;
    if (!ReplicationCodec::EncodeFrame(ReplicationCategory::Probe, reinterpret_cast<const uint8_t*>(Tag), sizeof(Tag) - 1, Frame))
    {
        if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic("photon_first_packet_refused codec_error");
        return;
    }
    auto* Session_ = static_cast<ExitGames::LoadBalancing::Client*>(Session);
    if (Session_ == nullptr || Phase != PhotonPhase::Joined ||
        Session_->getState() != ExitGames::LoadBalancing::PeerStates::Joined)
    {
        if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic("photon_first_packet_refused not_joined");
        return;
    }
    auto* Bytes = reinterpret_cast<nByte*>(Frame.data());
    if (!Session_->opRaiseEvent(true, Bytes, static_cast<int>(Frame.size()), ReplicationEvent))
    {
        if (Configuration.Diagnostic != nullptr) Configuration.Diagnostic("photon_first_packet_refused raise_failed");
        return;
    }
    ++FramesSent;
    FirstPacketSent = true;
    if (Configuration.Diagnostic != nullptr)
    {
        char Line[128] = {};
        std::snprintf(Line, sizeof(Line), "photon_first_packet_sent bytes=%zu", Frame.size());
        Configuration.Diagnostic(Line);
    }
}

#endif // FRONTIER_PHOTON_SDK

} // namespace Frontier
