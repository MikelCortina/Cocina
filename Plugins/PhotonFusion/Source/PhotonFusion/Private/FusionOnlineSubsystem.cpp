// Copyright 2026 Exit Games GmbH. All Rights Reserved.
// ReSharper disable CppUnusedIncludeDirective

#include "FusionOnlineSubsystem.h"
#include "Actions/DisconnectFromPhotonAsync.h"
#include "Actions/ConnectAndJoinRoomAsync.h"
#include "Actions/ConnectToPhotonAsync.h"
#include "Actions/JoinOrCreateRoomAsync.h"
#include "Fusion/Aliases.h"
#include "Fusion/LogUtils.h"
#include "Fusion/LogOutput.h"
#include "Physics/FusionPhysicsReplicationComponent.h"
#include "Logging/FusionStandardLogOutput.h"
#include "Logging/FusionOnScreenDebugMessageLogOutput.h"
#include "FusionClient.h"
#include "FusionHelpers.h"
#include "FusionUtils.h"
#include "PhotonOnlineSubsystemSettings.h"
#include "Actions/CreateRoomAsync.h"
#include "Actions/JoinRoomAsync.h"
#include "Actions/LeaveRoomAsync.h"
#include "FusionRealtimeClient.h"
#include "Fusion/RealtimeClient.h"
#include "Kismet/GameplayStatics.h"
#include "Modules/ModuleManager.h"
#include "Physics/FusionPhysicsUtils.h"
#include "Misc/AssertionMacros.h"
#include "Misc/EngineVersionComparison.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameUserSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/Engine.h"
#include "Types/FusionNetworkedArrayBuilder.h"

#if WITH_EDITOR
#include "Settings/LevelEditorPlaySettings.h"
#include "Editor.h"    
#endif

// ReSharper restore CppUnusedIncludeDirective

#include "FusionCVars.h"

namespace FusionCVars
{
	int LoadMapBehaviourOverride = 0;
	static FAutoConsoleVariableRef CVarFusionLoadMapBehaviourOverride(
		TEXT("Fusion.LoadMapBehaviourOverride"),
		LoadMapBehaviourOverride,
		TEXT("0 - Use Project Settings LoadMapAutomatically setting, 1 - Force Load Map Automatically, 2 - Do not Load Map Automatically"),
		ECVF_Default
	);
}


void UFusionOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		const ULevelEditorPlaySettings* PlayInSettings = GetMutableDefault<ULevelEditorPlaySettings>();
		check(PlayInSettings);
		PlayInSettings->GetRunUnderOneProcess(bRunUnderOneProcess);
	}
#endif
	
	OnWorldTickStartDelegate = FWorldDelegates::OnWorldTickStart.AddUObject(this, &UFusionOnlineSubsystem::WorldTick);
	OnMapDestroyDelegate = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UFusionOnlineSubsystem::OnMapDestroy);
	OnMapInitDelegate = FWorldDelegates::OnPreWorldInitialization.AddUObject(this, &UFusionOnlineSubsystem::OnMapInit);
	MapLoadDelegateHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UFusionOnlineSubsystem::OnPostMapLoad);
	MapPreLoadDelegateHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &UFusionOnlineSubsystem::OnPreMapLoad);

#if WITH_EDITOR
	EndPIEDelegateHandle = FEditorDelegates::EndPIE.AddUObject(this, &UFusionOnlineSubsystem::OnEndPIE);
