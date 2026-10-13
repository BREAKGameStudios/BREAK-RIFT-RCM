#include "RiftProfileSubsystem.h"
#include "RiftConnectionSubsystem.h"
#include "HttpModule.h"
#include "Engine/GameInstance.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace {
FString NormalizeProfileId(const FString& Raw)
{
    FString Id = Raw.TrimStartAndEnd().ToUpper();
    if (Id.IsEmpty() || Id.Len() > 64) return FString();
    for (TCHAR C : Id) if (!(C >= 'A' && C <= 'Z') && !(C >= '0' && C <= '9') && C != '-') return FString();
    return Id;
}
FString RequestKey(const FString& Id, bool Avatar) { return Id + (Avatar ? TEXT("/avatar") : TEXT("/profile")); }
}

void URiftProfileSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (IsRunningDedicatedServer()) return;
    Collection.InitializeDependency<URiftConnectionSubsystem>();
    Connection = GetGameInstance()->GetSubsystem<URiftConnectionSubsystem>();
    Connection->OnSessionChanged.AddDynamic(this, &ThisClass::SessionChanged);
    Connection->OnConnectionError.AddDynamic(this, &ThisClass::ConnectionFailed);
    SessionChanged(Connection->GetCachedSession());
    Ticker = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &ThisClass::TickProfiles), 5.f);
}

void URiftProfileSubsystem::Deinitialize()
{
    bStopping = true;
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if (Connection) {
        Connection->OnSessionChanged.RemoveDynamic(this, &ThisClass::SessionChanged);
        Connection->OnConnectionError.RemoveDynamic(this, &ThisClass::ConnectionFailed);
    }
    OnProfileUpdated.Clear();
    Watches.Empty();
    Invalidate();
    Connection = nullptr;
    Super::Deinitialize();
}

void URiftProfileSubsystem::SessionChanged(const FRiftSession& Session)
{
    const FString NewAccount = Session.bAuthenticated ? NormalizeProfileId(Session.PublicId) : FString();
    if (NewAccount != AccountId) { bAvailable = false; Invalidate(); }
    AccountId = NewAccount;
    bAvailable = !AccountId.IsEmpty();
    TickProfiles(0);
}

void URiftProfileSubsystem::ConnectionFailed(const FRiftConnectionError& Error)
{
    bAvailable = false;
    AccountId.Empty();
    Invalidate();
}

void URiftProfileSubsystem::Invalidate()
{
    ++Generation;
    auto OldRequests = MoveTemp(Requests);
    Requests.Empty();
    for (auto& Pair : OldRequests) if (Pair.Value) Pair.Value->CancelRequest();
    TArray<FString> Ids;
    Profiles.GetKeys(Ids);
    Profiles.Empty();
    for (const FString& Id : Ids) Notify(Id);
}

int32 URiftProfileSubsystem::WatchProfile(const FString& PublicId, bool bLoadAvatar)
{
    const FString Id = NormalizeProfileId(PublicId);
    if (Id.IsEmpty() || bStopping || IsRunningDedicatedServer()) return 0;
    if (NextHandle == MAX_int32) NextHandle = 0;
    do { ++NextHandle; } while (Watches.Contains(NextHandle));
    const int32 Handle = NextHandle;
    FWatch Watch; Watch.Id = Id; Watch.bAvatar = bLoadAvatar;
    Watches.Add(Handle, Watch);
    EnsureProfile(Id);
    return Handle;
}

void URiftProfileSubsystem::ReleaseProfile(int32 WatchHandle)
{
    Watches.Remove(WatchHandle);
    TrimCache();
}

bool URiftProfileSubsystem::GetProfile(const FString& PublicId, FRiftPublicProfile& OutProfile) const
{
    if (const FRiftPublicProfile* Found = Profiles.Find(NormalizeProfileId(PublicId))) {
        OutProfile = *Found;
        return true;
    }
    OutProfile = FRiftPublicProfile();
    OutProfile.PublicId = NormalizeProfileId(PublicId);
    return false;
}

