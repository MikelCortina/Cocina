// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "FusionGlobals.h"

#include "CoreMinimal.h"
#include "FusionHelpers.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Engine/World.h"

#include "Fusion/ClientConstructOptions.h"
#include "Fusion/CreateRoomOptions.h"
#include "Fusion/Broadcaster.h"
#include "Fusion/SubscriptionBag.h"

#include "FusionOnlineSubsystem.generated.h"

UENUM(BlueprintType)
enum class EFusionStatus : uint8
{
	None = 0 UMETA(DisplayName = "None"),
	Connecting = 1 UMETA(DisplayName = "Connecting"),
	Error = 2 UMETA(DisplayName = "Error"),
	Disconnected = 3 UMETA(DisplayName = "Disconnected"),
	Connected = 4 UMETA(DisplayName = "Connected"),
	JoiningRoom = 5 UMETA(DisplayName = "Joining Room"),
	InRoom = 6 UMETA(DisplayName = "In Room"),
	LeavingRoom = 7 UMETA(DisplayName = "Leaving Room"),
};

UENUM(BlueprintType)
enum class EFusionRegionSelectionMode : uint8
{
	// Connects to the first region that is available
	Default = 0 UMETA(DisplayName = "Default Region Selection"),
	// Select a region from a list of available regions
	Select = 1 UMETA(DisplayName = "Select Region"),
	// Chooses the region with the lowest ping, cache the result and use "Select Region" for faster connections
	Best = 2 UMETA(DisplayName = "Choose Best Region"),
};

USTRUCT(BlueprintType)
struct FFusionConnectOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	FString Region;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	EFusionRegionSelectionMode RegionSelectionMode = EFusionRegionSelectionMode::Default;
	
	PhotonMatchmaking::ClientConstructOptions ToClientConstructOptions(const FString& AppId, const FString& AppVersion) const
	{
		const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> AppIdUTF8 = StringCast<UTF8CHAR>(*AppId);
		const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> AppVersionUTF8 = StringCast<UTF8CHAR>(*AppVersion);

		PhotonMatchmaking::ClientConstructOptions Options{};
		Options.appId = reinterpret_cast<const PhotonCommon::CharType*>(AppIdUTF8.Get());
		Options.appVersion = reinterpret_cast<const PhotonCommon::CharType*>(AppVersionUTF8.Get());
		Options.regionSelectionMode = static_cast<PhotonMatchmaking::RegionSelectionMode>(RegionSelectionMode);
		return Options;
	}
};

USTRUCT(BlueprintType)
struct FFusionRoomOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	FString RoomName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	// ReSharper disable once CppUE4CodingStandardNamingViolationWarning
	uint8 EmptyTTL = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	// ReSharper disable once CppUE4CodingStandardNamingViolationWarning
	uint8 PlayerTTL = 5;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	uint8 MaxPlayers = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	bool bIsOpen = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	bool bIsVisible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	TSoftObjectPtr<UWorld> InitialWorld;

	PhotonMatchmaking::CreateRoomOptions ToCreateRoomOptions() const
	{
		PhotonMatchmaking::CreateRoomOptions Options{};
		Options.maxPlayers = MaxPlayers;
		Options.isOpen = bIsOpen;
		Options.isVisible = bIsVisible;
		Options.emptyRoomTtlMs = EmptyTTL * 1000;
		Options.playerTtlMs = PlayerTTL * 1000;
		return Options;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMapLoadRequested, FString, MapName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMapLoadPerform, FString, MapName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMapLoadDone, FString, MapName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnConnectedToPhoton);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnObjectReady, TSoftObjectPtr<UObject>, Object);