#endif
	
	const UPhotonOnlineSubsystemSettings* Settings = UPhotonOnlineSubsystemSettings::GetPhotonOnlineSettings();

	// Set up the log outputs
	if (Settings->EnabledLogOutput & StaticCast<uint8>(ESharedModeLogOutput::StandardLogOutput))
	{
		FusionStandardLogOutput* LogOutput = new FusionStandardLogOutput();
		LogOutputArr.Add(LogOutput);
		PhotonCommon::AddLogOutput(LogOutput);
	}

	if (Settings->EnabledLogOutput & StaticCast<uint8>(ESharedModeLogOutput::OnScreenDebugMessageLogOutput))
	{
		FusionOnScreenDebugMessageLogOutput* LogOutput = new FusionOnScreenDebugMessageLogOutput();
		LogOutputArr.Add(LogOutput);
		PhotonCommon::AddLogOutput(LogOutput);
	}

	// Set up the log levels to output
	PhotonCommon::SetLogLevelsFromBitmask(Settings->EnabledLogLevels);

	//Holds all our type data.
	Lookup = NewObject<UTypeLookup>();

	FPropertyBuildOptions BuildOptions { EBuildStructOptions::SkipNotReplicated };
	Lookup->CreateTypeDescriptor(UFusionPhysicsReplicationComponent::StaticClass(), BuildOptions);
	
	//Register custom types we want networked (serialize/deserialize)
	Lookup->RegisterTypeBuilder(FFusionNetworkedArray::StaticStruct(), UFusionNetworkedArrayBuilder::StaticClass());

	// Bridge Broadcaster events to UE delegates so call sites only need one Broadcast() call
	BridgeSubscriptions += OnMapLoadRequestedEvent.Subscribe([this](const FString& MapName) {
		OnMapLoadRequested.Broadcast(MapName);
	});
	BridgeSubscriptions += OnMapLoadPerformEvent.Subscribe([this](const FString& MapName) {
		OnMapLoadPerform.Broadcast(MapName);
	});
	BridgeSubscriptions += OnMapLoadDoneEvent.Subscribe([this](const FString& MapName) {
		OnMapLoadDone.Broadcast(MapName);
	});
	BridgeSubscriptions += OnObjectReadyEvent.Subscribe([this](TSoftObjectPtr<UObject> Object) {
		OnObjectReady.Broadcast(Object);
	});

	// Reset this CVar to its default value. As it's a static it can be maintained between runs in the editor. So reset it just to be sure.
	FusionCVars::LoadMapBehaviourOverride = 0;

}

void UFusionOnlineSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldTickStart.Remove(OnWorldTickStartDelegate);
	FWorldDelegates::OnWorldBeginTearDown.Remove(OnMapDestroyDelegate);
	FWorldDelegates::OnPreWorldInitialization.Remove(OnMapInitDelegate);
	
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapLoadDelegateHandle);
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(MapPreLoadDelegateHandle);

#if WITH_EDITOR
	FEditorDelegates::EndPIE.Remove(EndPIEDelegateHandle);
#endif

	Close();

	BridgeSubscriptions.UnsubscribeAll();
	OnMapLoadRequestedEvent.UnsubscribeAll();
	OnMapLoadPerformEvent.UnsubscribeAll();
	OnMapLoadDoneEvent.UnsubscribeAll();
	OnObjectReadyEvent.UnsubscribeAll();

	if (Lookup)
	{
		Lookup->UnRegisterTypeBuilder(FFusionNetworkedArray::StaticStruct());
		Lookup->Destroy();
	}
	Lookup = nullptr;
}

void UFusionOnlineSubsystem::OnEndPIE(bool bIsSimulating)
{
	UE_LOG(LogTemp, Display, TEXT("UFusionOnlineSubsystem::OnEndPIE: %d"), bIsSimulating);
	Close();
}

void UFusionOnlineSubsystem::Close()
{
	if (GFusionClient)
	{
		GFusionClient->Shutdown();
	}

	SetFusionClient(nullptr);

	RealtimeClient = nullptr;

	for (PhotonCommon::LogOutput* LogOutput : LogOutputArr)
	{
		if (!PhotonCommon::RemoveLogOutput(LogOutput))
		{
			UE_LOG(LogFusion, Error, TEXT("Error removing Fusion log output"));
		}
		delete LogOutput;
	}

	LogOutputArr.Empty();
}

void UFusionOnlineSubsystem::WorldTick([[maybe_unused]] UWorld* World, [[maybe_unused]] ELevelTick LevelTick, const float DeltaTime)
{
	if (IsEngineExitRequested())
	{
		Close();
		return;
	}
	
	if (LastTickFrame == GFrameCounter)
	{
		return;
	}

	LastTickFrame = GFrameCounter;

	if (GFusionClient)
	{
		GFusionClient->Tick(DeltaTime);
	}
	else if (RealtimeClient)
	{
		RealtimeClient->Service();
	}

	if (RealtimeClient)
	{
		RealtimeClient->CleanupCompletedTasks();
	}
}

