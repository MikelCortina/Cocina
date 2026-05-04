// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once


#include "FusionGlobals.h"

#include "Fusion/Client.h"

#include "Types/TypeDescriptor.h"
#include "CoreMinimal.h"
#include "FusionHelpers.h"
#include "FusionNetDriver.h"
#include "FusionOnlineSubsystem.h"
#include "ObjectActorPair.h"
#include "Components/ActorComponent.h"
#include "Types/PropertyHelpers.h"
#include "Types/TypeLookup.h"
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
	ReadyToNotifyAboutLevelLoad,
	Shutdown,
};


struct FDeferredDependency
{
	FObjectActorPair Pair;
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
	friend class UTypeDescriptor;
	friend class Property;
	friend class ObjectProperty;
	friend class ArrayProperty;
	friend class FusionArrayProperty;

	bool bPreviousMasterClientState = false;
	int32 SequenceIncrementAmount = 1;

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
	bool bBlockNextDestroy{false};
	
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
	TMap<FKeyObjectId, FObjectActorPair> ObjectIdToPair{};

	UPROPERTY()
	TMap<FKeyObjectId, FObjectActorPair> TempObjectIdToPair{};
	
	UPROPERTY()
	TMap<TObjectPtr<UObject>, uint64> ObjectToObjectId{};

	UPROPERTY()
	TMap<TObjectPtr<UObject>, uint64> TempObjectToObjectId{};
	
	TArray<SharedMode::ObjectRoot*> NewRemoteObjectRoots{};
	TArray<SharedMode::ObjectChild*> NewRemoteObjectChildren{};
	
	TMap<FKeyObjectId, TArray<FDeferredDependency>> DependencyChecks;
	
	TArray<SharedMode::ObjectId> RemoveAfterEndBeginFrame{};

	FWorldChangeRequest WorldChangeRequest;

	bool bDoingSeamlessTravel = false;
	
	void SetMapState(EMapState NewMapState);
	
	void AttachMapActor(UFusionActorComponent* Source, uint32 MapSequence, bool SendUpdates);
	void AttachGlobalInstanceActor(UObject* Object);
	void AttachSpawnedActor(UFusionActorComponent* Source, uint32 Scene, bool SendUpdates);
	SharedMode::ObjectRoot* FindRootParent(SharedMode::ObjectId Id);
	SharedMode::Object* CreateCustomObject(const FCopyContext& Context, UObject* Object, const UTypeDescriptor* Descriptor, uint32 Scene);
	void TriggerMapLoad();
	void TriggerMapLoadedCallback();
	
	void InitializeNewRemoteObjects();
	void InitializeNewLocalAndMapObjects();
	void UpdateRemoteObjectsActorState(const double Dt);

	void TickInRoomAndRunningEndFrame(double Dt);
	void TickInRoomAndRunningBeginFrame(double Dt);
	void UpdateRemoteState(const FObjectActorPair& Pair, const struct FPackagedSettings& Settings, const double Dt);
	void TickInRoomAndRunningRemoveActors();


	void CheckForMasterChange();
	void AddSpawnBlockedCls(UClass* InClass);
	void RemoveSpawnBlockedCls(UClass* InClass);

	void CopyLocalStateToObject(FObjectActorPair& Pair);
	void CopyRemoteStateToObject(FCopyContext& Context, const FObjectActorPair& Pair, bool IsInitialUpdate = false);

	void InvokeOnReps(UObject* Container, TSet<FRepValue>& Set);

	void CreateNetDriver(UWorld* World);
	void SetupNetDriver(UWorld* World);

	void TravelInternal(const FString& String);

	const FName FusionNetDriverDefName = TEXT("FusionNetDriver");
	const FName FusionNetDriverClassName = TEXT("/Script/PhotonFusion.FusionNetDriver");
	
	UPROPERTY()
	TObjectPtr<UFusionNetDriver> FusionNetDriver = nullptr;

	UObject* RemoveObjectPairs(const SharedMode::ObjectId Id);
	UObject* RemoveObjectRoot(const SharedMode::ObjectRoot* Root);
	
