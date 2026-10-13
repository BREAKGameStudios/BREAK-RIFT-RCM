#include "RiftConnectionSubsystem.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace RiftConnectionManager
{
    constexpr int32 SupportedProtocolVersion = 1;

    FString LaunchToken()
    {
        FString Token = FPlatformMisc::GetEnvironmentVariable(
            TEXT("RIFT_LAUNCH_TOKEN")
        );

        if (Token.IsEmpty())
        {
            FParse::Value(
                FCommandLine::Get(),
                TEXT("RiftLaunchToken="),
                Token
            );
        }

        return Token.TrimStartAndEnd();
    }

    FRiftConnectionError ParseErrorResponse(
        const FString& ResponseBody,
        const FString& FallbackCode,
        const FString& FallbackMessage
    )
    {
        FRiftConnectionError Result;
        Result.Code = FallbackCode;
        Result.Message = FallbackMessage;

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader =
            TJsonReaderFactory<>::Create(ResponseBody);

        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            return Result;
        }

        const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
        if (!Root->TryGetObjectField(TEXT("error"), ErrorObject) ||
            ErrorObject == nullptr ||
            !ErrorObject->IsValid())
        {
            return Result;
        }

        (*ErrorObject)->TryGetStringField(TEXT("code"), Result.Code);
        (*ErrorObject)->TryGetStringField(TEXT("message"), Result.Message);
        return Result;
    }
}

void URiftConnectionSubsystem::Initialize(
    FSubsystemCollectionBase& Collection
)
{
    Super::Initialize(Collection);
    RequestFreshSession();
}

void URiftConnectionSubsystem::Deinitialize()
{
    ++SessionRequestGeneration;
    ++HostSessionRequestGeneration;
    ++CloseHostSessionRequestGeneration;
    ++FindGameSessionsRequestGeneration;

    bSessionLoading = false;
    bHostSessionRequestLoading = false;
    bCloseHostSessionRequestLoading = false;
    bFindGameSessionsLoading = false;

    HostedSession =
        FRiftGameSession();

    FoundGameSessions.Reset();

    OnSessionChanged.Clear();
    OnConnectionError.Clear();

    OnHostSessionCreated.Clear();
    OnHostSessionError.Clear();
    OnHostSessionClosed.Clear();
    OnHostSessionCloseError.Clear();

    OnGameSessionsFound.Clear();
    OnGameSessionsError.Clear();

    Super::Deinitialize();
}

void URiftConnectionSubsystem::RequestFreshSession()
{
    const uint64 RequestGeneration = ++SessionRequestGeneration;
    bSessionLoading = true;
    FetchSession(RequestGeneration);
}

void URiftConnectionSubsystem::CreateHostSession(
    const FRiftHostSessionRequest& Request
)
{
    const uint64 RequestGeneration = ++HostSessionRequestGeneration;
    bHostSessionRequestLoading = true;
    SendCreateHostSession(RequestGeneration, Request);
}

void URiftConnectionSubsystem::CloseHostedSession()
{
    const uint64 RequestGeneration =
        ++CloseHostSessionRequestGeneration;

    bCloseHostSessionRequestLoading = true;

    SendCloseHostedSession(
        RequestGeneration
    );
}

void URiftConnectionSubsystem::FindGameSessions()
{
    const uint64 RequestGeneration =
        ++FindGameSessionsRequestGeneration;

    bFindGameSessionsLoading = true;

    SendFindGameSessions(
        RequestGeneration
    );
}