void UFusionOnlineSubsystem::SendCustomRPC(const UObject* Source, const FString& EventName, const uint64 RPCId, const EFusionRPCTarget Target, const TArray<uint8>& Buffer, const ERPCMode RPCMode)
{
	if (Source)
	{
		if (!IsConnected())
			return;

		if (!IsInRoom())
			return;
		
		GFusionClient->SendCustomRPC(Source, EventName, RPCId, Target, Buffer, RPCMode);
	}
}

bool UFusionOnlineSubsystem::IsLoadingMap()
{
	return GFusionClient && GFusionClient->IsLoadingMap();
}

FString UFusionOnlineSubsystem::CurrentActiveMapName()
{
	if (GFusionClient)
	{
		return GFusionClient->GetLevelName();
	}

	return FString();
}

UWorld* UFusionOnlineSubsystem::CurrentActiveWorld()
{
	if (GFusionClient)
	{
		return GFusionClient->GetCurrentWorld();
	}

	return nullptr;
}

void UFusionOnlineSubsystem::SetEditorDevelopmentFrameRateLimits()
{
	UGameUserSettings* UserSettings = GEngine->GetGameUserSettings();
	UserSettings->SetFrameRateLimit(60);
	UserSettings->ApplySettings(true);
}

UConnectToPhotonAsync* UFusionOnlineSubsystem::ConnectToPhoton(const FFusionConnectOptions Options, UObject* WorldContextObject)
{
	if (UConnectToPhotonAsync* Action = UConnectToPhotonAsync::ConnectToPhoton(Options, WorldContextObject))
	{
		Action->Activate();
		return Action;
	}
	
	return nullptr;
}

UDisconnectFromPhotonAsync* UFusionOnlineSubsystem::DisconnectFromPhoton(UObject* WorldContextObject)
{
	if (UDisconnectFromPhotonAsync* Action = UDisconnectFromPhotonAsync::DisconnectFromPhoton(WorldContextObject))
	{
		Action->Activate();
		return Action;
	}

	return nullptr;
}

UJoinOrCreateRoomAsync* UFusionOnlineSubsystem::JoinOrCreateRoom(const FFusionRoomOptions Options, UObject* WorldContextObject)
{
	if (UJoinOrCreateRoomAsync* Action = UJoinOrCreateRoomAsync::JoinOrCreateRoom(Options, WorldContextObject))
	{
		Action->Activate();
		return Action;
	}

	return nullptr;
}

UJoinOrCreateRoomAsync* UFusionOnlineSubsystem::JoinOrCreateRandomRoom(const FFusionRoomOptions Options, UObject* WorldContextObject)
{
	if (UJoinOrCreateRoomAsync* Action = UJoinOrCreateRoomAsync::JoinOrCreateRandomRoom(Options, WorldContextObject))
	{
		Action->Activate();
		return Action;
	}

	return nullptr;
}

UCreateRoomAsync* UFusionOnlineSubsystem::CreateRoom(const FFusionRoomOptions Options, UObject* WorldContextObject)
{
	if (UCreateRoomAsync* Action = UCreateRoomAsync::CreateRoom(Options, WorldContextObject))
	{
		Action->Activate();
		return Action;
	}

	return nullptr;
}

UJoinRoomAsync* UFusionOnlineSubsystem::JoinRoom(const FString RoomName, UObject* WorldContextObject)
{
	if (UJoinRoomAsync* Action = UJoinRoomAsync::JoinRoom(RoomName, WorldContextObject))
	{
		Action->Activate();
		return Action;
	}

	return nullptr;
}

UJoinRoomAsync* UFusionOnlineSubsystem::JoinRandomRoom(UObject* WorldContextObject)
{
	if (UJoinRoomAsync* Action = UJoinRoomAsync::JoinRandomRoom(WorldContextObject))
	{
		Action->Activate();
		return Action;
	}

	return nullptr;
}

UConnectAndJoinRoomAsync* UFusionOnlineSubsystem::ConnectAndJoinRoom(const FFusionConnectOptions ConnectOptions, const FFusionRoomOptions RoomOptions, UObject* WorldContextObject)
{
	if (UConnectAndJoinRoomAsync* Action = UConnectAndJoinRoomAsync::ConnectAndJoinRoom(ConnectOptions, RoomOptions, WorldContextObject))
	{
		Action->Activate();
		return Action;
	}
	return nullptr;
}