UCLASS()
class PHOTONFUSION_API UFusionOnlineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="Fusion")
	void TestTick(UWorld* World, float DeltaTime);
	
	void WorldTick(UWorld* World, ELevelTick LevelTick, float DeltaTime);

	bool IsLoadingMap();
	void SendCustomRPC(const UObject* Source, const FString& EventName, uint64 RPCId, EFusionRPCTarget Target, const TArray<uint8>& Buffer, ERPCMode RPCMode);

	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FOnMapLoadPerform OnMapLoadPerform;
	
	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FOnMapLoadRequested OnMapLoadRequested;
	
	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FOnMapLoadDone OnMapLoadDone;
	
	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FOnObjectReady OnObjectReady;
	
	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FOnConnectedToPhoton OnConnectedToPhoton;

	PhotonCommon::Broadcaster<void(const FString&)> OnMapLoadRequestedEvent;
	PhotonCommon::Broadcaster<void(const FString&)> OnMapLoadPerformEvent;
	PhotonCommon::Broadcaster<void(const FString&)> OnMapLoadDoneEvent;
	PhotonCommon::Broadcaster<void(TSoftObjectPtr<UObject>)> OnObjectReadyEvent;

	UFUNCTION(BlueprintPure, Category="Fusion")
	FString CurrentActiveMapName();

	UFUNCTION(BlueprintPure, Category="Fusion")
	UWorld* CurrentActiveWorld();
	
	UFUNCTION(BlueprintCallable, Category = "Photon")
	static void SetEditorDevelopmentFrameRateLimits();
	
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Photon")
	class UFusionConnectToPhotonAsync* ConnectToPhoton(const FFusionConnectOptions Options, UObject* WorldContextObject);
	
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Photon")
	class UFusionDisconnectFromPhotonAsync* DisconnectFromPhoton(UObject* WorldContextObject);
	
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Photon")
	class UFusionJoinOrCreateRoomAsync* JoinOrCreateRoom(const FFusionRoomOptions Options, UObject* WorldContextObject);
	
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Photon")
	UFusionJoinOrCreateRoomAsync* JoinOrCreateRandomRoom(const FFusionRoomOptions Options, UObject* WorldContextObject);
	
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Photon")
	class UFusionCreateRoomAsync* CreateRoom(const FFusionRoomOptions Options, UObject* WorldContextObject);
	
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Photon")
	class UFusionJoinRoomAsync* JoinRoom(const FString RoomName, UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Photon")
	UFusionJoinRoomAsync* JoinRandomRoom(UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Photon")
	class UFusionConnectAndJoinRoomAsync* ConnectAndJoinRoom(const FFusionConnectOptions ConnectOptions, const FFusionRoomOptions RoomOptions, UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Photon")
	class UFusionLeaveRoomAsync* LeaveRoom(UObject* WorldContextObject);
	
	bool StartFusionSession();

	void StopFusionSession();

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Fusion")
	void StopFusionSession(UObject* WorldContextObject, TSoftObjectPtr<UWorld> ReturnWorld = nullptr);

	UFUNCTION(BlueprintPure, Category = "Photon")
	UFusionRealtimeClient* GetRealtimeClient() const;
	
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Fusion")
	bool ChangeWorld(const TSoftObjectPtr<UWorld> World, UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Fusion")
	bool ChangeWorldByName(const FString WorldName, UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "Fusion")
	void ClientTravel(const FString LevelName, UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "Actor"), Category = "Fusion", Meta=(DefaultToSelf="Actor"))
	static void SetPriority(const AActor* Actor, int32 Priority);
	
	UFUNCTION(BlueprintCallable, Category = "Fusion", Meta=(DefaultToSelf="Actor"))
	void AttachActor(UFusionActorComponent* Source);

	UFUNCTION(BlueprintCallable, Category = "Fusion", Meta=(DefaultToSelf="Actor"))
	void AttachCurrentMap();
	
	UFUNCTION(BlueprintPure, Category = "Fusion", Meta=(DefaultToSelf="Object"))
	static bool HasOwner(const UObject* Object);
	
	UFUNCTION(BlueprintPure, Category = "Fusion", Meta=(DefaultToSelf="Object"))
	static int32 GetOwner(const UObject* Object);

	UFUNCTION(BlueprintPure,  Category = "Fusion", Meta=(DefaultToSelf="Object"))
	static int32 GetResolvedOwner(const UObject* Object);
	
	UFUNCTION(BlueprintPure, Category="Fusion", Meta=(DefaultToSelf="Object"))
	static bool IsOwner(const UObject* Object);
	
	UFUNCTION(BlueprintPure, Category="Fusion", Meta=(DefaultToSelf="Object"))
	static bool CanModify(const UObject* Object);
	
	UFUNCTION(BlueprintCallable, Category="Fusion", Meta=(DefaultToSelf="Actor"))
	static void SendRpc(const AActor* Actor, const int64 Id, const TArray<uint8>& Data);
	
	UFUNCTION(BlueprintCallable, Category="Fusion", Meta=(DefaultToSelf="Actor"))
	static void SendRpcToPlayer(const AActor* Actor, const int64 Id, const TArray<uint8>& Data, const int32 PlayerId);
	
	UFUNCTION(BlueprintPure, Category="Fusion", Meta=(DefaultToSelf="Actor"))
	static double ActorNetworkTime(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category="Fusion", Meta=(DefaultToSelf = "Actor"))
	static bool IsNetworked(const AActor* Actor);
	
	UFUNCTION(BlueprintCallable, Category="Fusion", Meta=(DefaultToSelf="Actor"))
	static void SetWantsOwner(const AActor* Actor, const bool bWantsOwner = true);

	// Manually register a collision with the Forecast physics system.
	// This is normally automatically called in the OnHitCallback of the simulating primitive.
	UFUNCTION(BlueprintCallable, Category = "Fusion")
	static bool RegisterForecastCollision(class UFusionPhysicsReplicationComponent* FusionPhysicsReplicationComponent);
	
	UFUNCTION(BlueprintPure, Category="Fusion")
	double NetworkTime() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	double NetworkTimeScale() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	bool IsConnected() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	bool IsFusionRunning() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	bool IsMasterClient() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	bool IsJoiningOrInRoom() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	bool IsNotJoiningOrInRoom() const;
	
	UFUNCTION(BlueprintPure, Category="Fusion")
	bool IsInRoom() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	bool IsNotInRoom() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	int32 PlayerCount() const;
	
	UFUNCTION(BlueprintPure, Category="Fusion")
	EFusionStatus Status() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	int32 GetLocalPlayerId() const;

	UFUNCTION(BlueprintPure, Category = "Fusion")
	double GetRtt() const;

	UFUNCTION(BlueprintPure, Category="Fusion")
	UPARAM(DisplayName="Valid")
	bool CurrentRoomInfo(FString& Name, int32& Players) const;

	class UFusionTypeLookup* GetTypeLookup() const;

	UPROPERTY()
	TObjectPtr<class UFusionRealtimeClient> RealtimeClient;

	UPROPERTY()
	TObjectPtr<UFusionClient> GFusionClient;

	void SetFusionClient(UFusionClient* Client);

	UFusionClient* GetFusionClient() const;

private:
	void OnMapDestroy(UWorld* LoadedWorld);
	void OnMapInit(UWorld* LoadedWorld, UWorld::InitializationValues InitValues);
	void OnPreMapLoad(const FWorldContext& Context, const FString& MapName);
	void OnPostMapLoad(UWorld* LoadedWorld);

	void OnLevelAdded(ULevel* Level, UWorld* World);
	void OnLevelRemoved(ULevel* Level, UWorld* World);

	void OnEndPIE(bool bIsSimulating);
	void Close();

	UPROPERTY()
	TObjectPtr<class UFusionTypeLookup> Lookup;
	
	FDelegateHandle OnMapDestroyDelegate{};
	FDelegateHandle OnMapInitDelegate{};

	FDelegateHandle MapLoadDelegateHandle{};
	FDelegateHandle MapPreLoadDelegateHandle{};
	
	FDelegateHandle OnWorldTickStartDelegate{};

	FDelegateHandle OnEngineExitHandle{};

	FDelegateHandle OnLevelAddedDelegate{};
	FDelegateHandle OnLevelRemovedDelegate{};

#if WITH_EDITOR
	FDelegateHandle EndPIEDelegateHandle{};
#endif

	uint64 LastTickFrame = UINT64_MAX;

	bool bRunUnderOneProcess{false};

	// Bridges Broadcaster events to UE delegates so call sites only need one Broadcast() call.
	PhotonCommon::SubscriptionBag BridgeSubscriptions;
};