void URiftConnectionSubsystem::SendFindGameSessions(
    uint64 RequestGeneration
)
{
    int32 ProtocolVersion = 0;
    int32 LauncherPort = 0;
    FString LauncherSecret;
    FString ConfigError;

    if (!ReadLauncherConfig(
            ProtocolVersion,
            LauncherPort,
            LauncherSecret,
            ConfigError))
    {
        FailFindGameSessions(
            RequestGeneration,
            TEXT("launcher_unavailable"),
            ConfigError
        );
        return;
    }

    if (
        ProtocolVersion !=
        RiftConnectionManager::SupportedProtocolVersion
        )
    {
        FailFindGameSessions(
            RequestGeneration,
            TEXT("unsupported_protocol"),
            FString::Printf(
                TEXT(
                    "RIFT Local Protocol %d is not supported by this SDK."
                ),
                ProtocolVersion
            )
        );
        return;
    }

    const FString LaunchToken =
        RiftConnectionManager::LaunchToken();

    if (LaunchToken.IsEmpty())
    {
        FailFindGameSessions(
            RequestGeneration,
            TEXT("launch_token_required"),
            TEXT(
                "The game must be started by the RIFT Launcher to find RIFT sessions."
            )
        );
        return;
    }

    const FString Url =
        FString::Printf(
            TEXT(
                "http://127.0.0.1:%d/game-sessions"
            ),
            LauncherPort
        );

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
        FHttpModule::Get().CreateRequest();

    HttpRequest->SetVerb(TEXT("GET"));
    HttpRequest->SetURL(Url);
    HttpRequest->SetHeader(
        TEXT("Accept"),
        TEXT("application/json")
    );
    HttpRequest->SetHeader(
        TEXT("X-Rift-Secret"),
        LauncherSecret
    );
    HttpRequest->SetHeader(
        TEXT("X-Rift-Launch-Token"),
        LaunchToken
    );

    TWeakObjectPtr<URiftConnectionSubsystem> WeakThis(this);

    HttpRequest->OnProcessRequestComplete().BindLambda(
        [WeakThis, RequestGeneration](
            FHttpRequestPtr,
            FHttpResponsePtr Response,
            bool bSucceeded
        )
        {
            if (!WeakThis.IsValid())
            {
                return;
            }

            URiftConnectionSubsystem* Subsystem =
                WeakThis.Get();

            if (
                RequestGeneration !=
                Subsystem->FindGameSessionsRequestGeneration
                )
            {
                return;
            }

            if (!bSucceeded || !Response.IsValid())
            {
                Subsystem->FailFindGameSessions(
                    RequestGeneration,
                    TEXT("launcher_unavailable"),
                    TEXT(
                        "Could not connect to the RIFT Launcher."
                    )
                );
                return;
            }

            const int32 StatusCode =
                Response->GetResponseCode();

            const FString ResponseBody =
                Response->GetContentAsString();

            if (StatusCode < 200 || StatusCode >= 300)
            {
                const FRiftConnectionError Error =
                    RiftConnectionManager::ParseErrorResponse(
                        ResponseBody,
                        TEXT("find_game_sessions_failed"),
                        FString::Printf(
                            TEXT(
                                "RIFT Launcher returned HTTP %d."
                            ),
                            StatusCode
                        )
                    );

                Subsystem->FailFindGameSessions(
                    RequestGeneration,
                    Error.Code,
                    Error.Message
                );
                return;
            }

            TArray<FRiftGameSession> Sessions;
            FRiftConnectionError Error;

            if (!Subsystem->ParseGameSessionsResponse(
                    ResponseBody,
                    Sessions,
                    Error))
            {
                Subsystem->FailFindGameSessions(
                    RequestGeneration,
                    Error.Code,
                    Error.Message
                );
                return;
            }

            Subsystem->CompleteFindGameSessions(
                RequestGeneration,
                Sessions
            );
        }
    );

    if (!HttpRequest->ProcessRequest())
    {
        FailFindGameSessions(
            RequestGeneration,
            TEXT("launcher_unavailable"),
            TEXT(
                "Could not start the request to the RIFT Launcher."
            )
        );
    }
}

void URiftConnectionSubsystem::CompleteFindGameSessions(
    uint64 RequestGeneration,
    const TArray<FRiftGameSession>& Sessions
)
{
    if (
        RequestGeneration !=
        FindGameSessionsRequestGeneration
        )
    {
        return;
    }

    bFindGameSessionsLoading = false;
    FoundGameSessions = Sessions;

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "RCM: found %d compatible game session(s)"
        ),
        FoundGameSessions.Num()
    );

        UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "RCM: broadcasting game sessions listeners=%s subsystem=%s"
        ),
        OnGameSessionsFound.IsBound()
        ? TEXT("true")
        : TEXT("false"),
        *GetName()
    );

    OnGameSessionsFound.Broadcast(
        FoundGameSessions
    );
}