bool UFusionOnlineSubsystem::ChangeWorld(const TSoftObjectPtr<UWorld> World, UObject* WorldContextObject)
{
	const FString MapPath = World.ToSoftObjectPath().GetLongPackageName();

	if (MapPath.IsEmpty())
		return false;
	
	return ChangeWorldByName(MapPath, WorldContextObject);
}

bool UFusionOnlineSubsystem::ChangeWorldByName(const FString WorldName, UObject* WorldContextObject)
{
	if (!IsInRoom())
		return false;

	if (!RealtimeClient || !RealtimeClient->IsMasterClient())
		return false;

	UWorld* CurrentWorld = WorldContextObject->GetWorld();
	if (!CurrentWorld)
		return false;
	
	if (GFusionClient)
		return GFusionClient->ChangeWorld(WorldName);
	
	return false;
}

void UFusionOnlineSubsystem::ClientTravel(const FString LevelName, UObject* WorldContextObject)
{
	if (GFusionClient)
	{
		GFusionClient->ClientTravel(LevelName);
	}
}

double UFusionOnlineSubsystem::NetworkTime() const
{
	if (GFusionClient)
	{
		return GFusionClient->NetworkTime();
	}

	return 0;
}

void UFusionOnlineSubsystem::SendRpc(const AActor* Actor, const int64 Id, const TArray<uint8>& Data)
{
	if (Actor)
	{
		if (const UFusionOnlineSubsystem* SubSystem = Actor->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>())
		{
			if (SubSystem->GFusionClient)
			{
				SubSystem->GFusionClient->SendUserRpc(Id, 0, Actor, reinterpret_cast<const char*>(Data.GetData()), Data.Num());
			}
		}
	}
}

void UFusionOnlineSubsystem::SendRpcToPlayer(const AActor* Actor, const int64 Id, const TArray<uint8>& Data, const int32 PlayerId)
{
	if (Actor)
	{
		if (const UFusionOnlineSubsystem* SubSystem = Actor->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>())
		{
			if (SubSystem->GFusionClient)
			{
				SubSystem->GFusionClient->SendUserRpc(Id, PlayerId, Actor, reinterpret_cast<const char*>(Data.GetData()), Data.Num());
			}
		}
	}
}

double UFusionOnlineSubsystem::NetworkTimeScale() const
{
	if (GFusionClient)
	{
		return GFusionClient->NetworkTimeScale();
	}

	return 0;
}

double UFusionOnlineSubsystem::ActorNetworkTime(const AActor* Actor)
{
	if (Actor)
	{
		if (const UFusionOnlineSubsystem* SubSystem = Actor->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>())
		{
			if (SubSystem->GFusionClient && Actor)
			{
				return SubSystem->GFusionClient->ActorNetworkTime(Actor);
			}
		}
	}

	return 0;
}

bool UFusionOnlineSubsystem::IsNetworked(const AActor* Actor)
{
	if (Actor)
	{
		if (const UFusionOnlineSubsystem* SubSystem = Actor->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>())
		{
			if (SubSystem->GFusionClient)
			{
				if (const SharedMode::Object* Obj = SubSystem->GFusionClient->FindObject(Actor))
				{
					return Obj != nullptr;
				}
			}
		}
	}

	return false;
}

void UFusionOnlineSubsystem::SetWantsOwner(const AActor* Actor, const bool bWantsOwner)
{
	if (Actor)
	{
		if (const UFusionOnlineSubsystem* SubSystem = Actor->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>())
		{
			if (SubSystem->GFusionClient)
			{
				if (bWantsOwner)
				{
					SubSystem->GFusionClient->SetWantOwner(Actor);
				}
				else
				{
					SubSystem->GFusionClient->SetDontWantOwner(Actor);
				}
			}
		}
	}
}

bool UFusionOnlineSubsystem::RegisterForecastCollision(UFusionPhysicsReplicationComponent* FusionPhysicsReplicationComponent)
{
	if (FusionPhysicsReplicationComponent != nullptr && !IsOwner(FusionPhysicsReplicationComponent->GetOwner()))
	{
		if (FusionPhysicsReplication* Replication = FusionPhysicsUtils::GetReplication(
			FusionPhysicsReplicationComponent->GetWorld()))
		{
			if (!Replication->RegisterCollision(FusionPhysicsReplicationComponent))
			{
				FUSION_LOG_WARN("Unable to register collision");
			}

			return true;
		}
	}

	return false;
}