bool URiftProfileSubsystem::WantsAvatar(const FString& Id) const
{
    for (const auto& Pair : Watches) if (Pair.Value.Id == Id && Pair.Value.bAvatar) return true;
    return false;
}

bool URiftProfileSubsystem::TickProfiles(float DeltaTime)
{
    if (bStopping) return false;
    TSet<FString> Ids;
    for (const auto& Pair : Watches) Ids.Add(Pair.Value.Id);
    for (const FString& Id : Ids) EnsureProfile(Id);
    TrimCache();
    return true;
}

void URiftProfileSubsystem::RefreshProfile(const FString& PublicId)
{
    const FString Id = NormalizeProfileId(PublicId);
    if (Id.IsEmpty()) return;
    FRiftPublicProfile& Profile = Profiles.FindOrAdd(Id);
    Profile.NextProfileFetch = 0;
    Profile.NextAvatarFetch = 0;
    EnsureProfile(Id);
}

void URiftProfileSubsystem::EnsureProfile(const FString& Id)
{
    if (!bAvailable || bStopping) return;
    FRiftPublicProfile& Profile = Profiles.FindOrAdd(Id);
    Profile.PublicId = Id;
    Profile.LastAccess = FPlatformTime::Seconds();
    const bool FetchName = !Profile.bLoading && Profile.LastAccess >= Profile.NextProfileFetch;
    const bool FetchAvatar = Profile.bLoaded && Profile.bHasAvatar && WantsAvatar(Id)
        && !Profile.bAvatarLoading && Profile.LastAccess >= Profile.NextAvatarFetch;
    if (FetchName) Fetch(Id, false);
    if (FetchAvatar) Fetch(Id, true);
}

void URiftProfileSubsystem::Fetch(const FString& Id, bool bAvatar)
{
    const FString Key = RequestKey(Id, bAvatar);
    if (Requests.Contains(Key) || !Connection || bStopping) return;
    int32 Port = 0;
    FString Secret, Error;
    if (!Connection->GetLauncherEndpoint(Port, Secret, Error)) {
        FRiftPublicProfile& P = Profiles.FindOrAdd(Id);
        if (bAvatar) { P.AvatarError = TEXT("Launcher unavailable."); P.NextAvatarFetch = FPlatformTime::Seconds() + 15; }
        else { P.Error = TEXT("Launcher unavailable."); P.NextProfileFetch = FPlatformTime::Seconds() + 15; }
        Notify(Id);
        return;
    }
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FString::Printf(TEXT("http://127.0.0.1:%d/profiles/%s%s"),
        Port, *Id, bAvatar ? TEXT("/avatar") : TEXT("")));
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("X-Rift-Secret"), Secret);
    FString Token = FPlatformMisc::GetEnvironmentVariable(TEXT("RIFT_LAUNCH_TOKEN"));
    if (Token.IsEmpty())
    {
        FParse::Value(FCommandLine::Get(), TEXT("RiftLaunchToken="), Token);
    }
    Token = Token.TrimStartAndEnd();
    if (!Token.IsEmpty()) Request->SetHeader(TEXT("X-Rift-Launch-Token"), Token);
    Request->SetTimeout(20.f);
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    const uint64 ExpectedGeneration = Generation;
    TWeakObjectPtr<URiftProfileSubsystem> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, ExpectedGeneration, Id, bAvatar, Key](FHttpRequestPtr Sent, FHttpResponsePtr Response, bool bSuccess) {
            auto* Self = WeakThis.Get();
            if (!Self || Self->bStopping || Self->Generation != ExpectedGeneration) return;
            const FHttpRequestPtr* Pending = Self->Requests.Find(Key);
            if (!Pending || *Pending != Sent) return;
            Self->Requests.Remove(Key);
            Self->Complete(Id, bAvatar, Response, bSuccess);
        });
    Requests.Add(Key, Request);
    FRiftPublicProfile& P = Profiles.FindChecked(Id);
    if (bAvatar) { P.bAvatarLoading = true; P.AvatarError.Empty(); }
    else { P.bLoading = true; P.Error.Empty(); }
    // Begin the request before notifying listeners, which may remove their watch.
    if (!Request->ProcessRequest()) {
        Requests.Remove(Key);
        Complete(Id, bAvatar, nullptr, false);
    } else Notify(Id);
}