void URiftConnectionSubsystem::FailFindGameSessions(
    uint64 RequestGeneration,
    const FString& Code,
    const FString& Message
)
{
    if (
        RequestGeneration !=
        FindGameSessionsRequestGeneration
        )
    {
        return;
    }

    bFindGameSessionsLoading = false;
    FoundGameSessions.Reset();

    UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "RCM: find game sessions failed code=%s message=%s"
        ),
        *Code,
        *Message
    );

    FRiftConnectionError Error;
    Error.Code = Code;
    Error.Message = Message;

    OnGameSessionsError.Broadcast(
        Error
    );
}

void URiftConnectionSubsystem::SendCreateHostSession(
    uint64 RequestGeneration,
    const FRiftHostSessionRequest& HostRequest
)
{
    int32 ProtocolVersion = 0;
    int32 LauncherPort = 0;
    FString LauncherSecret;
    FString ConfigError;

    if (!ReadLauncherConfig(
            ProtocolVersion,
            LauncherPort,
            LauncherSecret,
            ConfigError))
    {
        FailHostSessionRequest(
            RequestGeneration,
            TEXT("launcher_unavailable"),
            ConfigError
        );
        return;
    }

    if (ProtocolVersion != RiftConnectionManager::SupportedProtocolVersion)
    {
        FailHostSessionRequest(
            RequestGeneration,
            TEXT("unsupported_protocol"),
            FString::Printf(
                TEXT("RIFT Local Protocol %d is not supported by this SDK."),
                ProtocolVersion
            )
        );
        return;
    }

    const FString LaunchToken =
        RiftConnectionManager::LaunchToken();

    if (LaunchToken.IsEmpty())
    {
        FailHostSessionRequest(
            RequestGeneration,
            TEXT("launch_token_required"),
            TEXT("The game must be started by the RIFT Launcher to host a RIFT session.")
        );
        return;
    }

    const FString ConnectionString = HostRequest.ConnectionString.TrimStartAndEnd();
    const FString Visibility = HostRequest.Visibility.TrimStartAndEnd().ToLower();

    if (ConnectionString.IsEmpty())
    {
        FailHostSessionRequest(
            RequestGeneration,
            TEXT("connection_string_required"),
            TEXT("A connection string is required to host a RIFT session.")
        );
        return;
    }

    if (Visibility != TEXT("private") &&
        Visibility != TEXT("friends") &&
        Visibility != TEXT("public"))
    {
        FailHostSessionRequest(
            RequestGeneration,
            TEXT("invalid_visibility"),
            TEXT("Visibility must be private, friends, or public.")
        );
        return;
    }

    if (HostRequest.MaxPlayers < 1 || HostRequest.MaxPlayers > 128)
    {
        FailHostSessionRequest(
            RequestGeneration,
            TEXT("invalid_max_players"),
            TEXT("MaxPlayers must be between 1 and 128.")
        );
        return;
    }

    TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("connection_string"), ConnectionString);
    Body->SetStringField(TEXT("map_id"), HostRequest.MapId.TrimStartAndEnd());
    Body->SetStringField(TEXT("mode_id"), HostRequest.ModeId.TrimStartAndEnd());
    Body->SetStringField(TEXT("visibility"), Visibility);
    Body->SetNumberField(TEXT("max_players"), HostRequest.MaxPlayers);

    FString BodyText;
    const TSharedRef<TJsonWriter<>> Writer =
        TJsonWriterFactory<>::Create(&BodyText);

    if (!FJsonSerializer::Serialize(Body, Writer))
    {
        FailHostSessionRequest(
            RequestGeneration,
            TEXT("serialization_failed"),
            TEXT("Could not serialize the host session request.")
        );
        return;
    }

    const FString Url = FString::Printf(
        TEXT("http://127.0.0.1:%d/game-sessions"),
        LauncherPort
    );

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
        FHttpModule::Get().CreateRequest();

    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetURL(Url);
    HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    HttpRequest->SetHeader(TEXT("X-Rift-Secret"), LauncherSecret);
    HttpRequest->SetHeader(TEXT("X-Rift-Launch-Token"), LaunchToken);
    HttpRequest->SetContentAsString(BodyText);

    TWeakObjectPtr<URiftConnectionSubsystem> WeakThis(this);

    HttpRequest->OnProcessRequestComplete().BindLambda(
        [WeakThis, RequestGeneration](
            FHttpRequestPtr,
            FHttpResponsePtr Response,
            bool bSucceeded)
        {
            if (!WeakThis.IsValid())
            {
                return;
            }

            URiftConnectionSubsystem* Subsystem = WeakThis.Get();

            if (RequestGeneration != Subsystem->HostSessionRequestGeneration)
            {
                return;
            }

            if (!bSucceeded || !Response.IsValid())
            {
                Subsystem->FailHostSessionRequest(
                    RequestGeneration,
                    TEXT("launcher_unavailable"),
                    TEXT("Could not connect to the RIFT Launcher.")
                );
                return;
            }

            const int32 StatusCode = Response->GetResponseCode();
            const FString ResponseBody = Response->GetContentAsString();

            if (StatusCode < 200 || StatusCode >= 300)
            {
                const FRiftConnectionError Error =
                    RiftConnectionManager::ParseErrorResponse(
                        ResponseBody,
                        TEXT("host_session_failed"),
                        FString::Printf(
                            TEXT("RIFT Launcher returned HTTP %d."),
                            StatusCode
                        )
                    );

                Subsystem->FailHostSessionRequest(
                    RequestGeneration,
                    Error.Code,
                    Error.Message
                );
                return;
            }

            FRiftGameSession Session;
            FRiftConnectionError Error;

            if (!Subsystem->ParseGameSessionResponse(
                    ResponseBody,
                    Session,
                    Error))
            {
                Subsystem->FailHostSessionRequest(
                    RequestGeneration,
                    Error.Code,
                    Error.Message
                );
                return;
            }

            Subsystem->CompleteHostSessionRequest(
                RequestGeneration,
                Session
            );
        }
    );

    if (!HttpRequest->ProcessRequest())
    {
        FailHostSessionRequest(
            RequestGeneration,
            TEXT("launcher_unavailable"),
            TEXT("Could not start the request to the RIFT Launcher.")
        );
    }
}

