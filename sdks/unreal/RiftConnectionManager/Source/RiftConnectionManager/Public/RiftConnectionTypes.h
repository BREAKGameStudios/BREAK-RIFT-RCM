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
