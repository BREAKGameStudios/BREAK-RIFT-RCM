#pragma once

#include "CoreMinimal.h"
#include "RiftConnectionTypes.generated.h"

USTRUCT(BlueprintType)
struct RIFTCONNECTIONMANAGER_API FRiftGameIdentity
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString Slug;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString BuildId;
};

USTRUCT(BlueprintType)
struct RIFTCONNECTIONMANAGER_API FRiftJoinContext
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString ContextId;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString SessionId;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString InvitationId;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString RequiredBuildId;
};

USTRUCT(BlueprintType)
struct RIFTCONNECTIONMANAGER_API FRiftConnectionError
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString Code;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString Message;
};

USTRUCT(BlueprintType)
struct RIFTCONNECTIONMANAGER_API FRiftSession
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    int32 ProtocolVersion = 1;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    bool bAuthenticated = false;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString PublicId;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString Username;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString Ticket;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FDateTime TicketExpiresAt;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    bool bHasGame = false;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FRiftGameIdentity Game;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    bool bHasJoinContext = false;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FRiftJoinContext JoinContext;
};

USTRUCT(BlueprintType)
struct RIFTCONNECTIONMANAGER_API FRiftHostSessionRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RIFT")
    FString ConnectionString;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RIFT")
    FString MapId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RIFT")
    FString ModeId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RIFT")
    FString Visibility = TEXT("private");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RIFT",
        meta = (ClampMin = "1", ClampMax = "128"))
    int32 MaxPlayers = 10;
};

USTRUCT(BlueprintType)
struct RIFTCONNECTIONMANAGER_API FRiftGameSession
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString Id;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString GameSlug;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString BuildId;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString HostPublicId;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString HostUsername;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString State;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString Visibility;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    bool bJoinable = false;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString ConnectionString;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString MapId;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    FString ModeId;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    int32 MaxPlayers = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    int32 PlayerCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RIFT")
    int32 Revision = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FRiftSessionChangedSignature,
    const FRiftSession&,
    Session
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FRiftConnectionErrorSignature,
    const FRiftConnectionError&,
    Error
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FRiftHostSessionCreatedSignature,
    const FRiftGameSession&,
    Session
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FRiftHostSessionErrorSignature,
    const FRiftConnectionError&,
    Error
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FRiftGameSessionsFoundSignature,
    const TArray<FRiftGameSession>&,
    Sessions
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FRiftGameSessionsErrorSignature,
    const FRiftConnectionError&,
    Error
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(
    FRiftHostSessionClosedSignature
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FRiftHostSessionCloseErrorSignature,
    const FRiftConnectionError&,
    Error
);