void URiftConnectionSubsystem::SendCloseHostedSession(
    uint64 RequestGeneration
)
{
    if (HostedSession.Id.IsEmpty())
    {
        FailCloseHostedSession(
            RequestGeneration,
            TEXT("host_session_unavailable"),
            TEXT("There is no hosted RIFT session to close.")
        );
        return;
    }

    int32 ProtocolVersion = 0;
    int32 LauncherPort = 0;
    FString LauncherSecret;
    FString ConfigError;

    if (!ReadLauncherConfig(
            ProtocolVersion,
            LauncherPort,
            LauncherSecret,
            ConfigError))
    {
        FailCloseHostedSession(
            RequestGeneration,
            TEXT("launcher_unavailable"),
            ConfigError
        );
        return;
    }

    if (
        ProtocolVersion !=
        RiftConnectionManager::SupportedProtocolVersion
        )
    {
        FailCloseHostedSession(
            RequestGeneration,
            TEXT("unsupported_protocol"),
            FString::Printf(
                TEXT(
                    "RIFT Local Protocol %d is not supported by this SDK."
                ),
                ProtocolVersion
            )
        );
        return;
    }

    const FString LaunchToken =
        RiftConnectionManager::LaunchToken();

    if (LaunchToken.IsEmpty())
    {
        FailCloseHostedSession(
            RequestGeneration,
            TEXT("launch_token_required"),
            TEXT(
                "The game must be started by the RIFT Launcher "
                "to close a hosted RIFT session."
            )
        );
        return;
    }

    const FString Url =
        FString::Printf(
            TEXT(
                "http://127.0.0.1:%d/game-sessions/close"
            ),
            LauncherPort
        );

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
        FHttpModule::Get().CreateRequest();

    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetURL(Url);
    HttpRequest->SetHeader(
        TEXT("Accept"),
        TEXT("application/json")
    );
    HttpRequest->SetHeader(
        TEXT("Content-Type"),
        TEXT("application/json")
    );
    HttpRequest->SetHeader(
        TEXT("X-Rift-Secret"),
        LauncherSecret
    );
    HttpRequest->SetHeader(
        TEXT("X-Rift-Launch-Token"),
        LaunchToken
    );
    HttpRequest->SetContentAsString(TEXT("{}"));

    TWeakObjectPtr<URiftConnectionSubsystem> WeakThis(this);

    HttpRequest->OnProcessRequestComplete().BindLambda(
        [WeakThis, RequestGeneration](
            FHttpRequestPtr,
            FHttpResponsePtr Response,
            bool bSucceeded
            )
        {
            if (!WeakThis.IsValid())
            {
                return;
            }

            URiftConnectionSubsystem* Subsystem =
                WeakThis.Get();

            if (
                RequestGeneration !=
                Subsystem->CloseHostSessionRequestGeneration
                )
            {
                return;
            }

            if (!bSucceeded || !Response.IsValid())
            {
                Subsystem->FailCloseHostedSession(
                    RequestGeneration,
                    TEXT("launcher_unavailable"),
                    TEXT(
                        "Could not connect to the RIFT Launcher."
                    )
                );
                return;
            }

            const int32 StatusCode =
                Response->GetResponseCode();

            const FString ResponseBody =
                Response->GetContentAsString();

            if (
                StatusCode < 200 ||
                StatusCode >= 300
                )
            {
                const FRiftConnectionError Error =
                    RiftConnectionManager::ParseErrorResponse(
                        ResponseBody,
                        TEXT("host_session_close_failed"),
                        FString::Printf(
                            TEXT(
                                "RIFT Launcher returned HTTP %d."
                            ),
                            StatusCode
                        )
                    );

                Subsystem->FailCloseHostedSession(
                    RequestGeneration,
                    Error.Code,
                    Error.Message
                );
                return;
            }

            Subsystem->CompleteCloseHostedSession(
                RequestGeneration
            );
        }
    );

    if (!HttpRequest->ProcessRequest())
    {
        FailCloseHostedSession(
            RequestGeneration,
            TEXT("launcher_unavailable"),
            TEXT(
                "Could not start the close request "
                "to the RIFT Launcher."
            )
        );
    }
}