	SharedMode::Client* Client{nullptr};
	PhotonCommon::SubscriptionBag ClientSubscriptionBag;
	
	TMap<uint32, TSet<FKeyObjectId>> DestroyedMapActors;

	SharedMode::ObjectId CurrentPlayerStateId;


public:

	UPROPERTY()
	TWeakObjectPtr<UTypeLookup> Lookup;

	SharedMode::Client* GetClient() const { return Client; }
	
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

	void SendUserRpc(const int64 Id, const SharedMode::PlayerId Player, const AActor* Actor, const char* Data, SIZE_T DataLength);
	void SendCustomRPC(const UObject* Source, const FString& EventName, uint64 RPCId, EFusionRPCTarget Target, const TArray<uint8>& Buffer, ERPCMode RPCMode);
	
	virtual ~UFusionClient() override;
	FObjectActorPair RegisterObject(UFusionActorComponent* Source, AActor* OwningActor, UObject* Object, SharedMode::Object* FusionObject, EObjectPairType Type);
	FObjectActorPair RegisterRuntimeObject(UFusionActorComponent* Source, AActor* OwningActor, UObject* Object, SharedMode::Object* FusionObject, EObjectPairType Type);


	void Tick(double Dt);
	void TriggerLevelChanged(const FString& MapName, bool AttachCurrent = false);

	double NetworkTime();
	double NetworkTimeScale();
	double ActorNetworkTime(const AActor* Actor);
	
	FString GetLevelName() const { return CurrentMapInstance.Name; }

	void AddDependencyCheck(SharedMode::ObjectId Id, const FCopyContext& Root, const TFunction<bool()>& Callback);
	
	//
	void AddActorSource(UFusionActorComponent* Source);

	SharedMode::ObjectId FindObjectId(const UObject* Object);
	UObject* FindObject(SharedMode::ObjectId Id);
	FObjectActorPair FindObjectPair(SharedMode::ObjectId Id);
	SharedMode::Object* FindObject(const UObject* Object);
	SharedMode::ObjectRoot* FindObjectRoot(const UObject* Actor);

	void OnActorSpawned(AActor* SpawnedActor);
	void OnEngineObjectDestroyed(const UObject* DestroyedObject, bool bEngineObjectDestroyed);
	
	void Startup(UWorld* InitialWorld, UTypeLookup* TypeLookup, const UPhotonOnlineSubsystemSettings* const Settings, PhotonMatchmaking::RealtimeClient& InRealtimeClient);
	void Shutdown();
	
	void OnForcedDisconnect(FString Message);

	void OnObjectReady(SharedMode::ObjectRoot* Obj);
	void OnSubObjectCreated(SharedMode::ObjectChild* Obj);
	void OnSubObjectDestroyed(SharedMode::ObjectChild* Obj, SharedMode::DestroyModes Mode);

	void CopyToBackBuffer(const FObjectActorPair& Pair);
	bool OnObjectCreatedFinalize(SharedMode::Object* Obj);
	void OnObjectDestroyed(const SharedMode::ObjectRoot* Obj, SharedMode::DestroyModes Mode);

	void OnMapActorDestroyedRemote(uint32 SceneSequence, const SharedMode::ObjectId Id, const SharedMode::DestroyModes Mode);
	
	void OnSceneChange(uint32 Index, uint32 Sequence, SharedMode::Data Data);
	void OnRpcReceived(const SharedMode::Rpc& Rpc);
	bool IsLoadingMap();

	void OnMapInit(UWorld* World);
	void PreMapLoad(const FWorldContext& WorldContext, const FString& MapName, bool bIsSeamlessTravel = false);
	void PostMapLoad(UWorld* LoadedWorld);
	void OnMapDestroy(UWorld* World);

	void SendSocketToBackgroundThread();
	void RetrieveSocketFromBackgroundThread();
	
	bool ChangeWorld(const FString& WorldName);
	void ClientTravel(const FString& LevelName);
	bool IsTargetWorld(UWorld* World);
	
	void ToggleNetworkSend(UFusionActorComponent* FusionActorSettings, bool bToggle);
};