void URiftProfileSubsystem::Complete(const FString& Id, bool bAvatar,
    const FHttpResponsePtr& Response, bool bSuccess)
{
    FRiftPublicProfile& P = Profiles.FindOrAdd(Id);
    P.PublicId = Id;
    TSharedPtr<FJsonObject> Json;
    bool bValid = bSuccess && Response && Response->GetResponseCode() == 200
        && Response->GetContent().Num() <= 3 * 1024 * 1024
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json)
        && Json.IsValid();
    FString ReturnedId;
    int32 Version = 0;
    bValid = bValid && Json->TryGetNumberField(TEXT("protocol_version"), Version) && Version == 1
        && Json->TryGetStringField(TEXT("public_id"), ReturnedId) && NormalizeProfileId(ReturnedId) == Id;
    if (bAvatar) {
        P.bAvatarLoading = false;
        FString Encoded;
        TArray<uint8> Bytes;
        bValid = bValid && Json->TryGetStringField(TEXT("avatar_base64"), Encoded)
            && FBase64::Decode(Encoded, Bytes) && Bytes.Num() > 0 && Bytes.Num() <= 2 * 1024 * 1024;
        if (bValid) {
            auto& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            const auto Wrapper = Images.CreateImageWrapper(EImageFormat::PNG);
            bValid = Wrapper && Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num())
                && Wrapper->GetWidth() > 0 && Wrapper->GetWidth() <= 256
                && Wrapper->GetHeight() > 0 && Wrapper->GetHeight() <= 256;
            if (bValid) {
                UTexture2D* Texture = FImageUtils::ImportBufferAsTexture2D(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()));
                bValid = Texture != nullptr;
                if (bValid && P.bHasAvatar) P.Avatar = Texture;
            }
        }
        P.AvatarError = bValid ? FString() : TEXT("Avatar unavailable.");
        P.NextAvatarFetch = FPlatformTime::Seconds() + (bValid ? 60 : 15);
    } else {
        P.bLoading = false;
        FString Username, DisplayName;
        bool HasAvatar = false;
        bValid = bValid && Json->TryGetStringField(TEXT("username"), Username) && !Username.IsEmpty()
            && Json->TryGetStringField(TEXT("display_name"), DisplayName)
            && Json->TryGetBoolField(TEXT("has_avatar"), HasAvatar);
        if (bValid) {
            P.Username = Username.Left(128);
            P.DisplayName = DisplayName.Left(256);
            P.bHasAvatar = HasAvatar;
            P.bLoaded = true;
            if (!HasAvatar) { P.Avatar = nullptr; P.AvatarError.Empty(); }
        }
        P.Error = bValid ? FString() : TEXT("Profile unavailable.");
        P.NextProfileFetch = FPlatformTime::Seconds() + (bValid ? 60 : 15);
    }
    Notify(Id);
    // A metadata response can unlock the first lazy avatar request.
    if (!bAvatar) EnsureProfile(Id);
}

void URiftProfileSubsystem::Notify(const FString& Id)
{
    FRiftPublicProfile Copy;
    GetProfile(Id, Copy);
    OnProfileUpdated.Broadcast(Id, Copy);
}

void URiftProfileSubsystem::TrimCache()
{
    if (Profiles.Num() <= 128) return;
    TSet<FString> Active;
    for (const auto& Pair : Watches) Active.Add(Pair.Value.Id);
    TArray<FString> Idle;
    for (const auto& Pair : Profiles) if (!Active.Contains(Pair.Key)
        && !Pair.Value.bLoading && !Pair.Value.bAvatarLoading) Idle.Add(Pair.Key);
    Idle.Sort([this](const FString& A, const FString& B) {
        return Profiles.FindChecked(A).LastAccess < Profiles.FindChecked(B).LastAccess;
    });
    for (const FString& Id : Idle) { if (Profiles.Num() <= 128) break; Profiles.Remove(Id); }
}