void URiftConnectionSubsystem::CompleteHostSessionRequest(
    uint64 RequestGeneration,
    const FRiftGameSession& Session
)
{
    if (RequestGeneration != HostSessionRequestGeneration)
    {
        return;
    }

    bHostSessionRequestLoading = false;
    HostedSession = Session;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("RCM: host session created id=%s game_slug=%s players=%d/%d"),
        *HostedSession.Id,
        *HostedSession.GameSlug,
        HostedSession.PlayerCount,
        HostedSession.MaxPlayers
    );

    OnHostSessionCreated.Broadcast(HostedSession);
}

void URiftConnectionSubsystem::FailHostSessionRequest(
    uint64 RequestGeneration,
    const FString& Code,
    const FString& Message
)
{
    if (RequestGeneration != HostSessionRequestGeneration)
    {
        return;
    }

    bHostSessionRequestLoading = false;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("RCM: host session request failed code=%s message=%s"),
        *Code,
        *Message
    );

    FRiftConnectionError Error;
    Error.Code = Code;
    Error.Message = Message;
    OnHostSessionError.Broadcast(Error);
}

void URiftConnectionSubsystem::CompleteCloseHostedSession(
    uint64 RequestGeneration
)
{
    if (
        RequestGeneration !=
        CloseHostSessionRequestGeneration
        )
    {
        return;
    }

    bCloseHostSessionRequestLoading = false;

    const FString ClosedSessionId =
        HostedSession.Id;

    HostedSession =
        FRiftGameSession();

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "RCM: hosted session closed id=%s"
        ),
        *ClosedSessionId
    );

    OnHostSessionClosed.Broadcast();
}

void URiftConnectionSubsystem::FailCloseHostedSession(
    uint64 RequestGeneration,
    const FString& Code,
    const FString& Message
)
{
    if (
        RequestGeneration !=
        CloseHostSessionRequestGeneration
        )
    {
        return;
    }

    bCloseHostSessionRequestLoading = false;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "RCM: host session close failed code=%s message=%s"
        ),
        *Code,
        *Message
    );

    FRiftConnectionError Error;
    Error.Code = Code;
    Error.Message = Message;

    OnHostSessionCloseError.Broadcast(
        Error
    );
}

