#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RiftConnectionTypes.h"
#include "RiftConnectionSubsystem.generated.h"

UCLASS()
class RIFTCONNECTIONMANAGER_API URiftConnectionSubsystem final
    : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(
        FSubsystemCollectionBase& Collection
    ) override;

    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "RIFT")
    void RequestFreshSession();

    UFUNCTION(BlueprintPure, Category = "RIFT")
    const FRiftSession& GetCachedSession() const
    {
        return CachedSession;
    }

    UFUNCTION(BlueprintPure, Category = "RIFT")
    bool IsSessionLoading() const
    {
        return bSessionLoading;
    }

    UFUNCTION(BlueprintCallable, Category = "RIFT|Multiplayer")
    void CreateHostSession(
        const FRiftHostSessionRequest& Request
    );

    UFUNCTION(BlueprintCallable, Category = "RIFT|Multiplayer")
    void CloseHostedSession();

    UFUNCTION(BlueprintPure, Category = "RIFT|Multiplayer")
    bool IsCloseHostSessionRequestLoading() const
    {
        return bCloseHostSessionRequestLoading;
    }

    UFUNCTION(BlueprintCallable, Category = "RIFT|Multiplayer")
    void FindGameSessions();

    UFUNCTION(BlueprintPure, Category = "RIFT|Multiplayer")
    bool IsFindGameSessionsLoading() const
    {
        return bFindGameSessionsLoading;
    }

    UFUNCTION(BlueprintPure, Category = "RIFT|Multiplayer")
    const TArray<FRiftGameSession>& GetFoundGameSessions() const
    {
        return FoundGameSessions;
    }

    UFUNCTION(BlueprintPure, Category = "RIFT|Multiplayer")
    bool IsHostSessionRequestLoading() const
    {
        return bHostSessionRequestLoading;
    }

    UFUNCTION(BlueprintPure, Category = "RIFT|Multiplayer")
    const FRiftGameSession& GetHostedSession() const
    {
        return HostedSession;
    }

    UPROPERTY(BlueprintAssignable, Category = "RIFT")
    FRiftSessionChangedSignature OnSessionChanged;

    UPROPERTY(BlueprintAssignable, Category = "RIFT")
    FRiftConnectionErrorSignature OnConnectionError;

    UPROPERTY(BlueprintAssignable, Category = "RIFT|Multiplayer")
    FRiftHostSessionCreatedSignature OnHostSessionCreated;

    UPROPERTY(BlueprintAssignable, Category = "RIFT|Multiplayer")
    FRiftHostSessionErrorSignature OnHostSessionError;

    UPROPERTY(BlueprintAssignable, Category = "RIFT|Multiplayer")
    FRiftHostSessionClosedSignature OnHostSessionClosed;

    UPROPERTY(BlueprintAssignable, Category = "RIFT|Multiplayer")
    FRiftHostSessionCloseErrorSignature OnHostSessionCloseError;

    UPROPERTY(BlueprintAssignable, Category = "RIFT|Multiplayer")
    FRiftGameSessionsFoundSignature OnGameSessionsFound;

    UPROPERTY(BlueprintAssignable, Category = "RIFT|Multiplayer")
    FRiftGameSessionsErrorSignature OnGameSessionsError;

    bool GetLauncherEndpoint(int32& OutPort, FString& OutSecret, FString& OutError) const;

private:
    void FetchSession(uint64 RequestGeneration);

    void CompleteSessionRequest(
        uint64 RequestGeneration,
        const FRiftSession& Session
    );

    void FailSessionRequest(
        uint64 RequestGeneration,
        const FString& Code,
        const FString& Message
    );

    bool ReadLauncherConfig(
        int32& OutProtocolVersion,
        int32& OutPort,
        FString& OutSecret,
        FString& OutError
    ) const;

    bool ParseSessionResponse(
        const FString& ResponseBody,
        FRiftSession& OutSession,
        FRiftConnectionError& OutError
    ) const;

    void SendCreateHostSession(
        uint64 RequestGeneration,
        const FRiftHostSessionRequest& HostRequest
    );

    void CompleteHostSessionRequest(
        uint64 RequestGeneration,
        const FRiftGameSession& Session
    );

    void FailHostSessionRequest(
        uint64 RequestGeneration,
        const FString& Code,
        const FString& Message
    );

    bool ParseGameSessionResponse(
        const FString& ResponseBody,
        FRiftGameSession& OutSession,
        FRiftConnectionError& OutError
    ) const;

    void SendFindGameSessions(
        uint64 RequestGeneration
    );

    void CompleteFindGameSessions(
        uint64 RequestGeneration,
        const TArray<FRiftGameSession>& Sessions
    );

    void FailFindGameSessions(
        uint64 RequestGeneration,
        const FString& Code,
        const FString& Message
    );

    bool ParseGameSessionsResponse(
        const FString& ResponseBody,
        TArray<FRiftGameSession>& OutSessions,
        FRiftConnectionError& OutError
    ) const;

private:
    UPROPERTY()
    FRiftSession CachedSession;

    UPROPERTY()
    FRiftGameSession HostedSession;

    UPROPERTY()
    TArray<FRiftGameSession> FoundGameSessions;

    bool bSessionLoading = false;
    uint64 SessionRequestGeneration = 0;
    bool bHostSessionRequestLoading = false;
    uint64 HostSessionRequestGeneration = 0;
    bool bFindGameSessionsLoading = false;
    uint64 FindGameSessionsRequestGeneration = 0;
    bool bCloseHostSessionRequestLoading = false;
    uint64 CloseHostSessionRequestGeneration = 0;

    void SendCloseHostedSession(
        uint64 RequestGeneration
    );

    void CompleteCloseHostedSession(
        uint64 RequestGeneration
    );

    void FailCloseHostedSession(
        uint64 RequestGeneration,
        const FString& Code,
        const FString& Message
    );
};
