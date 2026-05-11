// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once


#include "FusionGlobals.h"

#include "Fusion/Client.h"

#include "Types/FusionTypeDescriptor.h"
#include "CoreMinimal.h"
#include "FusionHelpers.h"
#include "FusionNetDriver.h"
#include "FusionOnlineSubsystem.h"
#include "FusionObjectActorPair.h"
#include "Components/ActorComponent.h"
#include "Types/FusionPropertyHelpers.h"
#include "Types/FusionTypeLookup.h"
#include "UObject/Class.h"
#include "FusionClient.generated.h"


UENUM(BlueprintType)
enum class EFusionDestroyMode : uint8
{
	Unknown = 0,
	Remote = 1,
	Engine = 2
};

class UFusionActorComponent;

USTRUCT()
struct FPendingObject
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UObject> Object{nullptr};
	
	UPROPERTY()
	TObjectPtr<UFusionActorComponent> Source{nullptr};
};

USTRUCT()
struct FMapInstance
{
	GENERATED_BODY()
	
	UPROPERTY()
	uint32 Sequence{0};
	
	UPROPERTY()
	FString Name{};

	UPROPERTY()
	bool bAttachCurrent{false};

	friend bool operator <(const FMapInstance& Lhs, const FMapInstance& Rhs)
	{
		return Lhs.Sequence < Rhs.Sequence;
	}
};

UENUM()
enum class EMapState : uint8
{
	Invalid,
	LevelActive,
	MasterClientChangeWorld,
	HasRequestToChangeLevel,
	IsLoading,
	WaitingToAttach,
	ReadyToNotifyAboutLevelLoad,
	Shutdown,
};


struct FDeferredDependency
{
	FFusionObjectActorPair Pair;
};

USTRUCT(BlueprintType)
struct FWorldChangeRequest
{
	GENERATED_BODY()

	bool bIsActive = false;
	FString WorldName;
};


/*
 * 
 * Level Loading - Name+Sequence
 * Level Ready - Name+Sequence+CallbackInvokeOrNot
 */

UCLASS(BlueprintType)
class PHOTONFUSION_API UFusionClient : public UObject
{
	GENERATED_BODY()

	friend UFusionHelpers;
	friend class UFusionTypeDescriptor;
	friend class Property;
	friend class ObjectProperty;
	friend class ArrayProperty;
	friend class FusionArrayProperty;

	FMapInstance TargetMapInstance;
	FMapInstance CurrentMapInstance;
	EMapState CurrentMapState = EMapState::Invalid;

	UPROPERTY()
	TArray<FMapInstance> RequestedMapInstances;

	UPROPERTY()
	TWeakObjectPtr<AActor> CurrentMapActor;

	UPROPERTY()
	TWeakObjectPtr<UWorld> CurrentWorld;

	UPROPERTY()
	TObjectPtr<APlayerController> PlayerController;

	UPROPERTY()
	EFusionInstanceType ClientInstanceType;
	
	UPROPERTY()
	TMap<uint32, TObjectPtr<AActor>> MapActors;

	FGuid ClientInstanceId;
	bool bRunUnderOneProcess{false};
	FString DriverName;
	bool bSocketInBgThread{false};

	TSet<const UObject*> RemoteDestroyedObjects;
	
	std::atomic_bool MainThreadReady{};
	std::atomic_bool BackThreadDone{};

	UPROPERTY()
	TArray<TObjectPtr<UClass>> BlockedClasses;

	TSet<TObjectPtr<AActor>> RemoteSpawnedActors;

	FDelegateHandle OnActorSpawnedHandle{};
	FDelegateHandle OnActorDestroyedHandle{};

	UPROPERTY()
	TArray<FPendingObject> PendingObjects{};

	UPROPERTY()
	TMap<FKeyObjectId, FFusionObjectActorPair> ObjectIdToPair{};

	UPROPERTY()
	TMap<FKeyObjectId, FFusionObjectActorPair> TempObjectIdToPair{};
	
	UPROPERTY()
	TMap<TObjectPtr<UObject>, uint64> ObjectToObjectId{};