bool URiftConnectionSubsystem::ParseGameSessionResponse(
    const FString& ResponseBody,
    FRiftGameSession& OutSession,
    FRiftConnectionError& OutError
) const
{
    OutError.Code = TEXT("invalid_game_session_response");
    OutError.Message = TEXT("RIFT Launcher returned an invalid game session response.");

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(ResponseBody);

    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    if (!Root->TryGetStringField(TEXT("id"), OutSession.Id) ||
        !Root->TryGetStringField(TEXT("game_slug"), OutSession.GameSlug) ||
        !Root->TryGetStringField(TEXT("build_id"), OutSession.BuildId) ||
        !Root->TryGetStringField(TEXT("connection_string"), OutSession.ConnectionString) ||
        OutSession.Id.IsEmpty() ||
        OutSession.GameSlug.IsEmpty() ||
        OutSession.BuildId.IsEmpty() ||
        OutSession.ConnectionString.IsEmpty())
    {
        return false;
    }

    Root->TryGetStringField(TEXT("host_public_id"), OutSession.HostPublicId);
    Root->TryGetStringField(TEXT("host_username"), OutSession.HostUsername);
    Root->TryGetStringField(TEXT("state"), OutSession.State);
    Root->TryGetStringField(TEXT("visibility"), OutSession.Visibility);
    Root->TryGetStringField(TEXT("map_id"), OutSession.MapId);
    Root->TryGetStringField(TEXT("mode_id"), OutSession.ModeId);
    Root->TryGetBoolField(TEXT("joinable"), OutSession.bJoinable);
    Root->TryGetNumberField(TEXT("max_players"), OutSession.MaxPlayers);
    Root->TryGetNumberField(TEXT("player_count"), OutSession.PlayerCount);
    Root->TryGetNumberField(TEXT("revision"), OutSession.Revision);

    return true;
}

bool URiftConnectionSubsystem::ParseGameSessionsResponse(
    const FString& ResponseBody,
    TArray<FRiftGameSession>& OutSessions,
    FRiftConnectionError& OutError
) const
{
    OutSessions.Reset();

    OutError.Code =
        TEXT("invalid_game_sessions_response");

    OutError.Message =
        TEXT(
            "RIFT Launcher returned an invalid game sessions response."
        );

    TSharedPtr<FJsonObject> Root;

    const TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(
            ResponseBody
        );

    if (
        !FJsonSerializer::Deserialize(
            Reader,
            Root
        ) ||
        !Root.IsValid()
        )
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* SessionsArray =
        nullptr;

    if (
        !Root->TryGetArrayField(
            TEXT("sessions"),
            SessionsArray
        ) ||
        SessionsArray == nullptr
        )
    {
        return false;
    }

    for (
        const TSharedPtr<FJsonValue>& SessionValue :
        *SessionsArray
        )
    {
        if (!SessionValue.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonObject> SessionObject =
            SessionValue->AsObject();

        if (!SessionObject.IsValid())
        {
            return false;
        }

        FString SessionJson;

        const TSharedRef<TJsonWriter<>> Writer =
            TJsonWriterFactory<>::Create(
                &SessionJson
            );

        if (
            !FJsonSerializer::Serialize(
                SessionObject.ToSharedRef(),
                Writer
            )
            )
        {
            return false;
        }

        FRiftGameSession Session;
        FRiftConnectionError SessionError;

        if (
            !ParseGameSessionResponse(
                SessionJson,
                Session,
                SessionError
            )
            )
        {
            OutError = SessionError;
            return false;
        }

        OutSessions.Add(
            MoveTemp(Session)
        );
    }

    return true;
}

void URiftConnectionSubsystem::FetchSession(uint64 RequestGeneration)
{
    int32 ProtocolVersion = 0;
    int32 LauncherPort = 0;
    FString LauncherSecret;
    FString ConfigError;

    if (!ReadLauncherConfig(
            ProtocolVersion,
            LauncherPort,
            LauncherSecret,
            ConfigError))
    {
        FailSessionRequest(
            RequestGeneration,
            TEXT("launcher_unavailable"),
            ConfigError
        );
        return;
    }

    if (ProtocolVersion != RiftConnectionManager::SupportedProtocolVersion)
    {
        FailSessionRequest(
            RequestGeneration,
            TEXT("unsupported_protocol"),
            FString::Printf(
                TEXT("RIFT Local Protocol %d is not supported by this SDK."),
                ProtocolVersion
            )
        );
        return;
    }

    const FString Url = FString::Printf(
        TEXT("http://127.0.0.1:%d/session"),
        LauncherPort
    );

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
        FHttpModule::Get().CreateRequest();

    Request->SetVerb(TEXT("GET"));
    Request->SetURL(Url);
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("X-Rift-Secret"), LauncherSecret);

    const FString LaunchToken =
        RiftConnectionManager::LaunchToken();

    if (!LaunchToken.IsEmpty())
    {
        Request->SetHeader(TEXT("X-Rift-Launch-Token"), LaunchToken);
    }

    TWeakObjectPtr<URiftConnectionSubsystem> WeakThis(this);

    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, RequestGeneration](
            FHttpRequestPtr,
            FHttpResponsePtr Response,
            bool bSucceeded)
        {
            if (!WeakThis.IsValid())
            {
                return;
            }

            URiftConnectionSubsystem* Subsystem = WeakThis.Get();

            if (RequestGeneration != Subsystem->SessionRequestGeneration)
            {
                return;
            }

            if (!bSucceeded || !Response.IsValid())
            {
                Subsystem->FailSessionRequest(
                    RequestGeneration,
                    TEXT("launcher_unavailable"),
                    TEXT("Could not connect to the RIFT Launcher.")
                );
                return;
            }

            const int32 StatusCode = Response->GetResponseCode();
            const FString ResponseBody = Response->GetContentAsString();

            if (StatusCode < 200 || StatusCode >= 300)
            {
                const FRiftConnectionError Error =
                    RiftConnectionManager::ParseErrorResponse(
                        ResponseBody,
                        TEXT("launcher_error"),
                        FString::Printf(
                            TEXT("RIFT Launcher returned HTTP %d."),
                            StatusCode
                        )
                    );

                Subsystem->FailSessionRequest(
                    RequestGeneration,
                    Error.Code,
                    Error.Message
                );
                return;
            }

            FRiftSession Session;
            FRiftConnectionError Error;

            if (!Subsystem->ParseSessionResponse(
                    ResponseBody,
                    Session,
                    Error))
            {
                Subsystem->FailSessionRequest(
                    RequestGeneration,
                    Error.Code,
                    Error.Message
                );
                return;
            }

            Subsystem->CompleteSessionRequest(
                RequestGeneration,
                Session
            );
        }
    );

    if (!Request->ProcessRequest())
    {
        FailSessionRequest(
            RequestGeneration,
            TEXT("launcher_unavailable"),
            TEXT("Could not start the request to the RIFT Launcher.")
        );
    }
}

