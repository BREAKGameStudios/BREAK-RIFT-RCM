#include "RiftConnectionSubsystem.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RiftConnectionManager
{
    constexpr int32 SupportedProtocolVersion = 1;

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
    bSessionLoading = false;
    OnSessionChanged.Clear();
    OnConnectionError.Clear();
    Super::Deinitialize();
}

void URiftConnectionSubsystem::RequestFreshSession()
{
    const uint64 RequestGeneration = ++SessionRequestGeneration;
    bSessionLoading = true;
    FetchSession(RequestGeneration);
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
        FPlatformMisc::GetEnvironmentVariable(TEXT("RIFT_LAUNCH_TOKEN"));

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