	UPROPERTY()
	TMap<TObjectPtr<UObject>, uint64> TempObjectToObjectId{};
	
	TArray<FusionCore::ObjectRoot*> NewRemoteObjectRoots{};
	TArray<FusionCore::ObjectChild*> NewRemoteObjectChildren{};
	
	TMap<FKeyObjectId, TArray<FDeferredDependency>> DependencyChecks;
	
	TArray<FusionCore::ObjectId> RemoveAfterEndBeginFrame{};

	FWorldChangeRequest WorldChangeRequest;

	bool bDoingSeamlessTravel = false;
	
	void SetMapState(EMapState NewMapState);
	
	void AttachMapActor(UFusionActorComponent* Source, uint32 MapSequence, bool SendUpdates);
	void AttachGlobalInstanceActor(UFusionActorComponent* Source, const uint32 MapSequence, UObject* Object);
	void AttachSpawnedActor(UFusionActorComponent* Source, uint32 Scene, bool SendUpdates);
	FusionCore::ObjectRoot* FindRootParent(FusionCore::ObjectId Id);
	FusionCore::Object* CreateCustomObject(const FCopyContext& Context, UObject* Object, const UFusionTypeDescriptor* Descriptor, uint32 Scene);
	void TriggerMapLoad();
	void TriggerMapLoadedCallback();
	
	void InitializeNewRemoteObjects();
	void InitializeNewLocalAndMapObjects();
	void UpdateRemoteObjectsActorState(const double Dt);

	void TickInRoomAndRunningEndFrame(double Dt);
	void TickInRoomAndRunningBeginFrame(double Dt);
	void UpdateRemoteState(const FFusionObjectActorPair& Pair, const struct FPackagedSettings& Settings, const double Dt);
	void TickInRoomAndRunningRemoveActors();


	void AddSpawnBlockedCls(UClass* InClass);
	void RemoveSpawnBlockedCls(UClass* InClass);

	void CopyLocalStateToObject(FFusionObjectActorPair& Pair);
	void CopyRemoteStateToObject(FCopyContext& Context, const FFusionObjectActorPair& Pair, bool IsInitialUpdate = false);

	void InvokeOnReps(UObject* Container, TSet<FRepValue>& Set);

	void CreateNetDriver(UWorld* World);
	void SetupNetDriver(UWorld* World);

	void TravelInternal(const FString& String);

	const FName FusionNetDriverDefName = TEXT("FusionNetDriver");
	const FName FusionNetDriverClassName = TEXT("/Script/PhotonFusion.FusionNetDriver");
	
	UPROPERTY()
	TObjectPtr<UFusionNetDriver> FusionNetDriver = nullptr;

	UPROPERTY()
	TObjectPtr<class UFusionRealtimeClient> RealtimeClient = nullptr;

	UObject* RemoveObjectPairs(const FusionCore::ObjectId Id);
	UObject* RemoveObjectRoot(const FusionCore::ObjectRoot* Root);
	
	FusionCore::Client* Client{nullptr};
	PhotonCommon::SubscriptionBag ClientSubscriptionBag;
	
	TMap<uint32, TSet<FKeyObjectId>> DestroyedMapActors;

	FusionCore::ObjectId CurrentPlayerStateId;

public:

	UPROPERTY()
	TWeakObjectPtr<UFusionTypeLookup> Lookup;

	FusionCore::Client* GetClient() const { return Client; }
	
	void AttachCurrentMap(UWorld* World);
	void AttachCurrentMap_Internal(UWorld* World);

	UFusionClient();

	void ClientConnected();

	FGuid GetInstanceId() const
	{
		return ClientInstanceId;
	}

	EFusionInstanceType GetInstanceType() const
	{
		return ClientInstanceType;
	}
	
	UWorld* GetCurrentWorld() const;

	void SetWantOwner(const AActor* Actor);
	void SetDontWantOwner(const AActor* Actor);
	void ClearOwnerCooldown(const AActor* Actor);