void URiftConnectionSubsystem::CompleteSessionRequest(
    uint64 RequestGeneration,
    const FRiftSession& Session
)
{
    if (RequestGeneration != SessionRequestGeneration)
    {
        return;
    }

    bSessionLoading = false;
    CachedSession = Session;

    UE_LOG(
        LogTemp,
        Log,
        TEXT(
            "RCM: session received authenticated=%s username=%s has_game=%s game_slug=%s build_id=%s"
        ),
        CachedSession.bAuthenticated ? TEXT("true") : TEXT("false"),
        *CachedSession.Username,
        CachedSession.bHasGame ? TEXT("true") : TEXT("false"),
        *CachedSession.Game.Slug,
        *CachedSession.Game.BuildId
    );

    OnSessionChanged.Broadcast(CachedSession);
}

void URiftConnectionSubsystem::FailSessionRequest(
    uint64 RequestGeneration,
    const FString& Code,
    const FString& Message
)
{
    if (RequestGeneration != SessionRequestGeneration)
    {
        return;
    }

    bSessionLoading = false;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("RCM: session request failed code=%s message=%s"),
        *Code,
        *Message
    );

    FRiftConnectionError Error;
    Error.Code = Code;
    Error.Message = Message;
    OnConnectionError.Broadcast(Error);
}