void UFusionOnlineSubsystem::SetPriority(const AActor* Actor, const int32 Priority)
{
	if (Actor)
	{
		if (const UFusionOnlineSubsystem* SubSystem = Actor->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>())
		{
			if (SubSystem->GFusionClient)
			{
				if (SharedMode::Object* Obj = SubSystem->GFusionClient->FindObject(Actor))
				{
					SubSystem->GFusionClient->GetClient()->SetSendRate(Obj, Priority);
				}
			}
		}
	}
}

void UFusionOnlineSubsystem::AttachActor(UFusionActorComponent* Source)
{
	if (GFusionClient)
	{
		GFusionClient->AddActorSource(Source);
	}
}

void UFusionOnlineSubsystem::AttachCurrentMap()
{
	if (GFusionClient)
	{
		GFusionClient->AttachCurrentMap(GetWorld());
	}
}

bool UFusionOnlineSubsystem::HasOwner(const UObject* Object)
{
	if (Object)
	{
		if (UWorld* World = Object->GetWorld())
		{
			if (const UFusionOnlineSubsystem* SubSystem = World->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>())
			{
				if (SubSystem->GFusionClient)
				{
					if (const SharedMode::Object* Obj = SubSystem->GFusionClient->FindObject(Object))
					{
						return SubSystem->GFusionClient->GetClient()->HasOwner(Obj);
					}
				}
			}
		}

	}

	return false;
}

int32 UFusionOnlineSubsystem::GetOwner(const UObject* Object)
{
	if (Object)
	{
		if (UWorld* World = Object->GetWorld())
		{
			if (const UFusionOnlineSubsystem* SubSystem = World->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>())
			{
				if (SubSystem->GFusionClient)
				{
					if (const SharedMode::Object* Obj = SubSystem->GFusionClient->FindObject(Object))
					{
						return SubSystem->GFusionClient->GetClient()->GetOwner(Obj);
					}
				}
			}
		}
	}

	return INT_MIN;
}

int32 UFusionOnlineSubsystem::GetResolvedOwner(const UObject* Object)
{
	return GetOwner(Object);
}

bool UFusionOnlineSubsystem::IsOwner(const UObject* Object)
{
	if (Object)
	{
		if (UWorld* World = Object->GetWorld())
		{
			const UGameInstance* Instance = World->GetGameInstance();

			if (!Instance)
				return false;

			if (const UFusionOnlineSubsystem* SubSystem = Instance->GetSubsystem<UFusionOnlineSubsystem>())
			{
				if (SubSystem->GFusionClient)
				{
					if (const SharedMode::Object* Obj = SubSystem->GFusionClient->FindObject(Object))
					{
						return SubSystem->GFusionClient->GetClient()->IsOwner(Obj);
					}
				}
			}
		}
	}

	return false;
}

bool UFusionOnlineSubsystem::CanModify(const UObject* Object)
{
	if (Object)
	{
		const UWorld* World = Object->GetWorld();
		if (const UFusionOnlineSubsystem* SubSystem = World->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>())
		{
			if (SubSystem->GFusionClient)
			{
				if (const SharedMode::Object* Obj = SubSystem->GFusionClient->FindObject(Object))
				{
					return SubSystem->GFusionClient->GetClient()->CanModify(Obj);
				}
			}
		}
	}

	return false;
}

bool UFusionOnlineSubsystem::IsConnected() const
{
	return RealtimeClient && RealtimeClient->IsValid() && RealtimeClient->IsConnected();
}

bool UFusionOnlineSubsystem::IsFusionRunning() const
{
	return GFusionClient != nullptr;
}

bool UFusionOnlineSubsystem::IsMasterClient() const
{
	return RealtimeClient && RealtimeClient->IsMasterClient();
}

bool UFusionOnlineSubsystem::IsJoiningOrInRoom() const
{
	if (!RealtimeClient || !RealtimeClient->IsValid())
		return false;
	const auto State = RealtimeClient->GetClient()->GetState();
	return State == PhotonMatchmaking::ConnectionState::JoiningRoom || State == PhotonMatchmaking::ConnectionState::InRoom;
}