	// Update area interest keys for all locally-owned root actors.
	// KeyFunc receives each actor and returns its area key (0 = global / no area filtering).
	void UpdateOwnedActorAreaInterestKeys(TFunctionRef<uint64(const AActor*)> KeyFunc);

	void SendUserRpc(const int64 Id, const FusionCore::PlayerId Player, const AActor* Actor, const char* Data, SIZE_T DataLength);
	void SendCustomRPC(const UObject* Source, const FString& EventName, uint64 RPCId, EFusionRPCTarget Target, const TArray<uint8>& Buffer, ERPCMode RPCMode);
	
	virtual ~UFusionClient() override;
	FFusionObjectActorPair RegisterObject(UFusionActorComponent* Source, AActor* OwningActor, UObject* Object, FusionCore::Object* FusionObject, EFusionObjectPairType Type);
	FFusionObjectActorPair RegisterRuntimeObject(UFusionActorComponent* Source, AActor* OwningActor, UObject* Object, FusionCore::Object* FusionObject, EFusionObjectPairType Type);

	void Tick(double Dt);
	void TriggerLevelChanged(const FString& MapName, bool AttachCurrent = false);

	double NetworkTime();
	double NetworkTimeScale();
	double ActorNetworkTime(const AActor* Actor);
	
	FString GetLevelName() const { return CurrentMapInstance.Name; }

	void UpdateGameState();

	void AddDependencyCheck(FusionCore::ObjectId Id, const FCopyContext& Root, const TFunction<bool()>& Callback);
	
	//
	void AddActorSource(UFusionActorComponent* Source);

	FusionCore::ObjectId FindObjectId(const UObject* Object);
	UObject* FindObject(FusionCore::ObjectId Id);
	FFusionObjectActorPair FindObjectPair(FusionCore::ObjectId Id);
	FusionCore::Object* FindObject(const UObject* Object);
	FusionCore::ObjectRoot* FindObjectRoot(const UObject* Actor);

	void OnActorSpawned(AActor* SpawnedActor);
	void OnEngineObjectDestroyed(AActor* AActor);
	
	void Startup(UWorld* InitialWorld, UFusionTypeLookup* TypeLookup, const UFusionOnlineSubsystemSettings* const Settings, TObjectPtr<class UFusionRealtimeClient> FusionRealtimeClient);
	void Shutdown();
	
	void OnForcedDisconnect(FString Message);

	void OnObjectReady(FusionCore::ObjectRoot* Obj);
	void OnSubObjectCreated(FusionCore::ObjectChild* Obj);
	void OnSubObjectDestroyed(FusionCore::ObjectChild* Obj, FusionCore::DestroyModes Mode);

	void CopyToBackBuffer(FFusionObjectActorPair& Pair);
	bool OnObjectCreatedFinalize(FusionCore::Object* Obj);
	void OnObjectDestroyed(const FusionCore::ObjectRoot* Obj, FusionCore::DestroyModes Mode);

	void OnMapActorDestroyedRemote(uint32 SceneSequence, const FusionCore::ObjectId Id, const FusionCore::DestroyModes Mode);

	void OnFusionStart();
	
	void OnMapChange(const std::unordered_map<FusionCore::Map, FusionCore::Data> &Maps, bool Initial);
	void OnRpcReceived(const FusionCore::Rpc& Rpc);
	bool IsLoadingMap();

	void OnMapInit(UWorld* World);
	void PreMapLoad(const FWorldContext& WorldContext, const FString& MapName, bool bIsSeamlessTravel = false);
	void PostMapLoad(UWorld* LoadedWorld);
	void OnMapDestroy(UWorld* World);

	void OnLevelAdded(ULevel* Level, UWorld* World);
	void OnLevelRemoved(ULevel* Level, UWorld* World);


	void SendSocketToBackgroundThread();
	void RetrieveSocketFromBackgroundThread();
	
	bool ChangeWorld(const FString& WorldName);
	void ClientTravel(const FString& LevelName);
	bool IsTargetWorld(UWorld* World);
	
	void ToggleNetworkSend(UFusionActorComponent* FusionActorSettings, bool bToggle);
};