bool URiftConnectionSubsystem::ReadLauncherConfig(
    int32& OutProtocolVersion,
    int32& OutPort,
    FString& OutSecret,
    FString& OutError
) const
{
    OutProtocolVersion = 0;
    OutPort = 0;
    OutSecret.Empty();
    OutError.Empty();

    const FString AppData =
        FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));

    if (AppData.IsEmpty())
    {
        OutError = TEXT("APPDATA is not available.");
        return false;
    }

    const FString ConfigPath = FPaths::Combine(
        AppData,
        TEXT("Rift"),
        TEXT("launcher.json")
    );

    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath))
    {
        OutError = TEXT("RIFT Launcher configuration was not found.");
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(JsonText);

    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutError = TEXT("RIFT Launcher configuration contains invalid JSON.");
        return false;
    }

    if (!Root->TryGetNumberField(TEXT("protocol_version"), OutProtocolVersion) ||
        !Root->TryGetNumberField(TEXT("port"), OutPort) ||
        !Root->TryGetStringField(TEXT("secret"), OutSecret) ||
        OutPort < 1 ||
        OutPort > 65535 ||
        OutSecret.IsEmpty())
    {
        OutError = TEXT("RIFT Launcher configuration is incomplete.");
        return false;
    }

    return true;
}

bool URiftConnectionSubsystem::ParseSessionResponse(
    const FString& ResponseBody,
    FRiftSession& OutSession,
    FRiftConnectionError& OutError
) const
{
    OutError.Code = TEXT("invalid_response");
    OutError.Message = TEXT("RIFT Launcher returned an invalid session response.");

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(ResponseBody);

    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    if (!Root->TryGetNumberField(
            TEXT("protocol_version"),
            OutSession.ProtocolVersion) ||
        !Root->TryGetBoolField(
            TEXT("authenticated"),
            OutSession.bAuthenticated))
    {
        return false;
    }

    if (OutSession.ProtocolVersion !=
        RiftConnectionManager::SupportedProtocolVersion)
    {
        OutError.Code = TEXT("unsupported_protocol");
        OutError.Message = TEXT("RIFT Launcher uses an unsupported protocol version.");
        return false;
    }

    if (!OutSession.bAuthenticated)
    {
        return true;
    }

    if (!Root->TryGetStringField(TEXT("public_id"), OutSession.PublicId) ||
        !Root->TryGetStringField(TEXT("username"), OutSession.Username) ||
        OutSession.PublicId.IsEmpty() ||
        OutSession.Username.IsEmpty())
    {
        return false;
    }

    Root->TryGetStringField(TEXT("ticket"), OutSession.Ticket);

    FString ExpiresAt;
    if (Root->TryGetStringField(TEXT("ticket_expires_at"), ExpiresAt) &&
        !ExpiresAt.IsEmpty() &&
        !FDateTime::ParseIso8601(*ExpiresAt, OutSession.TicketExpiresAt))
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* GameObject = nullptr;
    if (Root->TryGetObjectField(TEXT("game"), GameObject) &&
        GameObject != nullptr &&
        GameObject->IsValid())
    {
        OutSession.bHasGame =
            (*GameObject)->TryGetStringField(
                TEXT("slug"),
                OutSession.Game.Slug) &&
            (*GameObject)->TryGetStringField(
                TEXT("build_id"),
                OutSession.Game.BuildId) &&
            !OutSession.Game.Slug.IsEmpty() &&
            !OutSession.Game.BuildId.IsEmpty();

        if (!OutSession.bHasGame)
        {
            return false;
        }
    }

    const TSharedPtr<FJsonObject>* JoinObject = nullptr;
    if (Root->TryGetObjectField(TEXT("join_context"), JoinObject) &&
        JoinObject != nullptr &&
        JoinObject->IsValid())
    {
        OutSession.bHasJoinContext =
            (*JoinObject)->TryGetStringField(
                TEXT("context_id"),
                OutSession.JoinContext.ContextId) &&
            (*JoinObject)->TryGetStringField(
                TEXT("session_id"),
                OutSession.JoinContext.SessionId) &&
            (*JoinObject)->TryGetStringField(
                TEXT("required_build_id"),
                OutSession.JoinContext.RequiredBuildId);

        (*JoinObject)->TryGetStringField(
            TEXT("invitation_id"),
            OutSession.JoinContext.InvitationId);

        if (!OutSession.bHasJoinContext)
        {
            return false;
        }
    }

    return true;
}

bool URiftConnectionSubsystem::GetLauncherEndpoint(
    int32& OutPort, FString& OutSecret, FString& OutError) const
{
    int32 Version = 0;
    if (!ReadLauncherConfig(Version, OutPort, OutSecret, OutError)) return false;
    if (Version != RiftConnectionManager::SupportedProtocolVersion) {
        OutError = TEXT("Unsupported launcher protocol.");
        return false;
    }
    return true;
}