bool UFusionOnlineSubsystem::IsNotJoiningOrInRoom() const
{
	return !IsJoiningOrInRoom();
}

bool UFusionOnlineSubsystem::IsInRoom() const
{
	return RealtimeClient && RealtimeClient->IsValid() && RealtimeClient->IsInRoom();
}

bool UFusionOnlineSubsystem::IsNotInRoom() const
{
	return !IsInRoom();
}

int32 UFusionOnlineSubsystem::PlayerCount() const
{
	if (RealtimeClient)
		return RealtimeClient->PlayerCount();

	return 0;
}

EFusionStatus UFusionOnlineSubsystem::Status() const
{
	if (!RealtimeClient)
		return EFusionStatus::None;

	return RealtimeClient->GetStatus();
}

int32 UFusionOnlineSubsystem::GetLocalPlayerId() const
{
	if (RealtimeClient)
	{
		return RealtimeClient->GetLocalPlayerId();
	}

	return 0;
}

double UFusionOnlineSubsystem::GetRtt() const
{
	if (GFusionClient)
	{
		return GFusionClient->GetClient()->GetRtt();
	}

	return 0;
}


bool UFusionOnlineSubsystem::CurrentRoomInfo(FString& Name, int32& Players) const
{
	if (RealtimeClient)
	{
		return RealtimeClient->GetRoomInfo(Name, Players);
	}

	return false;
}

bool UFusionOnlineSubsystem::StartFusionSession()
{
	if (!RealtimeClient || !RealtimeClient->IsValid())
	{
		FUSION_LOG_ERROR("StartFusionSession: Invalid RealtimeClient");
		return false;
	}

	if (!RealtimeClient->IsInRoom())
	{
		FUSION_LOG_ERROR("StartFusionSession: Client is not in a room");
		return false;
	}

	if (GFusionClient)
	{
		FUSION_LOG_ERROR("StartFusionSession: Fusion is already running, call StopFusionSession first");
		return false;
	}

	UWorld* World = GetGameInstance()->GetWorld();
	if (!World)
	{
		FUSION_LOG_ERROR("StartFusionSession: Invalid World");
		return false;
	}

	const UPhotonOnlineSubsystemSettings* Settings = UPhotonOnlineSubsystemSettings::GetPhotonOnlineSettings();
	UFusionClient* FusionClient = NewObject<UFusionClient>();
	FusionClient->Startup(World, GetTypeLookup(), Settings, *RealtimeClient->GetClient());
	SetFusionClient(FusionClient);
	FusionClient->ClientConnected();

	return true;
}

bool UFusionOnlineSubsystem::StartFusionSession(UObject* WorldContextObject, TSoftObjectPtr<UWorld> InitialWorld)
{
	if (!StartFusionSession())
		return false;

	if (!InitialWorld.IsNull())
	{
		ChangeWorld(InitialWorld, WorldContextObject);
	}

	return true;
}

void UFusionOnlineSubsystem::StopFusionSession()
{
	if (GFusionClient)
	{
		GFusionClient->Shutdown();
		SetFusionClient(nullptr);
	}
}

void UFusionOnlineSubsystem::StopFusionSession(UObject* WorldContextObject, TSoftObjectPtr<UWorld> ReturnWorld)
{
	StopFusionSession();

	if (!ReturnWorld.IsNull())
	{
		const FString MapPath = ReturnWorld.ToSoftObjectPath().GetLongPackageName();
		if (!MapPath.IsEmpty())
		{
			UGameplayStatics::OpenLevel(WorldContextObject, FName(*MapPath));
		}
	}
}

ULeaveRoomAsync* UFusionOnlineSubsystem::LeaveRoom(UObject* WorldContextObject)
{
	if (ULeaveRoomAsync* Action = ULeaveRoomAsync::LeaveRoom(nullptr, WorldContextObject))
	{
		Action->Activate();
		return Action;
	}

	return nullptr;
}

UTypeLookup* UFusionOnlineSubsystem::GetTypeLookup() const
{
	return Lookup;
}

UFusionRealtimeClient* UFusionOnlineSubsystem::GetRealtimeClient() const
{
	return RealtimeClient;
}

