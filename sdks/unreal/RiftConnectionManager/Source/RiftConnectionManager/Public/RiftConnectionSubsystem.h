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

    UPROPERTY(BlueprintAssignable, Category = "RIFT")
    FRiftSessionChangedSignature OnSessionChanged;

    UPROPERTY(BlueprintAssignable, Category = "RIFT")
    FRiftConnectionErrorSignature OnConnectionError;

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

private:
    UPROPERTY()
    FRiftSession CachedSession;

    bool bSessionLoading = false;
    uint64 SessionRequestGeneration = 0;
};
