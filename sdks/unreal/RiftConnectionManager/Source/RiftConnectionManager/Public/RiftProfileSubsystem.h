#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "RiftConnectionTypes.h"
#include "RiftProfileSubsystem.generated.h"

class UTexture2D;
class URiftConnectionSubsystem;

USTRUCT(BlueprintType)
struct RIFTCONNECTIONMANAGER_API FRiftPublicProfile
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") FString PublicId;
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") FString Username;
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") FString DisplayName;
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") bool bLoaded = false;
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") bool bLoading = false;
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") bool bHasAvatar = false;
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") bool bAvatarLoading = false;
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") TObjectPtr<UTexture2D> Avatar = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") FString Error;
    UPROPERTY(BlueprintReadOnly, Category="RIFT|Profile") FString AvatarError;
    double NextProfileFetch = 0;
    double NextAvatarFetch = 0;
    double LastAccess = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRiftProfileUpdated,
    const FString&, PublicId, const FRiftPublicProfile&, Profile);

// Shared presentation data. Profiles never authenticate a player or authorize gameplay.
UCLASS()
class RIFTCONNECTIONMANAGER_API URiftProfileSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Keep the returned handle and release it when the consumer stops displaying this player.
    UFUNCTION(BlueprintCallable, Category="RIFT|Profile")
    int32 WatchProfile(const FString& PublicId, bool bLoadAvatar);
    UFUNCTION(BlueprintCallable, Category="RIFT|Profile")
    void ReleaseProfile(int32 WatchHandle);
    UFUNCTION(BlueprintPure, Category="RIFT|Profile")
    bool GetProfile(const FString& PublicId, FRiftPublicProfile& OutProfile) const;
    UFUNCTION(BlueprintCallable, Category="RIFT|Profile")
    void RefreshProfile(const FString& PublicId);
    UFUNCTION(BlueprintPure, Category="RIFT|Profile")
    bool IsProfileServiceAvailable() const { return bAvailable; }
    UPROPERTY(BlueprintAssignable, Category="RIFT|Profile")
    FRiftProfileUpdated OnProfileUpdated;

private:
    struct FWatch { FString Id; bool bAvatar = false; };
    UPROPERTY(Transient) TMap<FString, FRiftPublicProfile> Profiles;
    UPROPERTY(Transient) TObjectPtr<URiftConnectionSubsystem> Connection;
    TMap<int32, FWatch> Watches;
    TMap<FString, FHttpRequestPtr> Requests;
    FTSTicker::FDelegateHandle Ticker;
    int32 NextHandle = 0;
    uint64 Generation = 0;
    FString AccountId;
    bool bAvailable = false;
    bool bStopping = false;
    UFUNCTION() void SessionChanged(const FRiftSession& Session);
    UFUNCTION() void ConnectionFailed(const FRiftConnectionError& Error);
    bool TickProfiles(float DeltaTime);
    bool WantsAvatar(const FString& Id) const;
    void EnsureProfile(const FString& Id);
    void Fetch(const FString& Id, bool bAvatar);
    void Complete(const FString& Id, bool bAvatar, const FHttpResponsePtr& Response, bool bSuccess);
    void Notify(const FString& Id);
    void Invalidate();
    void TrimCache();
};