void UFusionOnlineSubsystem::SetFusionClient(UFusionClient* Client)
{
	if (Client)
	{
		Client->AddToRoot();
		FUSION_LOG("Setting Fusion Subsystem Client, InstanceId: %s  InstanceType: %s", *Client->GetInstanceId().ToString(), *UEnum::GetValueAsString(Client->GetInstanceType()));
	}
	else
	{
		if (GFusionClient)
		{
			FUSION_LOG("Remove previous instance from root");
			GFusionClient->RemoveFromRoot();
		}
		
		FUSION_LOG("Setting Fusion Subsystem Client to null");
	}

	GFusionClient = Client;
}

UFusionClient* UFusionOnlineSubsystem::GetFusionClient() const
{
	return GFusionClient;
}

void UFusionOnlineSubsystem::OnMapDestroy(UWorld* LoadedWorld)
{
	if (!GFusionClient)
		return;

	//When under one process all maps are under the engine and we need to figure out which world goes with which fusion client.
	if (bRunUnderOneProcess && !UFusionHelpers::IsAllowedWorldInstance(GFusionClient, LoadedWorld))
		return;

	// Remove actor spawn/destroy handlers from the world being torn down
	if (LoadedWorld)
	{
		if (OnActorSpawnedHandle.IsValid())
		{
			LoadedWorld->RemoveOnActorSpawnedHandler(OnActorSpawnedHandle);
			OnActorSpawnedHandle.Reset();
		}
		if (OnActorDestroyedHandle.IsValid())
		{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
			LoadedWorld->RemoveOnActorDestroyededHandler(OnActorDestroyedHandle);
#else
			LoadedWorld->RemoveOnActorDestroyedHandler(OnActorDestroyedHandle);
#endif
			OnActorDestroyedHandle.Reset();
		}
	}

	GFusionClient->OnMapDestroy(LoadedWorld);
}

void UFusionOnlineSubsystem::OnMapInit(UWorld* LoadedWorld, UWorld::InitializationValues InitValues)
{
	if (!GFusionClient)
		return;
	
	if (LoadedWorld->WorldType == EWorldType::EditorPreview)
		return;

	if (!IsConnected())
		return;

	//When under one process all maps are under the engine and we need to figure out which world goes with which fusion client.
	if (bRunUnderOneProcess && !UFusionHelpers::IsAllowedWorldInstance(GFusionClient, LoadedWorld))
		return;

	if (!GFusionClient->IsTargetWorld(LoadedWorld))
	{
		return;
	}
	
	OnActorSpawnedHandle = LoadedWorld->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UFusionOnlineSubsystem::OnActorSpawned));
	OnActorDestroyedHandle = LoadedWorld->AddOnActorDestroyedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UFusionOnlineSubsystem::OnActorDestroyed));
	
	GFusionClient->OnMapInit(LoadedWorld);
}

void UFusionOnlineSubsystem::OnActorSpawned(AActor* SpawnedActor)
{
	if (!GFusionClient)
		return;

	if (!IsConnected())
		return;

	GFusionClient->OnActorSpawned(SpawnedActor);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void UFusionOnlineSubsystem::OnActorDestroyed(AActor* DestroyedActor)
{
	if (!GFusionClient)
		return;

	if (!IsConnected())
		return;

	GFusionClient->OnEngineObjectDestroyed(DestroyedActor, true);
}

void UFusionOnlineSubsystem::OnPreMapLoad(const FWorldContext& Context, const FString& WorldName)
{
	if (!GFusionClient)
		return;
	
	if (bRunUnderOneProcess && !UFusionHelpers::IsAllowedWorldContext(GFusionClient, Context))
		return;

	if (!IsConnected())
		return;
	
	GFusionClient->PreMapLoad(Context, WorldName);
}

void UFusionOnlineSubsystem::OnPostMapLoad(UWorld* LoadedWorld)
{
	if (!GFusionClient)
		return;

	//When under one process all maps are under the engine and we need to figure out which world goes with which fusion client.
	if (bRunUnderOneProcess && !UFusionHelpers::IsAllowedWorldInstance(GFusionClient, LoadedWorld))
		return;

	if (!GFusionClient->IsTargetWorld(LoadedWorld))
		return;

	if (!IsConnected())
		return;
	
	GFusionClient->PostMapLoad(LoadedWorld);
}
