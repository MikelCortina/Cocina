// Copyright 2026 Exit Games GmbH. All Rights Reserved.
// ReSharper disable CppUnusedIncludeDirective

#include "FusionClient.h"
#include "FusionCVars.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include <codecvt>
#include <Fusion/FusionRoomProperties.h>

#include "Misc/AssertionMacros.h"
#include "Misc/EngineVersionComparison.h"
#include "EngineUtils.h"
#include "FusionActorComponent.h"
#include "FusionHelpers.h"
#include "Physics/FusionPhysicsReplicationComponent.h"
#include "FusionUtils.h"
#include "Physics/FusionPhysicsUtils.h"
#include "FusionOnlineSubsystemSettings.h"
#include "FusionRealtimeClient.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameState.h"
#include "GameFramework/WorldSettings.h"
#include "WorldPartition/WorldPartitionReplay.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "Engine/Engine.h"  
#include "Components/PrimitiveComponent.h" 
#include "UObject/UObjectIterator.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Async/Async.h"
#include "Engine/GameInstance.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/IConsoleManager.h"
#include "Types/FusionNetworkedArrayBuilder.h"
#include "Engine/GameEngine.h"
#include "Engine/NetDriver.h"
#include "GameFramework/GameMode.h"
#include "Types/FusionTypeLookup.h"
#include "Engine/LevelStreaming.h"

#if WITH_EDITOR
#include "Settings/LevelEditorPlaySettings.h"
#endif

// ReSharper restore CppUnusedIncludeDirective

// Photon Fusion is the actual transport, not UE replication. UE's level-streaming
// visibility transactions wait on a server ack that never arrives, hanging the
// MakingInvisible/MakingVisible transitions. These flags tell the engine to skip
// the transaction request — they're protected on ULevelStreaming, so we elevate
// access via a derived struct (no new members → safe static_cast).
struct FFusionLevelStreamingAccess : public ULevelStreaming
{
	using ULevelStreaming::bSkipClientUseMakingInvisibleTransactionRequest;
	using ULevelStreaming::bSkipClientUseMakingVisibleTransactionRequest;
};

static void ApplyFusionStreamingSkipFlags(UWorld* World)
{
	if (!World)
	{
		return;
	}
	
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (!Streaming)
		{
			continue;
		}
		auto* Access = static_cast<FFusionLevelStreamingAccess*>(Streaming);
		Access->bSkipClientUseMakingInvisibleTransactionRequest = 1;
		Access->bSkipClientUseMakingVisibleTransactionRequest   = 1;
	}
}


FKeyObjectId::operator uint64() const
{
	return static_cast<uint64>(Origin) | static_cast<uint64>(Counter) << 32;
}

static EFusionObjectDestroyMode ToUnrealDestroyMode(FusionCore::DestroyModes Mode)
{
	switch (Mode)
	{
	case FusionCore::DestroyModes::Local:            return EFusionObjectDestroyMode::Local;
	case FusionCore::DestroyModes::Remote:           return EFusionObjectDestroyMode::Remote;
	case FusionCore::DestroyModes::MapChange:        return EFusionObjectDestroyMode::MapChange;
	case FusionCore::DestroyModes::Shutdown:          return EFusionObjectDestroyMode::Shutdown;
	case FusionCore::DestroyModes::RejectedNotOwner: return EFusionObjectDestroyMode::RejectedNotOwner;
	case FusionCore::DestroyModes::ForceDestroy:     return EFusionObjectDestroyMode::ForceDestroy;
	default:                                         return EFusionObjectDestroyMode::Local;
	}
}

void UFusionClient::AttachCurrentMap(UWorld* World)
{
	if (!Client->IsMasterClient())
		return;
	
	TriggerLevelChanged(World->GetName(), true);
		
	//Assume masterclient can directly connect all things in the active world.
	AttachCurrentMap_Internal(World);
}

void UFusionClient::AttachCurrentMap_Internal(UWorld* World)
{
	if (CurrentWorld.IsValid())
	{
		TArray<FusionCore::ObjectId> PairsToRemove;
		for (auto Pair : ObjectIdToPair)
		{
			FusionCore::Object* Object = Client->FindObject(Pair.Key);

			if (!Object)
			{
				PairsToRemove.Add(Pair.Key);
				continue;
			}
			
			if (Object && (Object->EngineFlags & static_cast<uint32_t>(EObjectSpecialFlags::ExistsOnClient)) != 0)
			{
				continue;
			}

			if (Pair.Value.Actor && Pair.Value.Actor->GetLocalRole() != ROLE_SimulatedProxy)
			{
				continue;
			}
				
			PairsToRemove.Add(Pair.Key);
		}

		//Delete any proxies from previously connected sessions.
		for (auto Id : PairsToRemove)
		{
			auto Pair = FindObjectPair(Id);
			RemoveObjectPairs(Id);

			if (Pair.Actor)
			{
				Pair.Actor->Destroy();
			}
		}
	}

	//Destroy any remote proxy objects incase this is called multiple times.
	
	OnMapDestroy(CurrentWorld.Get());

	OnMapInit(World);

	CurrentWorld = World;

	if (CurrentWorld.IsValid())
	{
		PostMapLoad(CurrentWorld.Get());

		//Remove any previously owned objects.
		for (auto& [ObjectId, RootObject] : Client->AllRootObjects())
		{
			if (Client->IsOwner(RootObject))
			{
				RemoveObjectRoot(RootObject);

				if (RootObject && (RootObject->EngineFlags & static_cast<uint32_t>(EObjectSpecialFlags::ExistsOnClient)) == 0)
				{
					//Destroys any player owned objects that are not part of client init (map actors and GameInstance)
					Client->DestroyObjectLocal(RootObject, true);
				}
			}
		}
		
		UObject* GameInstance = UGameplayStatics::GetGameInstance(CurrentWorld.Get());
		AttachGlobalInstanceActor(nullptr, 0, GameInstance);

		// First, connect any existing remote network objects to actors in the current world.
		// These are objects that were spawned on remote clients before this map was attached.
		InitializeNewRemoteObjects();
		
		TMap<uint32, UFusionActorComponent*> UnboundMapActors;
		for (TActorIterator<AActor> It(CurrentWorld.Get()); It; ++It)
		{
			TObjectPtr<AActor> Actor = *It;

			UFusionActorComponent* Source = Actor->GetComponentByClass<UFusionActorComponent>();

			if (Actor->IsA(APlayerState::StaticClass()))
			{
				if (!Source)
				{
					Source = NewObject<UFusionActorComponent>(Actor);
					Source->bSkipAutoAttach = true;
					Source->RegisterComponent();
				}
				
				Source->Ownership = EFusionObjectOwnerFlags::PlayerAttached;
			}

			if (Actor->IsA(AGameStateBase::StaticClass()))
			{
				if (!Source)
				{
					Source = NewObject<UFusionActorComponent>(Actor);
					Source->bSkipAutoAttach = true;
					Source->RegisterComponent();
				}
				
				Source->Ownership = EFusionObjectOwnerFlags::MasterClient;
			}
			
			if (!Source)
				continue;

			if (Actor->HasAnyFlags(RF_Transient) || !Actor->bNetStartup)
			{
				if (FindObject(Actor)) //Skip already mapped objects
					continue;

				//Add local transient actors.
				AddActorSource(Source);
			}
			else
			{
				const uint32 ObjectHash = UFusionHelpers::SafeObjectNameHash(Actor);
				UnboundMapActors.Add(ObjectHash, Source);
			}
		}

		//Create all remote objects including already existing map objects.
		for (auto& [ObjectId, RootObject] : Client->AllRootObjects())
		{
			if (RootObject && (RootObject->EngineFlags & static_cast<uint32_t>(EObjectSpecialFlags::SceneObject)) != 0)
			{
				if (UnboundMapActors.Contains(ObjectId.Counter))
				{
					UFusionActorComponent* Source = UnboundMapActors[ObjectId.Counter];
					AttachMapActor(Source, CurrentMapInstance.Sequence, Source->bAutomaticallySendUpdates);
					UnboundMapActors.Remove(ObjectId.Counter);
				}
			}
			else
			{
				if (Client->IsOwner(RootObject))
					continue;

				//InitializeNewRemoteObjects above could potentially have made the object.
				auto Pair = FindObjectPair(ObjectId);
				if (Pair.IsValid())
					continue;
				
				OnObjectCreatedFinalize(RootObject);

				for (auto SubObjectId : RootObject->GetSubObjects())
				{
					FFusionObjectActorPair SubObjectPair = FindObjectPair(SubObjectId);
					if (SubObjectPair.IsValid()) //Check if we already have this connected
						continue;

					FusionCore::Object* SubObject = Client->FindObject(SubObjectId);
					OnObjectCreatedFinalize(SubObject);
				}
			}
		}
		
		//All remaining map actors should we connected to via pending
		for (auto Pair : UnboundMapActors)
		{
			AddActorSource(Pair.Value);
		}
	}

	SetMapState(EMapState::LevelActive);
}

UFusionClient::UFusionClient()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}
}

void UFusionClient::ClientConnected()
{
	// Set the current state to LevelActive as we do not Initialize during a load
	SetMapState(EMapState::LevelActive);
}

void UFusionClient::SendUserRpc(const int64 Id, const  FusionCore::PlayerId Player, const AActor* Actor, const char* Data, SIZE_T DataLength)
{
	auto RPC = Client->CreateUserRpc(Id, Player, FusionCore::ObjectId{}, 0, Data, DataLength);
	
	if (Actor == nullptr)
	{
		
		Client->SendUserRpc(RPC);
	}
	else
	{
		if (const FusionCore::Object* Obj = FindObject(Actor))
		{
			Client->SendUserRpc(RPC);
		}
		else
		{
			FUSION_LOG_WARN("Unknown Actor (Not attached to network?)");
		}
	}
}

void UFusionClient::SendCustomRPC(const UObject* Source, const FString& EventName, const uint64 RPCId, const EFusionRPCTarget Target, const TArray<uint8>& Buffer, const ERPCMode RPCMode)
{
	//Ensure that the backing event is mapped on a descriptor.
	if (const UFusionTypeDescriptor* Descriptor = Lookup->FindClassDescriptor(Source->GetClass()); Descriptor && Descriptor->EventFunctions.Contains(EventName))
	{
		if (const FusionCore::ObjectId ObjectId = FindObjectId(Source); ObjectId.IsSome())
		{
			//So we can find the actual event when receiving the rpc.
			const uint64 EventHash = Descriptor->EventNameToHash[EventName];

			FusionCore::PlayerId TargetPlayer;
			if (Target == EFusionRPCTarget::SendToMasterClient)
			{
				TargetPlayer = FusionCore::MasterClientPlayerId;
			}
			else if (Target == EFusionRPCTarget::SendToObjectOwner)
			{
				TargetPlayer = FusionCore::ObjectOwnerPlayerId;
			}
			else
			{
				//What do we use to donate no specific player as target and just for all clients?
				TargetPlayer = FusionCore::PlayerId{};

				//Immediate dispatch to self, since everyone is target, (perhaps with small artificial delay to not make local stuff to snappy...)
			}

			const FusionCore::Object* EngineObject = FindObject(Source);

			//Reason why we are sending an Int64 is because it's passed from potential Blueprint source and
			FusionCore::Rpc Rpc = Client->CreateUserRpc(RPCId, TargetPlayer, ObjectId,
			                                                  EventHash,
			                                                  reinterpret_cast<const char*>(Buffer.GetData()),
			                                                  Buffer.Num());
			
			//UE_LOG(LogFusion, Warning, TEXT("Sending RPC id: %llu  ClientId: %s"), Rpc.Id, *ClientInstanceId.ToString());
			
			if (RPCMode == ERPCMode::FusionRPC)
			{
				if (Target == EFusionRPCTarget::SendToEveryoneElse)
				{
					Client->SendUserRpc(Rpc);
				}
				else if (Target == EFusionRPCTarget::SendToAllClients)
				{
					//Send to self.
					OnRpcReceived(Rpc);
			
					Client->SendUserRpc(Rpc);
				}
				else if (Target == EFusionRPCTarget::SendToMasterClient && Client->IsMasterClient())
				{
					OnRpcReceived(Rpc);
				}
				else if (Target == EFusionRPCTarget::SendToObjectOwner && Client->IsOwner(EngineObject))
				{
					OnRpcReceived(Rpc);
				}
				else
				{
					Client->SendUserRpc(Rpc);
				}
			}
			else if (RPCMode == ERPCMode::UnrealRPC)
			{
				bool TargetMasterClient = TargetPlayer == FusionCore::MasterClientPlayerId;
				bool TargetOwner = TargetPlayer == FusionCore::ObjectOwnerPlayerId;
				bool TargetPlugin = TargetPlayer == FusionCore::PluginPlayerId;
				
				if (TargetMasterClient || TargetOwner || TargetPlugin)
				{
					OnRpcReceived(Rpc);
				}
				else
				{
					Client->SendUserRpc(Rpc);
				}
			}
		}
	}
}

UFusionClient::~UFusionClient()
{
	delete Client;
}

FFusionObjectActorPair UFusionClient::RegisterObject(UFusionActorComponent* Source, AActor* OwningActor, UObject* Object, FusionCore::Object* FusionObject, EFusionObjectPairType Type)
{
	if (Object == nullptr)
	{
		FUSION_LOG_WARN("Actor was null");
		return {};
	}

	if (FusionObject == nullptr)
	{
		FUSION_LOG_WARN("Obj was null");
		return {};
	}

	if (FusionObject->Type.Hash == 0)
	{
		FUSION_LOG_WARN("TypeRef is invalid");
		return {};
	}
	
	//
	FUSION_LOG("Shared Mode Object Registered %s (%s) [hash/id: %llu]", *ObjectIdToString(FusionObject->Id), *Object->GetName(), FusionObject->Type.Hash);

	// store actor reference on object also, but this will
	// not keep the object alive from GC pov.
	FusionObject->Engine = Object;

	FFusionObjectActorPair Pair = FFusionObjectActorPair{Type, OwningActor, Object, Source, FusionObject, FusionObject->Id };

	// Build property state mapping from type descriptor
	if (const TStrongObjectPtr<UFusionTypeDescriptor>* DescPtr = Lookup->HashToDescriptor.Find(FusionObject->Type.Hash))
	{
		if (DescPtr->IsValid())
		{
			const UFusionTypeDescriptor* Desc = DescPtr->Get();
			Pair.PropertyStates.Reserve(Desc->Properties.Num());
			for (Property* Prop : Desc->Properties)
			{
				Prop->BuildState(Pair.PropertyStates);
			}
		}
	}

	if (ObjectIdToPair.Contains(FusionObject->Id))
	{
		FUSION_LOG("duplicate2");
	}
	// mapping for id => actor+obj pair, this has
	// to be kept like this so that unreals garbage collector see it
	ObjectIdToPair.Add(FusionObject->Id, Pair);

	// mapping of AActor* to object Id
	ObjectToObjectId.Add(Object, FusionObject->Id);

	if (const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(CurrentWorld.Get()))
	{
		if (UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>())
		{
			OnlineSubsystem->OnObjectReadyEvent.Broadcast(Object);
		}
	}

	return Pair;
}

FFusionObjectActorPair UFusionClient::RegisterRuntimeObject(UFusionActorComponent* Source, AActor* OwningActor, UObject* Object, FusionCore::Object* FusionObject, EFusionObjectPairType Type)
{
	if (Object == nullptr)
	{
		FUSION_LOG_WARN("Object was null");
		return {};
	}

	if (FusionObject == nullptr)
	{
		FUSION_LOG_WARN("Obj was null");
		return {};
	}

	FPropertyBuildOptions BuildOptions = UFusionTypeLookup::GetDefaultBuildOptions();
	
	const UFusionTypeDescriptor* Desc = Lookup->CreateTypeDescriptor(Object->GetClass(), BuildOptions);

	//
	FUSION_LOG("Shared Mode Object Registered %s (%s) [hash/id: %llu]", *ObjectIdToString(FusionObject->Id), *Object->GetName(), Desc->TypeHash);

	// store actor reference on object also, but this will
	// not keep the object alive from GC pov.
	FusionObject->Engine = Object;
	
	// mapping of AActor* to object Id
	TempObjectToObjectId.Add(Object, FusionObject->Id);

	FFusionObjectActorPair Pair = FFusionObjectActorPair{ Type, OwningActor, Object, Source, FusionObject, FusionObject->Id };

	// Build property state mapping from type descriptor
	Pair.PropertyStates.Reserve(Desc->Properties.Num());
	for (Property* Prop : Desc->Properties)
	{
		Prop->BuildState(Pair.PropertyStates);
	}
	
	// mapping for id => actor+obj pair, this has
	// to be kept like this so that unreals garbage collector see it
	TempObjectIdToPair.Add(FusionObject->Id, Pair);

	if (const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(CurrentWorld.Get()))
	{
		if (UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>())
		{
			OnlineSubsystem->OnObjectReadyEvent.Broadcast(Object);
		}
	}

	return Pair;
}

void UFusionClient::SetMapState(EMapState NewMapState)
{
	FUSION_LOG("UFusionClient::SetMapState, IsMasterClient(%d) Previous Map State: %s    New Map State: %s", Client->IsMasterClient(), *UEnum::GetValueAsName(CurrentMapState).ToString(), *UEnum::GetValueAsName(NewMapState).ToString());
	CurrentMapState = NewMapState;
}

void UFusionClient::AttachMapActor(UFusionActorComponent* Source, const uint32 MapSequence, bool SendUpdates)
{
	AActor* Owner = Source->GetOwner();
	const uint32 MapActorHash = UFusionHelpers::SafeObjectNameHash(Owner);
	
	if (DestroyedMapActors.Contains(MapSequence))
	{
		FKeyObjectId MapActorId(0, MapSequence, MapActorHash);
		//Check if map actor is in the destroyed list.
		if (DestroyedMapActors[MapSequence].Contains(MapActorId))
		{
			FUSION_LOG_WARN("MapActor: %s is already destroyed, skipping", *Owner->GetName());

			Source->OnObjectDestroyed.Broadcast(EFusionObjectDestroyMode::Remote); //Assume its remote since only remote calls can add to the DestroyedMapActors map.
			Owner->Destroy();
			return;
		}
	}

	//Get actual runtime components but only those that exist in the CDO (class-defined).
	//The runtime added components are processed elsewhere.
	//CDO-defined components should be deterministic for all clients loading the map.
	TSet<UActorComponent*> Components;                                                                                                                                                                                                                                                                                                                                                                                           
	for (UActorComponent* Component : Owner->GetComponents())                                                                                                                                                                                                                                                                                                                                              
	{
		if (Component->CreationMethod == EComponentCreationMethod::Native ||
			Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript)
		{
			Components.Add(Component);
		}
	}

	TArray<FTypeData> TypesData{};
	FusionCore::ObjectOwnerModes OwnerMode = Source->GetTypes(this, Components, Owner->GetRootComponent(), TypesData);

	uint64 WordCount = 0;
	for (FTypeData Item : TypesData)
		WordCount += Item.TypeRef.WordCount;
	
	FTypeData BaseTypeData = TypesData[0];
	
	const int32 SubObjectCount = TypesData.Num() - 1;
	FUSION_LOG("Attempt To Attach Map Actor: %s Index: %d, Map: %d, words %llu",  *Source->GetOwner()->GetName(), MapActorHash, MapSequence, WordCount);
	
	uint32_t MapObjectFlags = static_cast<uint32_t>(EObjectSpecialFlags::SceneObject) | static_cast<uint32_t>(EObjectSpecialFlags::ExistsOnClient);
	
	bool bExisting{false};	
	if (FusionCore::ObjectRoot* Obj = Client->CreateMapObject(bExisting, BaseTypeData.TypeRef.WordCount, BaseTypeData.TypeRef, nullptr, 0, MapSequence, MapActorHash, OwnerMode, MapObjectFlags, SubObjectCount))
	{
		Obj->SetSendUpdates(SendUpdates);
		
		Source->SubscribeEvents(Client, Obj->Id);
		
		AActor* Actor = Cast<AActor>(BaseTypeData.Object.Get());
		FFusionObjectActorPair Pair = RegisterObject(Source, Actor, BaseTypeData.Object.Get(), Obj, EFusionObjectPairType::Actor);

		PhotonCommon::StringType ObjectIdString = Obj->Id;
		FUSION_LOG("Created Object Id: %s   Map: %d   WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(ObjectIdString.c_str()), Obj->GetMap(), Obj->Words.Length, *BaseTypeData.Object->GetName());

		auto RequiredObjects = Obj->RequiredObjects();
		int32 RequiredObjectIndex = 0;

		//iterate subobjects
		for (int i = 1; i < TypesData.Num(); i++)
		{
			int SubObjectIndex = i - 1;
			FTypeData SubObjectTypeData = TypesData[i];

			const uint32 SubObjectHash = UFusionHelpers::SafeObjectNameHash(SubObjectTypeData.Object.Get());

			if (TStrongObjectPtr<UFusionTypeDescriptor> Descriptor = Lookup->HashToDescriptor.FindRef(SubObjectTypeData.TypeRef.Hash))
			{
				FString const SubObjectClassPath = Descriptor->Type.Get()->GetPathName();

				TArray SubObjectsTypesData{SubObjectTypeData};
				FString SubObjectTypesJson = UFusionHelpers::GetTypesHeader(this, SubObjectsTypesData);
				const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> SubObjectTypesJsonUTF8 = StringCast<UTF8CHAR>(*SubObjectTypesJson);

				//Similar to parent container object we check if the subobjects just needs to get registered or created.
				//We dont register mapactors or their subobjects in the OnObjectCreatedFinalize.
				//This could potentially run in paralell with a remote scene object being created on the client.
				FusionCore::Object* ExistingSubObject = Client->FindSubObjectWithHash(Obj, SubObjectHash);

				if (!ExistingSubObject)
				{
					SharedMode::PlayerId MapObjectId = static_cast<SharedMode::PlayerId>((MapActorHash >> 16) ^ (MapActorHash & 0xFFFF));
					SharedMode::ObjectId SubObjectId{ MapObjectId, Obj->GetMap(), SubObjectHash};

					uint32_t SubObjectFlags = MapObjectFlags | static_cast<uint32_t>(SubObjectTypeData.SpecialFlags);
					
					FusionCore::ObjectChild* SubObject = Client->CreateSubObject(Obj->Id,
														 SubObjectTypeData.TypeRef.WordCount,
														 SubObjectTypeData.TypeRef,
														 reinterpret_cast<const PhotonCommon::CharType*>(SubObjectTypesJsonUTF8.Get()),
														 SubObjectTypesJsonUTF8.Length(),
														 SubObjectHash,
														 SubObjectId,
														 SubObjectFlags);

					if (Client->AddSubObject(Obj, SubObject))
					{
						if (!RequiredObjects.empty() && RequiredObjectIndex < SubObjectCount)
						{
							RequiredObjects[RequiredObjectIndex++] = SubObject->Id;
						}

						FFusionObjectActorPair SubObjectPair = RegisterObject(Source, Actor, SubObjectTypeData.Object.Get(), SubObject, EFusionObjectPairType::Component);
						CopyLocalStateToObject(SubObjectPair);

						PhotonCommon::StringType SubObjectIdString = SubObject->Id;
						FUSION_LOG("Created Object Id: %s   ObjectIndex: %d  WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(SubObjectIdString.c_str()), SubObjectIndex,  SubObject->Words.Length, *SubObjectTypeData.Object->GetName());
					}
					else
					{
						PhotonCommon::StringType ParentId = Obj->Id;
						FUSION_LOG_ERROR("Subobject: %llu with hash: %u  and name: %s  is already added to parent: %s", SubObjectTypeData.TypeRef.Hash, SubObjectHash, *SubObjectTypeData.Object->GetName(), UTF8_TO_TCHAR(ParentId.c_str()));
					}
				}
				else
				{
					//Remote update already came in for object, we just need to update our registers.
					FFusionObjectActorPair SubObjectPair = RegisterObject(Source, Actor, SubObjectTypeData.Object.Get(), ExistingSubObject, EFusionObjectPairType::Component);
					
					FCopyContext SubObjectContext
					{
						SubObjectPair,
						this,
						Source->PackageSettings(),
					};
					CopyRemoteStateToObject(SubObjectContext, SubObjectPair, true);
				}
			}
			else {
				FUSION_LOG_ERROR("Unable to find type descriptor for SubObject Type %llu  Object with hash: %d", SubObjectTypeData.TypeRef.Hash, SubObjectHash);
			}
		}
	
		//
		if (bExisting)
		{
			FCopyContext Context
			{
				Pair,
				this,
				Source->PackageSettings(),
			};
			CopyRemoteStateToObject(Context, Pair, true);
			FUSION_LOG("Already Existed, using remote state");
		}
		else
		{
			CopyLocalStateToObject(Pair);
		}

		Source->OnObjectReady.Broadcast();
	}
}

void UFusionClient::AttachGlobalInstanceActor(UFusionActorComponent* Source, const uint32 MapSequence, UObject* Object)
{
	constexpr FusionCore::ObjectOwnerModes OwnerMode = FusionCore::ObjectOwnerModes::GameGlobal;
	TArray<FTypeData> TypesData{};
	
	if (Source)
	{
		AActor* Owner = Source->GetOwner();

		//Get actual runtime components but only those that exist in the CDO (class-defined).
		//The runtime added components are processed elsewhere.
		//CDO-defined components should be deterministic for all clients loading the map.
		TSet<UActorComponent*> Components;                                                                                                                                                                                                                                                                                                                                                                                           
		for (UActorComponent* Component : Owner->GetComponents())                                                                                                                                                                                                                                                                                                                                              
		{
			if (Component->CreationMethod == EComponentCreationMethod::Native ||
				Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript)
			{
				Components.Add(Component);
			}
		}
		
		Source->GetTypes(this, Components, Owner->GetRootComponent(), TypesData);
	}
	else
	{
		FPropertyBuildOptions BuildOptions = UFusionTypeLookup::GetDefaultBuildOptions();
		const UFusionTypeDescriptor* TypeDescriptor = Lookup->CreateTypeDescriptor(Object->GetClass(), BuildOptions);
		const FusionCore::TypeRef TypeRef = FusionCore::TypeRef{TypeDescriptor->TypeHash, TypeDescriptor->WordCount};
		
		const FTypeData TypeData {
			TypeRef,
			Object
		};
		TypesData.Add(TypeData);
	}
	
	//Condense the spawned actor into json
	FString TypesJson = UFusionHelpers::GetTypesHeader(this, TypesData);
	const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> TypesJsonUTF8 = StringCast<UTF8CHAR>(*TypesJson);

	//Assume only 1 object of this class can exists on each client.
	FString Name = *Object->GetClass()->GetName();
	const uint32 Hash = UFusionHelpers::SafeObjectNameHash(TCHAR_TO_ANSI(*Name), Name.Len());

	bool bExisting{false};

	//This way we dont create this again on all other clients, assume they make their locally and we just apply whatever state is needed.
	uint32_t Flags = static_cast<uint32_t>(EObjectSpecialFlags::ExistsOnClient);

	FTypeData BaseTypeData = TypesData[0];
	
	if (auto* Obj = Client->CreateGlobalInstanceObject(bExisting, BaseTypeData.TypeRef.WordCount, BaseTypeData.TypeRef,
										reinterpret_cast<const PhotonCommon::CharType*>(TypesJsonUTF8.Get()),
										TypesJsonUTF8.Length(),
										MapSequence,
										Hash,
										OwnerMode,
										Flags))
	{
		FFusionObjectActorPair ObjectPair = RegisterObject(Source, nullptr, Object, Obj, EFusionObjectPairType::GlobalInstance);
		if (bExisting)
		{
			FCopyContext Context
			{
				ObjectPair,
				this,
				FPackagedSettings{},
			};
			CopyRemoteStateToObject(Context, ObjectPair, true);
			FUSION_LOG("Already Existed, using remote state");
		}
		else {
			CopyLocalStateToObject(ObjectPair);
		}

		const PhotonCommon::StringType ObjectIdString = Obj->Id;
		FUSION_LOG("Created Global Instance Object Id: %s  Map: %d   WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(ObjectIdString.c_str()), Obj->GetMap(), Obj->Words.Length, *Object->GetName());
	}
}

void UFusionClient::AttachSpawnedActor(UFusionActorComponent* Source, const uint32 Scene, bool SendUpdates)
{
	AActor* Owner = Source->GetOwner();
		
	TArray<FTypeData> TypesData{};
	FusionCore::ObjectOwnerModes OwnerMode = Source->GetTypes(this, Owner->GetComponents(), Owner->GetRootComponent(), TypesData);

	uint64 WordCount = 0;
	for (FTypeData Item : TypesData)
		WordCount += Item.TypeRef.WordCount;
	
	FUSION_LOG("Attempt To Attach Spawned Actor: %s  Map: %d, words %llu", *Source->GetOwner()->GetName(), Scene, WordCount);

	//Condense the spawned actor into json
	FString TypesJson = UFusionHelpers::GetTypesHeader(this, TypesData);
	const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> TypesJsonUTF8 = StringCast<UTF8CHAR>(*TypesJson);

	FTypeData BaseTypeData = TypesData[0];

	const int32 SubObjectCount = TypesData.Num() - 1;

	FusionCore::ObjectId PreconfiguredId;
	uint32 ActiveScene = Scene;
	if (Owner->IsA<APlayerState>())
	{
		if (FusionCore::ObjectRoot* Existing = Client->FindObjectRoot(CurrentPlayerStateId))
		{
			//Since player states have a fixed hash we must sure only 1 exists per player between map changes, it will not manually destroy itself.
			Client->DestroyObjectLocal(Existing, true);
			RemoveObjectRoot(Existing);
		}
		
		PreconfiguredId = Client->GetNewObjectId(0);
		ActiveScene = 0;
		CurrentPlayerStateId = PreconfiguredId;
	}
	else if (Owner->IsA(AGameStateBase::StaticClass()))
	{
		AttachGlobalInstanceActor(Source, Scene, Owner);
		return;
	}
	

	uint32_t EngineObjectFlags = static_cast<uint32_t>(Source->FusionObjectFlags);
	
	if (FusionCore::ObjectRoot* Obj = Client->CreateObject(BaseTypeData.TypeRef.WordCount, BaseTypeData.TypeRef,
														   reinterpret_cast<const PhotonCommon::CharType*>(TypesJsonUTF8.Get()), TypesJsonUTF8.Length(),
														   ActiveScene, OwnerMode, EngineObjectFlags, SubObjectCount, PreconfiguredId))
	{
		Obj->SetSendUpdates(SendUpdates);
		
		Source->SubscribeEvents(Client, Obj->Id);

		AActor* Actor = Cast<AActor>(BaseTypeData.Object.Get());
		FFusionObjectActorPair ObjectPair = RegisterObject(Source, Actor, BaseTypeData.Object.Get(), Obj, EFusionObjectPairType::Actor);
		CopyLocalStateToObject(ObjectPair);

		PhotonCommon::StringType ObjectIdString = Obj->Id;
		FUSION_LOG("Created Object Id: %s  Map: %d   WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(ObjectIdString.c_str()), Obj->GetMap(), Obj->Words.Length, *BaseTypeData.Object->GetName());

		auto RequiredObjects = Obj->RequiredObjects();
		int32 RequiredObjectIndex = 0;

		for (int i = 1; i < TypesData.Num(); i++) {
			int SubObjectIndex = i - 1;
			FTypeData SubObjectTypeData = TypesData[i];

			const uint32 SubObjectHash = UFusionHelpers::SafeObjectNameHash(SubObjectTypeData.Object.Get());

			if (TStrongObjectPtr<UFusionTypeDescriptor> Descriptor = Lookup->HashToDescriptor.FindRef(SubObjectTypeData.TypeRef.Hash))
			{
				FString SubObjectClassPath = Descriptor->Type.Get()->GetPathName();
				TArray SubObjectsTypesData{SubObjectTypeData};
				FString SubObjectTypesJson = UFusionHelpers::GetTypesHeader(this, SubObjectsTypesData);
				const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> SubObjectTypesJsonUTF8 = StringCast<UTF8CHAR>(*SubObjectTypesJson);

				//Dynamic subobjects will get their ids same way as parent object.
				FusionCore::ObjectId SubObjectId = Client->GetNewObjectId(Obj->GetMap());

				FusionCore::ObjectChild* SubObject = Client->CreateSubObject(Obj->Id, SubObjectTypeData.TypeRef.WordCount,
					SubObjectTypeData.TypeRef,
					reinterpret_cast<const PhotonCommon::CharType*>(SubObjectTypesJsonUTF8.Get()),
					SubObjectTypesJsonUTF8.Length(),
					SubObjectHash,
					SubObjectId,
					static_cast<uint32_t>(SubObjectTypeData.SpecialFlags));

				if (Client->AddSubObject(Obj, SubObject))
				{
					if (!RequiredObjects.empty() && RequiredObjectIndex < SubObjectCount)
					{
						RequiredObjects[RequiredObjectIndex++] = SubObject->Id;
					}

					auto SubObjectPair = RegisterObject(Source, Actor, SubObjectTypeData.Object.Get(), SubObject, EFusionObjectPairType::Component);
					CopyLocalStateToObject(SubObjectPair);

					PhotonCommon::StringType SubObjectIdString = SubObject->Id;
					FUSION_LOG("Created Object Id: %s   ObjectIndex: %d    WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(SubObjectIdString.c_str()), SubObjectIndex, SubObject->Words.Length, *SubObjectTypeData.Object->GetName());
				}
				else {
					PhotonCommon::StringType ParentId = Obj->Id;
					FUSION_LOG_ERROR("Subobject: %llu with hash: %d  and name: %s  is already added to parent: %s", SubObjectTypeData.TypeRef.Hash, SubObjectHash, *SubObjectTypeData.Object->GetName(), UTF8_TO_TCHAR(ParentId.c_str()));
				}
			}
			else
			{
				FUSION_LOG_ERROR("Unable to find type descriptor for SubObject Type %llu  Object with hash: %d", SubObjectTypeData.TypeRef.Hash, SubObjectHash);
			}
		}

		Source->OnObjectReady.Broadcast();
	}
}

FusionCore::ObjectRoot* UFusionClient::FindRootParent(FusionCore::ObjectId Id)
{
	if (FusionCore::Object* Object = Client->FindObject(Id))
	{
		if (FusionCore::ObjectRoot* Root = FusionCore::ObjectRoot::Cast(Object))
		{
			return Root;
		}

		if (const FusionCore::ObjectChild* Child = FusionCore::ObjectChild::Cast(Object))
		{
			if (FusionCore::ObjectRoot* FoundParent = FindRootParent(FusionCore::ObjectChild::GetParent(Child)))
			{
				return FusionCore::ObjectRoot::Cast(FoundParent);
			}
		}
	}
	
	return nullptr;
}

FusionCore::Object* UFusionClient::CreateCustomObject(const FCopyContext& Context, UObject* Object, const UFusionTypeDescriptor* Descriptor, uint32 Scene)
{
	FusionCore::ObjectRoot* ParentObject{nullptr};
	if (const UObject* Outer = Object->GetOuter()) {
		if (FusionCore::ObjectRoot* FoundObject = Context.FusionClient->FindObjectRoot(Outer))
		{
			ParentObject = FoundObject;
		}
	}
	
	if (!ParentObject) {
		//Disabled for now, we only ever allow custom objects (subobjects) to be created if they have a network parent.
		//Backup, use the object sitting at the top/root of the copy chain
		//ParentObject = FindRootParent(Context.Pair.Object->Id);
	}

	FusionCore::ObjectId ParentId = ParentObject ? ParentObject->Id : FusionCore::ObjectId();

	if (ParentId.IsSome())
	{
		FFusionObjectActorPair ParentPair = FindObjectPair(ParentId);
		const FusionCore::TypeRef TypeRef = FusionCore::TypeRef{Descriptor->TypeHash, Descriptor->WordCount};
		
		TArray<FTypeData> TypesData{};
		const FTypeData TypeData {
			TypeRef,
			Object
		};
		TypesData.Add(TypeData);
		
		const uint32 SubObjectHash = UFusionHelpers::SafeObjectNameHash(Object);
		
		const FString TypesJson = UFusionHelpers::GetTypesHeader(this, TypesData);
		const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> TypesJsonUTF8 = StringCast<UTF8CHAR>(*TypesJson);
		
		FusionCore::ObjectId SubObjectId =  Client->GetNewObjectId(ParentId.Map);

		FusionCore::ObjectChild* SubObject = Client->CreateSubObject(ParentObject->Id,
														 TypeRef.WordCount,
														 TypeRef,
														 reinterpret_cast<const PhotonCommon::CharType*>(TypesJsonUTF8.Get()),
														 TypesJsonUTF8.Length(),
														 SubObjectHash,
														 SubObjectId,
														 {});
		Client->AddSubObject(ParentObject, SubObject);
		const FFusionObjectActorPair Pair = RegisterRuntimeObject(nullptr, ParentPair.Actor, Object, SubObject, EFusionObjectPairType::CustomObject);
		const PhotonCommon::StringType ObjectIdString = SubObject->Id;
		FUSION_LOG("Created Custom Object Id: %s   Object Hash: %d    WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(ObjectIdString.c_str()), SubObjectHash,  SubObject->Words.Length, *Object->GetName());

		return Pair.Object;
	}

	return nullptr;
}

void UFusionClient::TriggerMapLoad()
{
	if (Client->IsMasterClient())
	{
		FUSION_LOG_ERROR("Attempting to trigger a map load but the client is master.");
		return;
	}
		
	if (RequestedMapInstances.Num() < 1)
	{
		FUSION_LOG_ERROR("Attempting to trigger a map load but the client has no map to load.");
		return;
	}
	
	FMapInstance NewLevelInstance;
	RequestedMapInstances.HeapPop(NewLevelInstance);

	CurrentMapInstance.Name = NewLevelInstance.Name;
	CurrentMapInstance.Sequence = NewLevelInstance.Sequence;
	
	TargetMapInstance.Name = FPackageName::GetShortName(NewLevelInstance.Name);

	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(CurrentWorld.Get());
	if (!GameInstance)
	{
		FUSION_LOG_ERROR("Invalid GameInstance when attempting to trigger map load");
		return;
	}
	UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem)
	{
		FUSION_LOG_ERROR("Invalid OnlineSubsystem when attempting to trigger map load");
		return;
	}
	OnlineSubsystem->OnMapLoadRequestedEvent.Broadcast(CurrentMapInstance.Name);

	if (const UFusionOnlineSubsystemSettings* Settings =
		UFusionOnlineSubsystemSettings::GetPhotonOnlineSettings()) {
		bool bLoadMapAutomatically = Settings->LoadMapAutomatically;

		if (FusionCVars::LoadMapBehaviourOverride > 0) {
			bLoadMapAutomatically = FusionCVars::LoadMapBehaviourOverride == 1;
		}

		if (bLoadMapAutomatically)
		{
			if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(CurrentWorld.Get()))
			{
				TravelInternal(CurrentMapInstance.Name);
				PreMapLoad(*WorldContext, CurrentMapInstance.Name, true);
			}
		}
		else
		{
			// Set state to now loading and remain in that state until client code loads the map
			SetMapState(EMapState::IsLoading);

			//Broadcast new map request and let developer handle it.
			OnlineSubsystem->OnMapLoadPerformEvent.Broadcast(CurrentMapInstance.Name);
		}
	}
}

void UFusionClient::TriggerMapLoadedCallback()
{
	if (CurrentWorld.Get())
	{
		if (const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(CurrentWorld.Get()))
		{
			if (UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>())
			{
				OnlineSubsystem->OnMapLoadDoneEvent.Broadcast(CurrentMapInstance.Name);
			}
		}
	}
	else
	{
		FUSION_LOG_ERROR("Invalid game world when attempting to trigger the map loaded callback");
	}
}

void UFusionClient::InitializeNewRemoteObjects()
{
	for (int i = 0; i < NewRemoteObjectRoots.Num(); i++)
	{
		FusionCore::ObjectRoot* Obj = NewRemoteObjectRoots[i];

		//Assume local client has locally connected these objects.
		if ((Obj->EngineFlags & static_cast<uint32_t>(EObjectSpecialFlags::ExistsOnClient)) != 0)
		{
			NewRemoteObjectRoots.RemoveAt(i);
			i--;
			continue;
		}

		// belongs to an old scene, ignore.
		if (Obj->GetMap() < CurrentMapInstance.Sequence && Obj->GetMap() != 0)
		{
			NewRemoteObjectRoots.RemoveAt(i);
			i--;
			continue;
		}
		
		// belongs to a newer scene, ignore but don't remove
		if (Obj->GetMap() > CurrentMapInstance.Sequence)
		{
			continue;
		}

		if (Obj->GetMap() == CurrentMapInstance.Sequence || Obj->GetMap() == 0)
		{
			if (Obj->Engine == nullptr)
			{
				if (OnObjectCreatedFinalize(Obj))
				{
					NewRemoteObjectRoots.RemoveAt(i);
					i--;
				}
			}
			else
			{
				NewRemoteObjectRoots.RemoveAt(i);
				i--;
			}
		}
	}

	for (int i = 0; i < NewRemoteObjectChildren.Num(); i++)
	{
		FusionCore::ObjectChild* Obj = NewRemoteObjectChildren[i];
		const FusionCore::ObjectRoot* Root = Client->GetRoot(Obj);

		if (Root == nullptr)
		{
			NewRemoteObjectChildren.RemoveAt(i);
			i--;
			continue;
		}

		//Assume local client has locally connected these objects.
		if ((Obj->EngineFlags & static_cast<uint32_t>(EObjectSpecialFlags::ExistsOnClient)) != 0)
		{
			NewRemoteObjectChildren.RemoveAt(i);
			i--;
			continue;
		}
		
		if (Root->GetMap() < CurrentMapInstance.Sequence && Root->GetMap() != 0)
		{
			NewRemoteObjectChildren.RemoveAt(i);
			i--;
			continue;
		}
		
		// belongs to a newer scene, ignore but don't remove
		if (Root->GetMap() > CurrentMapInstance.Sequence)
		{
			continue;
		}

		//Wait for root object to have unreal engine object assigned.
		if (Root->Engine == nullptr)
		{
			continue;
		}

		if (Obj->Engine == nullptr)
		{
			if (OnObjectCreatedFinalize(Obj))
			{
				NewRemoteObjectChildren.RemoveAt(i);
				i--;
			}
		}
		else
		{
			NewRemoteObjectChildren.RemoveAt(i);
			i--;
		}
	}
}

void UFusionClient::InitializeNewLocalAndMapObjects()
{
	for (int i = 0; i < PendingObjects.Num(); i++)
	{
		const FPendingObject Pending = PendingObjects[i];

		if (IsValid(Pending.Object) == false)
		{
			FUSION_LOG_ERROR("Pending Actor Object is invalid");
			PendingObjects.RemoveAt(i);
			i--;
			continue;
		}

		const TObjectPtr<UFusionActorComponent> Source = Pending.Source;
		FUSION_LOG("Process Pending Actor: %s Map: %d", *Pending.Object->GetName(), CurrentMapInstance.Sequence);

		if (this->ObjectToObjectId.Contains(Pending.Object))
		{
			FUSION_LOG("Actor: %s already exists in mapped objects", *Pending.Object->GetName());
			PendingObjects.RemoveAt(i);
			i--;
			continue;
		}

		FUSION_LOG("Process Pending Actor: %s Scene: %d", *Pending.Object->GetName(), CurrentMapInstance.Sequence);

		if (Pending.Object->IsA(UGameInstance::StaticClass()))
		{
			AttachGlobalInstanceActor(Source, 0, Pending.Object);
		}
		else
		{
			if (const AActor* Actor = Cast<AActor>(Pending.Object))
			{
				if (Actor->HasAnyFlags(RF_Transient) || !Actor->bNetStartup)
				{
					AttachSpawnedActor(Source, CurrentMapInstance.Sequence, Source->bAutomaticallySendUpdates);
				}
				else
				{
					AttachMapActor(Source, CurrentMapInstance.Sequence, Source->bAutomaticallySendUpdates);
				}
			}
		}

		PendingObjects.RemoveAt(i);
		i--;
	}
}

void UFusionClient::UpdateRemoteObjectsActorState(const double Dt)
{
	for (auto& [_, Pair] : ObjectIdToPair)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::UpdateRemoteObjectsActorState::ValidateAndLookup);

		FusionCore::Object* Current = Client->FindObject(Pair.ObjectId);

		if (!Current)
		{
			RemoveAfterEndBeginFrame.Add(Pair.ObjectId);
			continue;
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::BeginFrame::UpdateRemoteObjectsActorState::GetRoot);
			const FusionCore::ObjectRoot* Root = Client->GetRoot(Current);
			if (!Root) {
				FUSION_LOG_ERROR("GetRoot returned null for object [%d:%d]", Pair.Object->Id.Origin, Pair.Object->Id.Counter);
				continue;
			}

			if (Root->GetMap() != CurrentMapInstance.Sequence && Root->GetMap() != 0)
				continue;
		}

		if (!IsValid(Pair.EngineObject)) {
			RemoveAfterEndBeginFrame.Add(Pair.Object->Id);
			FUSION_LOG_WARN("Found pair without valid actor: [%d:%d]", Pair.Object->Id.Origin, Pair.Object->Id.Counter);
			continue;
		}

		bool CanModify;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::BeginFrame::UpdateRemoteObjectsActorState::CanModify);
			CanModify = Client->CanModify(Pair.Object);
		}

		if (CanModify) {
			if (Pair.Actor)
			{
				Pair.Actor->SetRole(ROLE_Authority);
			}

			continue;
		}
		if (Pair.Actor)
		{
			Pair.Actor->SetRole(ROLE_SimulatedProxy);
		}

		UFusionActorComponent* Settings{nullptr};
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::BeginFrame::UpdateRemoteObjectsActorState::GetSettings);

			if (FusionCore::ObjectChild* Child = FusionCore::ObjectChild::Cast(Pair.Object)) {
				const FusionCore::ObjectId ParentId = FusionCore::ObjectId(FusionCore::ObjectChild::GetParent(Child));
				if (const FFusionObjectActorPair ParentPair = FindObjectPair(ParentId); ParentPair.Actor) {
					Settings = ParentPair.Actor->GetComponentByClass<UFusionActorComponent>();
				}
			}
			else {
				if (Pair.Actor) {
					Settings = Pair.Actor->GetComponentByClass<UFusionActorComponent>();
				}
			}
		}

		const FPackagedSettings UpdateSettings = Settings ? Settings->PackageSettings() : FPackagedSettings{};
		UpdateRemoteState(Pair, UpdateSettings, Dt);
	}
}

void UFusionClient::UpdateRemoteState(const FFusionObjectActorPair& Pair, const FPackagedSettings& Settings, const double Dt)
{
	FTransform PreviousTransform;
	FVector PreviousLinVel;
	FVector PreviousAngVel;

	FBodyInstance* BodyInstance = nullptr;

	if (Settings.bForecastPhysicsEnabled) {

		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::BeginFrame::UpdateRemoteObjectsActorState::PreForecast);
		// When we have a component using Forecast physics, before copying/interpolating the values we grab the current body state
		// so it can be reset after the copy/interpolation has happened.
		// This is so the copy/interpolation can still happen for values other than the vales controlled by the Forecast system.
		if (const UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(Pair.EngineObject); PrimComponent && PrimComponent->IsSimulatingPhysics()) {
			if (PrimComponent->GetOwner())
			{
				BodyInstance = PrimComponent->GetBodyInstance(NAME_None);
				PreviousTransform = BodyInstance->GetUnrealWorldTransform();
				PreviousLinVel = BodyInstance->GetUnrealWorldVelocity();
				PreviousAngVel = BodyInstance->GetUnrealWorldAngularVelocityInRadians();
			}
		}
	}

	if (Client->HasBeenUpdatedByPlugin(Pair.Object, true))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::BeginFrame::UpdateRemoteObjectsActorState::CopyRemoteStateToObject);
		FCopyContext Context
		{
			Pair,
			this,
			Settings,
		};
		CopyRemoteStateToObject(Context, Pair, false);
	}

	// If we have a valid bodyInstance then use it to reset the values
	if (BodyInstance)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::BeginFrame::UpdateRemoteObjectsActorState::PostForecast);
		//FUSION_LOG_WARN("Actor name: %s, UPrimitiveComponent: %s", *owner->GetName(), *Pair.EngineObject->GetName());

		BodyInstance->SetBodyTransform(PreviousTransform, ETeleportType::TeleportPhysics, true);
		BodyInstance->SetLinearVelocity(PreviousLinVel, false);
		BodyInstance->SetAngularVelocityInRadians(PreviousAngVel, false);

	}
}

void UFusionClient::TickInRoomAndRunningBeginFrame(const double Dt)
{
	if (!CurrentWorld.IsValid())
		return;

	if (!FusionNetDriver)
		return;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::InitializeNewRemoteObjects);
		InitializeNewRemoteObjects();
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::InitializeNewLocalAndMapObjects);
		InitializeNewLocalAndMapObjects();
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::UpdateRemoteObjectsActorState);
		UpdateRemoteObjectsActorState(Dt);
	}
}

void UFusionClient::TickInRoomAndRunningRemoveActors()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::RemoveActors);

	if (RemoveAfterEndBeginFrame.Num() > 0)
	{
		for (FusionCore::ObjectId Id : RemoveAfterEndBeginFrame)
		{
			ObjectIdToPair.Remove(Id);
		}

		std::vector<UObject*> RemoveActorsAfterLoop{};
		
		for (auto& [Actor, _] : ObjectToObjectId)
		{
			if (!IsValid(Actor))
			{
				RemoveActorsAfterLoop.push_back(Actor);
			}
		}

		for (UObject* Actor : RemoveActorsAfterLoop)
		{
			ObjectToObjectId.Remove(Actor);
		}

		RemoveAfterEndBeginFrame.Empty();
	}
}

void UFusionClient::TickInRoomAndRunningEndFrame(const double Dt)
{
	if (!CurrentWorld.IsValid())
	{
		return;
	}

	TArray<FusionCore::ObjectChild*> SubObjectsToDestroy;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects);

		for (auto& [_, Pair] : ObjectIdToPair)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::ValidateAndLookup);

			const FusionCore::Object* Current;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::FindObject);
				Current = Client->FindObject(Pair.ObjectId);
			}

			if (!Current)
			{
				RemoveAfterEndBeginFrame.Add(Pair.ObjectId);
				continue;
			}

			const FusionCore::ObjectRoot* Root;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::GetRoot);
				Root = Client->GetRoot(Current);
			}
			if (!Root) {
				FUSION_LOG_ERROR("GetRoot returned null for object [%d:%d]", Pair.Object->Id.Origin, Pair.Object->Id.Counter);
				continue;
			}

			if (Root->GetMap() != CurrentMapInstance.Sequence && Root->GetMap() != 0)
				continue;

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::ValidateEngineObject);
				if (!IsValid(Pair.EngineObject))
				{
					RemoveAfterEndBeginFrame.Add(Pair.Object->Id);
					FUSION_LOG_WARN("Found pair without valid actor: [%d:%d]", Pair.Object->Id.Origin, Pair.Object->Id.Counter);
					continue;
				}
			}

			bool CanModify;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::CanModify);
				CanModify = Client->CanModify(Pair.Object);
			}

			if (CanModify)
			{
				bool ShouldCopy = true;

				if (Pair.Settings && Pair.Settings->LocalStateCopyMode == ELocalStateCopyMode::Manual)
				{
					ShouldCopy = Pair.Settings->ConsumePendingLocalStateCopy();
				}

				if (ShouldCopy)
				{
					if (Pair.Actor)
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::CallPreReplication);
						if (FusionNetDriver)
						{
							Pair.Actor->CallPreReplication(FusionNetDriver);
						}
						else
						{
							FUSION_LOG_ERROR("Invalid FusionNetDriver");
						}
					}

					CopyLocalStateToObject(Pair);
				}

			}

			if (Pair.ObjectType == EFusionObjectPairType::Actor && Pair.Actor)
			{
				if (CanModify)
				{
					//Find deleted subobjects
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::FindDeletedSubObjects);
						const std::vector<FusionCore::ObjectId>& RegisteredSubObjects = Client->GetSubObject(Pair.Object);

						if (!RegisteredSubObjects.empty())
						{
							const TSet<UActorComponent*>& ActorComponents = Pair.Actor->GetComponents();

							for (const auto& SubObjectId : RegisteredSubObjects)
							{
								if (const FFusionObjectActorPair* SubObjectActorPair = ObjectIdToPair.Find(SubObjectId);
									SubObjectActorPair && SubObjectActorPair->ObjectType == EFusionObjectPairType::Component)
								{
									if (!ActorComponents.Contains(Cast<UActorComponent>(SubObjectActorPair->EngineObject.Get())))
									{
										if (FusionCore::ObjectChild* Child = FusionCore::ObjectChild::Cast(SubObjectActorPair->Object))
										{
											SubObjectsToDestroy.Add(Child);
										}
									}
								}
							}
						}
					}

					//Check for late registered subobjects
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::LateRegisterSubObjects);
					auto ReplicatedComponents = Pair.Actor->GetReplicatedComponents();
					for  (auto ReplicatedComponent : ReplicatedComponents)
					{
					//This will only work if the object is registered.
					FusionCore::ObjectId ObjectId = FindObjectId(ReplicatedComponent);
					FusionCore::Object* Object = Client->FindObject(ObjectId);

					if (!Object)
					{
						FPropertyBuildOptions BuildOptions = UFusionTypeLookup::GetDefaultBuildOptions();
						UFusionTypeDescriptor* Descriptor = Lookup->CreateTypeDescriptor(ReplicatedComponent->GetClass(), BuildOptions);
						
						FusionCore::TypeRef TypeRef{
							Descriptor->TypeHash,
							Descriptor->WordCount
						};
						
						FTypeData SubObjectTypeData{
							TypeRef,
							ReplicatedComponent,
						};
						
						TArray SubObjectsTypesData{SubObjectTypeData};
						FString SubObjectTypesJson = UFusionHelpers::GetTypesHeader(this, SubObjectsTypesData);
						const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> SubObjectTypesJsonUTF8 = StringCast<UTF8CHAR>(*SubObjectTypesJson);
						
						const uint32 SubObjectHash = UFusionHelpers::SafeObjectNameHash(SubObjectTypeData.Object.Get());

						FusionCore::ObjectRoot* PairRoot = FusionCore::ObjectRoot::Cast(Pair.Object);

						//Similar to parent container object we check if the subobjects just needs to get registered or created.
						//We dont register mapactors or their subobjects in the OnObjectCreatedFinalize.
						//This could potentially run in paralell with a remote scene object being created on the client.
						FusionCore::Object* ExistingSubObject = Client->FindSubObjectWithHash(PairRoot, SubObjectHash);

						//We dont allow adding dynamic component adding on map actors (for now)
						if (!ExistingSubObject)
						{
							FusionCore::ObjectId SubObjectId = Client->GetNewObjectId(Pair.Object->Id.Map);

							auto SubObject = Client->CreateSubObject(Pair.Object->Id, Descriptor->WordCount,
										TypeRef,
										reinterpret_cast<const PhotonCommon::CharType*>(SubObjectTypesJsonUTF8.Get()),
										SubObjectTypesJsonUTF8.Length(),
										SubObjectHash,
										SubObjectId,
										static_cast<uint32_t>(SubObjectTypeData.SpecialFlags));
				

							if (Client->AddSubObject(PairRoot, SubObject))
							{
								auto NewSubObjectPair = RegisterRuntimeObject(nullptr, Pair.Actor, SubObjectTypeData.Object.Get(), SubObject, EFusionObjectPairType::Component);
								CopyLocalStateToObject(NewSubObjectPair);
							}
							else
							{
								PhotonCommon::StringType ParentId = Pair.Object->Id;
								FUSION_LOG_ERROR("Subobject: %llu with hash: %d  and name: %s  is already added to parent: %s", SubObjectTypeData.TypeRef.Hash, SubObjectHash, *SubObjectTypeData.Object->GetName(), UTF8_TO_TCHAR(ParentId.c_str()));
							}
						}
					}
					}
				}
			}
		}
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::DestroySubObjects);

		for (FusionCore::ObjectChild* SubObject : SubObjectsToDestroy)
		{
			FUSION_LOG("Deleted sub object: [%u:%u]", SubObject->Id.Origin, SubObject->Id.Counter);
			Client->DestroySubObjectLocal(SubObject);
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::MergeTempPairs);

		for (auto& Pair : TempObjectIdToPair)
		{
			ObjectIdToPair.Add(Pair.Key, Pair.Value);
			ObjectToObjectId.Add(Pair.Value.EngineObject, Pair.Value.Object->Id);
			//CopyLocalStateToObject(Pair.Value); //Can trigger mutations in TempObjectIdToPair
		}

		TempObjectIdToPair.Empty();
		TempObjectToObjectId.Empty();
	}

}

void UFusionClient::AddSpawnBlockedCls(UClass* InClass)
{
	FUSION_LOG("Add SpawnBlock For Class: %s", *InClass->GetName());
	BlockedClasses.Add(InClass);
}

void UFusionClient::RemoveSpawnBlockedCls(UClass* InClass)
{
	FUSION_LOG("Remove SpawnBlock For Class: %s", *InClass->GetName());
	BlockedClasses.Remove(InClass);
}

void UFusionClient::Tick(const double Dt)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::Tick);

	switch (CurrentMapState) {
		case EMapState::Invalid:
			// Move out of the invalid state in the first tick
			SetMapState(EMapState::LevelActive);
			break;
		
		case EMapState::Shutdown:
			Client->UpdateServiceOnly(); //We need to run load balancer service when shutting down in order for client state to be put into correct state.
			break;

		case EMapState::WaitingToAttach:
			Client->UpdateServiceOnly(); //We need to run load balancer service when shutting down in order for client state to be put into correct state.
			break;
		
		case EMapState::MasterClientChangeWorld:
			if (WorldChangeRequest.bIsActive)
			{
				WorldChangeRequest.bIsActive = false;
				
				if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(CurrentWorld.Get()))
				{
					TravelInternal(WorldChangeRequest.WorldName);
					PreMapLoad(*WorldContext, WorldChangeRequest.WorldName, true);
				}
			}
			else
			{
				FUSION_LOG_ERROR("EMapState::MasterClientChangeWorld is in an invalid state");
			}
			break;
	
		case EMapState::LevelActive:

			// Depending on the initialization order, the PlayerController NetConnection may need to be updated
			if (PlayerController && !PlayerController->NetConnection && FusionNetDriver)
			{
				PlayerController->NetConnection = FusionNetDriver->ServerConnection;
			}


			// If there has been a request to change level then move to the next state.
			// This should only happen on master clients.
			if (RequestedMapInstances.Num() > 0)
			{
				SetMapState(EMapState::HasRequestToChangeLevel);
				return;
			}

			// * yes - this order is in reverse
			// * yes - it's correct, don't change it

			// 1. end current frame, copies modifiable objects to networked state
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::Tick::EndFrame);
				TickInRoomAndRunningEndFrame(Dt);
			}

			// 2. send networked state out
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::Tick::SendState);
				Client->UpdateFrameEnd();
			}

			// 3. begin next frame, receive data from network
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::Tick::ReceiveState);
				Client->UpdateFrameBegin(Dt);
			}

			// 4. create any pending actors and copy remote state into actors we can't modify
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::Tick::BeginFrame);
				TickInRoomAndRunningBeginFrame(Dt);
			}

			// 5. Modify GameMode/GameState
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::Tick::UpdateGameState);
				UpdateGameState();
			}

			// 6. clean up old/invalid actor pairs
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::Tick::RemoveActors);
				TickInRoomAndRunningRemoveActors();
			}

			break;
		case EMapState::HasRequestToChangeLevel:

			// This state can only be entered from LevelActive on non-master clients.
			// Master clients can not enter this state.

			TriggerMapLoad();

			// Next state: In the PreMapLoad() call back we check we are in this state and then move to EMapState::IsLoading

			break;
		case EMapState::IsLoading:

			// This state can be entered from:
			//  * HasRequestToChangeLevel on non-master clients.
			//  * LevelActive on master clients.

			// Next state: In the PostMapLoad() call back we check we are in this state and then move to EMapState::ReadyToNotifyAboutLevelLoad

			break;
		case EMapState::ReadyToNotifyAboutLevelLoad:

			// This state can only be entered from IsLoading.

			TriggerMapLoadedCallback();
		
			// Now loop back to the LevelActive state as the level is now loaded and callbacks have been called.
			SetMapState(EMapState::LevelActive);

			break;
	}
}


void UFusionClient::TriggerLevelChanged(const FString& MapName, bool AttachCurrent /*= false*/)
{
	if (Client->IsMasterClient())
	{
		CurrentMapInstance.Name = MapName;

		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("MapName"), CurrentMapInstance.Name);
		Payload->SetBoolField(TEXT("Attached"), AttachCurrent);
    
		FString PayloadString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadString);
		FJsonSerializer::Serialize(Payload, Writer);

		const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> MapNameUTF8 = StringCast<UTF8CHAR>(*PayloadString);
		CurrentMapInstance.Sequence = Client->MapChange(reinterpret_cast<const PhotonCommon::CharType*>(MapNameUTF8.Get()));
		FUSION_LOG("Starting to load map (mc): %s (%i)", *CurrentMapInstance.Name, CurrentMapInstance.Sequence);
	}
	else
	{
		FUSION_LOG("Starting to load map (rc): %s (%i)", *MapName, this->CurrentMapInstance.Sequence);
	}
}

double UFusionClient::NetworkTime()
{
	if (Client)
	{
		return Client->NetworkTime();
	}

	return 0;
}

double UFusionClient::NetworkTimeScale()
{
	if (Client)
	{
		return Client->NetworkTimeScale();
	}

	return 0;
}

double UFusionClient::ActorNetworkTime(const AActor* Actor)
{
	if (const FusionCore::ObjectId Id = FindObjectId(Actor); Id.IsSome())
	{
		if (const FFusionObjectActorPair* Pair = ObjectIdToPair.Find(Id))
		{
			return Client->GetTime(Pair->Object);
		}
	}

	return 0;
}

// Recursively update change tracking for the receive path. For leaf states, compares
// against PreviousReceivedWords. For states with sub-properties, recurses and sums.
static void UpdateReceivedWordStates(FPropertyWordState& State, const SharedMode::Word* Current)
{
	if (State.SubPropertyStates.Num() > 0)
	{
		int32 TotalChanged = 0;
		for (FPropertyWordState& Sub : State.SubPropertyStates)
		{
			UpdateReceivedWordStates(Sub, Current + Sub.WordOffset);
			TotalChanged += Sub.ChangedWordCount;
		}
		State.ChangedWordCount = TotalChanged;
	}
	else
	{
		if (State.PreviousReceivedWords.Num() != State.WordCount)
		{
			State.PreviousReceivedWords.SetNumZeroed(State.WordCount);
		}
		int32 ChangedWords = 0;
		for (int32 i = 0; i < State.WordCount; ++i)
		{
			if (Current[i] != State.PreviousReceivedWords[i])
			{
				++ChangedWords;
				State.PreviousReceivedWords[i] = Current[i];
			}
		}
		State.ChangedWordCount = ChangedWords;
	}
}

// Recursively update change tracking for the send path. Compares Current vs Shadow words.
static void UpdateLocalWordStates(FPropertyWordState& State, const SharedMode::Word* Current, const SharedMode::Word* Shadow)
{
	if (State.SubPropertyStates.Num() > 0)
	{
		int32 TotalChanged = 0;
		for (FPropertyWordState& Sub : State.SubPropertyStates)
		{
			UpdateLocalWordStates(Sub, Current + Sub.WordOffset, Shadow + Sub.WordOffset);
			TotalChanged += Sub.ChangedWordCount;
		}
		State.ChangedWordCount = TotalChanged;
	}
	else
	{
		int32 ChangedWords = 0;
		for (int32 i = 0; i < State.WordCount; ++i)
		{
			if (Current[i] != Shadow[i])
			{
				++ChangedWords;
			}
		}
		State.ChangedWordCount = ChangedWords;
	}
}

void UFusionClient::CopyRemoteStateToObject(FCopyContext& Context, const FFusionObjectActorPair& Pair, const bool IsInitialUpdate)
{
	FusionCore::TypeRef TypeRef = Pair.Object->Type;
	const TStrongObjectPtr<UFusionTypeDescriptor> Desc = Lookup->HashToDescriptor.FindRef(TypeRef.Hash);

	if (!Desc || !Desc->Type) {
		//FUSION_LOG("Failed to find class of type %llu", TypeRef.Hash);
		return;
	}

	UObject* Container;

	bool bSkipPreNetReceive = false;
	bool bSkipPostNetReceive = false;
	
	if (Desc->Type->IsChildOf(AActor::StaticClass())) {
		Container = Pair.EngineObject;
		
		bSkipPreNetReceive = Context.Settings.bSkipPreNetReceive;
		bSkipPostNetReceive = Context.Settings.bSkipPostNetReceive;
	}
	else if (Desc->Type->IsChildOf(UActorComponent::StaticClass())) {
		Container = Pair.EngineObject;
		
		if (Container && Context.Settings.ActorSettings) {
			if (Context.Settings.ActorSettings->ComponentsToSkipPreAndPostNetReceive.FindByPredicate([&Container](const FFusionComponentRef& ComponentRef)
			{
				return ComponentRef.ComponentName == Container->GetName();
			}))
			{
				bSkipPreNetReceive = true;
				bSkipPostNetReceive = true;
			}
		}
	}
	else {
		Container = Pair.EngineObject;
		
		bSkipPreNetReceive = Context.Settings.bSkipPreNetReceive;
		bSkipPostNetReceive = Context.Settings.bSkipPostNetReceive;
	}
	
	if (Container)
	{
		if (!bSkipPreNetReceive) {
			Container->PreNetReceive();
		}
		
		bool IsRootTransform = (Pair.Object->EngineFlags & static_cast<uint32_t>(EObjectSpecialFlags::IsRootTransform)) != 0;
		bool IgnoreRootTransformProperties = (Pair.Object->EngineFlags & static_cast<uint32_t>(EObjectSpecialFlags::IgnoreRootTransformProperties)) != 0;

		const bool bHasMatchingStates = Pair.PropertyStates.Num() == Desc->Properties.Num();
		// PropertyStates is a mutable bookkeeping cache (change-counts, previous-words snapshots)
		// alongside the otherwise-const pair data, so cast away const for that access only.
		TArray<FPropertyWordState>& PropertyStates = const_cast<TArray<FPropertyWordState>&>(Pair.PropertyStates);

		for (int32 Index = 0; Index < Desc->Properties.Num(); ++Index) {
			const Property* Prop = Desc->Properties[Index];
			check(Prop->WordOffset < Pair.Object->Words.Length);

			SharedMode::Word* Current = Pair.Object->Words.Ptr + Prop->WordOffset;

			//Track how many words changed since the last receive. We can't use the shadow buffer
			//as a previous-state reference on the receive path, so each property carries its own
			//snapshot in PropertyStates[Index].PreviousWords.
			if (bHasMatchingStates)
			{
				FPropertyWordState& State = PropertyStates[Index];
				UpdateReceivedWordStates(State, Current);
			}

			//Special case where initial values are updated for properties when object is created.
			//Subsequent updates will be ignored for this property. This is mostly useful for root transform properties that have alternate update paths, eg: being updated by physics or movement component.
			if (!IsInitialUpdate)
			{
				if (Prop->IsTransformProperty && IsRootTransform && IgnoreRootTransformProperties)
				{
					continue;
				}
			}

			Prop->CopyFrom(this, Context, Container, Current, Pair.Object->Shadow.Ptr + Prop->WordOffset);
		}
		
		if (!bSkipPostNetReceive) {
			Container->PostNetReceive();
		}

		InvokeOnReps(Container, Context.OnReps);

		//FUSION_LOG("Updating Container: %s State", *Container->GetName());
		Container->PostRepNotifies();

		//Object has completed a full state update.
		Pair.Object->SetHasValidData();

		//Checks if some other object has a dependency to the current one being updated.
		if (DependencyChecks.Contains(Pair.Object->Id) && Context.bDoDependencyChecks)
		{
			for (FDeferredDependency& Dependency : DependencyChecks[Pair.Object->Id])
			{
				FCopyContext DependencyContext
				{
					Dependency.Pair,
					this,
					Context.Settings,
					false, //Avoids recursive dependency resolves.
				};
				
				//Fully Update the dependencies remote state data
				CopyRemoteStateToObject(DependencyContext, Dependency.Pair, IsInitialUpdate);
				FUSION_LOG("Resolved Dependency: %s Using Object Root: %s", *Dependency.Pair.EngineObject->GetName(), *Pair.EngineObject->GetName());
			}

			//Remove before check to avoid infinite recursions.
			DependencyChecks.Remove(Pair.Object->Id);
		}
	}
}


void UFusionClient::InvokeOnReps(UObject* Container, TSet<FRepValue>& Set)
{
	uint8 NullParams[32]{};
	for (const FRepValue OnRep : Set)
	{
		if (OnRep.RepFunction->NumParms > 0)
		{
			if (OnRep.PreviousPointer)
			{
				uint8* Params = static_cast<uint8*>(FMemory_Alloca(OnRep.RepFunction->ParmsSize));
				FMemory::Memzero(Params, OnRep.RepFunction->ParmsSize);

				if (const FObjectProperty* ObjectProperty = static_cast<FObjectProperty*>(OnRep.Property))
				{
					ObjectProperty->InitializeValue(Params);
					ObjectProperty->SetPropertyValue(Params, OnRep.PreviousPointer);

					Container->ProcessEvent(OnRep.RepFunction, Params);
					continue;
				}
			}
			else if (OnRep.PreviousValueData)
			{
				uint8* Params = static_cast<uint8*>(FMemory_Alloca(OnRep.RepFunction->ParmsSize));
				FMemory::Memzero(Params, OnRep.RepFunction->ParmsSize);

				OnRep.Property->CopyCompleteValue(
					Params,
					OnRep.PreviousValueData
				);
			
				Container->ProcessEvent(OnRep.RepFunction, Params);

				OnRep.Property->DestroyValue(OnRep.PreviousValueData);
				FMemory::Free(OnRep.PreviousValueData);
				continue;
			}
		}

		Container->ProcessEvent(OnRep.RepFunction, NullParams);
	}
}

void UFusionClient::CreateNetDriver(UWorld* World)
{
	const FName NetDriverName = FName(DriverName);
	
	bool bFoundDef = false;
	for (int32 i = 0; i < GEngine->NetDriverDefinitions.Num(); i++)
	{
		if (GEngine->NetDriverDefinitions[i].DefName == NetDriverName)
		{
			bFoundDef = true;
		}
	}
	
	if (!bFoundDef)
	{
		FNetDriverDefinition NewDriverEntry;
	
		NewDriverEntry.DefName = NetDriverName;
		NewDriverEntry.DriverClassName = FusionNetDriverClassName;
	
		// Don't allow fallbacks
		NewDriverEntry.DriverClassNameFallback = NewDriverEntry.DriverClassName;
	
		GEngine->NetDriverDefinitions.Add(NewDriverEntry);
	}

	const bool bMadeNewNetDriver = GEngine->CreateNamedNetDriver(World, NetDriverName, NetDriverName);
	FusionNetDriver = Cast<UFusionNetDriver>(GEngine->FindNamedNetDriver(World, NetDriverName));

	if (PlayerController)
	{
		PlayerController->NetConnection = FusionNetDriver->ServerConnection;
	}

	FUSION_LOG("UFusionClient::CreateNetDriver(%s)   MadeNewNetDriver: %d", *ClientInstanceId.ToString(), bMadeNewNetDriver);
}

void UFusionClient::SetWantOwner(const AActor* Actor)
{
	if (auto* Obj = FindObject(Actor))
	{
		Client->SetWantOwner(Obj);
	}
}
 
void UFusionClient::SetDontWantOwner(const AActor* Actor)
{
	if (auto* Obj = FindObject(Actor))
	{
		Client->SetDontWantOwner(Obj);
	}
}

void UFusionClient::ClearOwnerCooldown(const AActor* Actor)
{
	if (auto* Obj = FindObject(Actor))
	{
		Client->ClearOwnerCooldown(Obj);
	}
}

void UFusionClient::CopyLocalStateToObject(FFusionObjectActorPair& Pair)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyLocalStateToObject);

	FusionCore::TypeRef TypeRef = Pair.Object->Type;
	const TStrongObjectPtr<UFusionTypeDescriptor> Desc = Lookup->HashToDescriptor.FindRef(TypeRef.Hash);

	if (!Desc || !Desc->Type) {
		//FUSION_LOG("Failed to find class of type %llu", TypeRef.Hash);
		return;
	}

	if (Pair.EngineObject) {
		FCopyContext Root
		{
			Pair,
			this
		};
		
		const bool bHasMatchingStates = Pair.PropertyStates.Num() == Desc->Properties.Num();

		for (int32 Index = 0; Index < Desc->Properties.Num(); ++Index)
		{
			const Property* Prop = Desc->Properties[Index];
			const int32 Offset = Prop->WordOffset;
			check(Offset < Pair.Object->Words.Length);

			Prop->CopyTo(this, Root, Pair.PropertyStates[Index], Pair.EngineObject, Pair.Object->Words.Ptr + Offset);

			if (bHasMatchingStates)
			{
				FPropertyWordState& State = Pair.PropertyStates[Index];

				const SharedMode::Word* Current = Pair.Object->Words.Ptr + Offset;
				const SharedMode::Word* Shadow = Pair.Object->Shadow.Ptr + Offset;
				UpdateLocalWordStates(State, Current, Shadow);
			}
		}
	}
}

void UFusionClient::OnActorSpawned(AActor* SpawnedActor)
{
	bool IsConnected = RealtimeClient && RealtimeClient->IsValid() && RealtimeClient->IsConnected();
	if (!IsConnected)
		return;

	if (APlayerController* const SpawnedPlayerController = Cast<APlayerController>(SpawnedActor))
	{
		PlayerController = SpawnedPlayerController;

		if (FusionNetDriver)
		{
			PlayerController->NetConnection = FusionNetDriver->ServerConnection;
		}
	}

	if (SpawnedActor->IsA(AGameStateBase::StaticClass()))
	{
		if (UFusionActorComponent* SourceComp = SpawnedActor->GetComponentByClass<UFusionActorComponent>())
		{
			SourceComp->Ownership = EFusionObjectOwnerFlags::MasterClient;
			
			AddActorSource(SourceComp);
		}
		else
		{
			UFusionActorComponent* NewSource = NewObject<UFusionActorComponent>(SpawnedActor);
			NewSource->Ownership = EFusionObjectOwnerFlags::MasterClient;
			NewSource->bSkipAutoAttach = true;
			NewSource->RegisterComponent();
			
			AddActorSource(NewSource);
		}
	}

	//Temp for now, but we will want to build a whitelist of implicit things to attach here.
	if (SpawnedActor->IsA(APlayerState::StaticClass()))
	{
		if (BlockedClasses.Num() > 0)
		{
			UClass* Cls = SpawnedActor->GetClass();
			const bool bLocked = BlockedClasses.ContainsByPredicate([Cls](const UClass* Current)
			{
				return Cls->IsChildOf(Current);
			});

			if (bLocked)
			{
	 			FUSION_LOG_WARN("Actor Spawned: %s BLOCKED", *SpawnedActor->GetName());
	 			return;
			}
		}

		if (UFusionActorComponent* SourceComp = SpawnedActor->GetComponentByClass<UFusionActorComponent>())
		{
			SourceComp->Ownership = EFusionObjectOwnerFlags::PlayerAttached;
			
			AddActorSource(SourceComp);
		}
		else
		{
			UFusionActorComponent* NewSource = NewObject<UFusionActorComponent>(SpawnedActor);
			NewSource->Ownership = EFusionObjectOwnerFlags::PlayerAttached;
			NewSource->bSkipAutoAttach = true;
			NewSource->RegisterComponent();
			
			AddActorSource(NewSource);
		}
	}
}

void UFusionClient::OnEngineObjectDestroyed(AActor* Actor)
{
	bool IsConnected = RealtimeClient && RealtimeClient->IsValid() && RealtimeClient->IsConnected();
	if (!IsConnected)
		return;
	
	if (RemoteDestroyedObjects.Remove(Actor) > 0)
	{
		return;
	}
	
	FusionCore::ObjectId id = FindObjectId(Actor);
	if (id.IsSome())
	{
		FFusionObjectActorPair Pair = FindObjectPair(id);
		if (Pair.IsValid())
		{
			Pair.Settings->OnObjectDestroyed.Broadcast(EFusionObjectDestroyMode::Local);
			
			FusionCore::ObjectRoot* RootObject = FusionCore::ObjectRoot::Cast(Pair.Object);
			if (Client->DestroyObjectLocal(RootObject, true) == false)
			{
				FUSION_LOG_WARN("Engine destroyed actor %s which we don't have authority to destroy", *Actor->GetName());
			}
		}
		else
		{
			FUSION_LOG_ERROR("Engine destroyed actor %s which doesnt have a mapped object pair", *Actor->GetName());

			if (FusionCore::ObjectRoot* RootObject = FindObjectRoot(Actor))
			{
				if (Client->DestroyObjectLocal(RootObject, true) == false)
				{
					FUSION_LOG_WARN("Engine destroyed actor %s which we don't have authority to destroy", *Actor->GetName());
				}
			}
		}
	}

}

void UFusionClient::UpdateOwnedActorAreaInterestKeys(TFunctionRef<uint64(const AActor*)> KeyFunc)
{
	for (auto& [_, Pair] : ObjectIdToPair)
	{
		if (FusionCore::ObjectRoot::Cast(Pair.Object) == nullptr) continue;
		if (!Pair.Actor) continue;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::UpdateOwnedActorAreaInterestKeys::CanModify);
			if (!Client->CanModify(Pair.Object)) continue;
		}

		if (Client->HasSetInterestKey(Pair.Object) && Client->GetInterestKeyType(Pair.Object) != FusionCore::InterestKeyType::Area) {
			continue;
			}

		if (const uint64 Key = KeyFunc(Pair.Actor); Key != 0) {
			Client->SetAreaInterestKey(Pair.Object, Key);
		}
	}
}

void UFusionClient::UpdateGameState()
{
	auto WorldPtr = CurrentWorld.Get();
	if (!WorldPtr)
		return;
	
	if (AGameStateBase* GameState = WorldPtr->GetGameState())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::Tick::UpdateGameModeTime);

		// ReplicatedWorldTimeSeconds[Double] are protected on AGameStateBase, so reach them via
		// reflection rather than forcing the project to derive from a Fusion-specific GameState.
		static const FDoubleProperty* TimeDoubleProp = FindFProperty<FDoubleProperty>(
			AGameStateBase::StaticClass(), TEXT("ReplicatedWorldTimeSecondsDouble"));
		static const FFloatProperty* TimeFloatProp = FindFProperty<FFloatProperty>(
			AGameStateBase::StaticClass(), TEXT("ReplicatedWorldTimeSeconds"));

		const double NetTime = ActorNetworkTime(GameState);

		if (TimeDoubleProp)
		{
			*TimeDoubleProp->ContainerPtrToValuePtr<double>(GameState) = NetTime;
		}
		if (TimeFloatProp)
		{
			*TimeFloatProp->ContainerPtrToValuePtr<float>(GameState) = static_cast<float>(NetTime);
		}

		// Mirror MatchState from GameState onto the local GameMode on non-master clients.
		// AGameState::OnRep_MatchState has already been dispatched by Fusion's rep-notify path
		// when the synced value changed, so client-side handlers have run. We only patch the
		// GameMode field directly to keep GameMode->GetMatchState() consistent for any code
		// that reads it -- going through AGameMode::SetMatchState would re-fire server-only
		// handlers (HandleMatchHasStarted -> RestartPlayer / SwapPlayerControllers / etc.).
		if (!Client->IsMasterClient())
		{
			if (AGameState* State = Cast<AGameState>(WorldPtr->GetGameState()))
			{
				if (AGameMode* GameMode = Cast<AGameMode>(WorldPtr->GetAuthGameMode()))
				{
					static const FNameProperty* MatchStateProp = FindFProperty<FNameProperty>(
						AGameMode::StaticClass(), TEXT("MatchState"));
					if (MatchStateProp)
					{
						FName& Field = *MatchStateProp->ContainerPtrToValuePtr<FName>(GameMode);
						const FName NewState = State->GetMatchState();
						if (Field != NewState)
						{
							Field = NewState;
						}
					}
				}
			}
		}
	}
}

void UFusionClient::AddDependencyCheck(const FusionCore::ObjectId Id, const FCopyContext& Root, const TFunction<bool()>& Callback)
{
	if (DependencyChecks.Contains(Id))
	{
		bool AddRoot = true;
		for (FDeferredDependency& Dependency : DependencyChecks[Id])
		{
			if (Dependency.Pair.Object->Id == Root.Pair.Object->Id)
			{
				AddRoot = false;
				break;
			}
		}

		if (AddRoot)
		{
			DependencyChecks[Id].Add({Root.Pair});
		}
	}
	else
	{
		TArray<FDeferredDependency> Dependencies;
		Dependencies.Add({Root.Pair});

		DependencyChecks.Add(Id, Dependencies);
	}

	FUSION_LOG("Adding Dependency for Id: %d:%d", Id.Origin, Id.Counter);
}

void UFusionClient::AddActorSource(UFusionActorComponent* Source)
{
	if (!Source)
	{
		return;
	}

	AActor* Owner = Source->GetOwner();
	if (Owner->GetIsReplicated() == false)
	{
		return;
	}

	if ((Owner->HasAnyFlags(RF_Transient) || !Owner->bNetStartup) && FusionNetDriver)
	{
		//Remote spawned actors are fully initialized in OnObjectFinalize, dont need to run attach here.
		if (RemoteSpawnedActors.Contains(Owner))
		{
			FUSION_LOG_WARN("Actor Spawned: %s BLOCKED (instance)", *Owner->GetName());
			return;
		}
		
		//Since this can be called from anywhere in the codebase whenever actor is spawned we have to prevent the object from sending state updates before we have atleast run tick once.
		AttachSpawnedActor(Source, CurrentMapInstance.Sequence, Source->bAutomaticallySendUpdates);
	}
	else
	{
		FPendingObject Pending;
		Pending.Object = Source->GetOwner();
		Pending.Source = Source;
		
		PendingObjects.Add(Pending);

		FUSION_LOG_WARN("Added Actor: %s To Pending", *Source->GetOwner()->GetName());
	}
}

FusionCore::ObjectId UFusionClient::FindObjectId(const UObject* Object)
{
	if (Object)
	{
		if (const uint64* Result = ObjectToObjectId.Find(Object); Result)
		{
			return FusionCore::ObjectId(*Result);
		}

		if (const uint64* Result = TempObjectToObjectId.Find(Object); Result)
		{
			return FusionCore::ObjectId(*Result);
		}
	}

	return FusionCore::ObjectId();
}

UObject* UFusionClient::FindObject(const FusionCore::ObjectId Id)
{
	if (const FFusionObjectActorPair* Result = ObjectIdToPair.Find(Id))
	{
		return Result->EngineObject;
	}

	if (const FFusionObjectActorPair* Result = TempObjectIdToPair.Find(Id))
	{
		return Result->EngineObject;
	}

	return nullptr;
}

FFusionObjectActorPair UFusionClient::FindObjectPair(FusionCore::ObjectId Id)
{
	if (const FFusionObjectActorPair* Result = ObjectIdToPair.Find(Id))
	{
		return *Result;
	}

	return FFusionObjectActorPair();
}

FusionCore::Object* UFusionClient::FindObject(const UObject* Object)
{
	if (Client)
	{
		if (const FusionCore::ObjectId Id = FindObjectId(Object); Id.IsSome())
		{
			return Client->FindObject(Id);
		}
	}

	return nullptr;
}

FusionCore::ObjectRoot* UFusionClient::FindObjectRoot(const UObject* Actor)
{
	if (Client)
	{
		if (const FusionCore::ObjectId Id = FindObjectId(Actor); Id.IsSome())
		{
			return Client->FindObjectRoot(Id);
		}
	}

	return nullptr;
}


bool IsNotify(const FProperty* Prop)
{
	return Prop && (Prop->GetPropertyFlags() & CPF_RepNotify) == CPF_RepNotify;
}

bool IsValidReplicationType(const UStruct* Type)
{
	return Type && (
		Type->IsChildOf(AActor::StaticClass())
		||
		Type->IsChildOf(UActorComponent::StaticClass())
	);
}

void UFusionClient::OnForcedDisconnect(FString Message)
{
	FUSION_LOG_ERROR("Forced Disconnect: %s", *Message);

	UWorld* PreviousWorld = CurrentWorld.Get();

	Shutdown();

	if (PreviousWorld)
	{
		const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(PreviousWorld);
		UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
		
		OnlineSubsystem->SetFusionClient(nullptr);
	}
}

void UFusionClient::OnObjectReady(FusionCore::ObjectRoot* Obj)
{
	NewRemoteObjectRoots.Add(Obj);

	for (const auto& id : Obj->RequiredObjects())
	{
		if (auto* child = FusionCore::ObjectChild::Cast(Client->FindObject(id)))
		{
			NewRemoteObjectChildren.Add(child);
		}
	}
}

void UFusionClient::OnSubObjectCreated(FusionCore::ObjectChild* Obj)
{
	NewRemoteObjectChildren.Add(Obj);
}

void UFusionClient::CopyToBackBuffer(FFusionObjectActorPair& Pair)
{
	FusionCore::TypeRef TypeRef = Pair.Object->Type;
	const TStrongObjectPtr<UFusionTypeDescriptor> Desc = Lookup->HashToDescriptor.FindRef(TypeRef.Hash);

	if (!Desc || !Desc->Type) {
		//FUSION_LOG("Failed to find class of type %llu", TypeRef.Hash);
		return;
	}
	
	if (Pair.EngineObject)
	{
		FCopyContext Context
		{
			Pair,
			this
		};

		for (int Index = 0; Index < Desc->Properties.Num(); ++Index)
		{
			const Property* Prop = Desc->Properties[Index];
			const int32 Offset = Prop->WordOffset;
			check(Offset < Pair.Object->Words.Length);
			Prop->CopyTo(this, Context, Pair.PropertyStates[Index], Pair.EngineObject, Pair.Object->Shadow.Ptr + Prop->WordOffset);
		}
	}
}

bool UFusionClient::OnObjectCreatedFinalize(FusionCore::Object* Obj)
{
	if (!CurrentWorld.IsValid())
	{
		FUSION_LOG_ERROR("OnObjectCreatedFinalize: No world set!");
		return false;
	}

	if (!Obj)
	{
		FUSION_LOG_ERROR("OnObjectCreatedFinalize: Object is nullptr");
		return false;
	}
	
	if (Obj->EngineBlob.Length > 0)
	{
		TArray<UClass*> LoadedClasses;
		TArray<FString> LoadedNames;

		FString TypesJson(Obj->EngineBlob.Length, reinterpret_cast<char*>(Obj->EngineBlob.Ptr));
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TypesJson);
		if (TSharedPtr<FJsonObject> RootObject; FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
		{
			if (const TArray<TSharedPtr<FJsonValue>>* TypesArray; RootObject->TryGetArrayField(TEXT("Types"), TypesArray))
			{
				for (const TSharedPtr<FJsonValue>& Value : *TypesArray)
				{
					TSharedPtr<FJsonObject> ClassObject = Value->AsObject();
					FString ClassPath = ClassObject->GetStringField(TEXT("C"));
					FString Name = ClassObject->GetStringField(TEXT("N"));

					if (UClass* FoundClass = LoadObject<UClass>(nullptr, *ClassPath))
					{
						FPropertyBuildOptions BuildOptions = UFusionTypeLookup::GetDefaultBuildOptions();
						Lookup->CreateTypeDescriptor(FoundClass, BuildOptions);

						LoadedClasses.Add(FoundClass);
						LoadedNames.Add(Name);
					}
				}
			}
		}
		
		if (LoadedClasses.Num() == 0)
			return false;
		
		UClass* StartClass = LoadedClasses[0];

		if (!StartClass)
			return false;

		if (StartClass->IsChildOf(AActor::StaticClass()))
		{
			FActorSpawnParameters SpawnInfo{};
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnInfo.bAllowDuringConstructionScript = true;
			SpawnInfo.bNoFail = true;
			SpawnInfo.CustomPreSpawnInitalization = [this](AActor* SpawnedActor)
			{
				RemoteSpawnedActors.Add(SpawnedActor);
			};

			if (StartClass->IsChildOf(AController::StaticClass()))
			{
				//If a controller spawns it will most likely also spawn an implicit player state/game state. We don't want this.
				AddSpawnBlockedCls(APlayerState::StaticClass());
			}

			AActor* Actor = CurrentWorld->SpawnActor<AActor>(StartClass, SpawnInfo);

			FUSION_LOG("Spawned Remote Object Actor: %s  With Id: %s", *Actor->GetName(), *ObjectIdToString(Obj->Id));

			//Ensure any dynamic/runtime components are added to actor.
			for (int i = 0; i < LoadedClasses.Num(); i++)
			{
				if (UClass* SubObjectClass = LoadedClasses[i]; SubObjectClass && SubObjectClass->IsChildOf(UActorComponent::StaticClass()))
				{
					if (const UActorComponent* Component = Actor->GetComponentByClass(SubObjectClass); !Component)
					{
						FUSION_LOG("Actor: %s missing component: %s, add dynamically", *Actor->GetName(), *SubObjectClass->GetName());
						
						FName SubObjectName = FName(LoadedNames[i]);
						UActorComponent* const NewComponent = NewObject<UActorComponent>(Actor, SubObjectClass, SubObjectName);
						NewComponent->RegisterComponent();
					}
				}
			}
			
			TArray<FTypeData> TypesData{};
			UFusionActorComponent* ActorSource = Actor->GetComponentByClass<UFusionActorComponent>();
			
			if (ActorSource) {
				ActorSource->GetTypes(this, Actor->GetComponents(), Actor->GetRootComponent(), TypesData);
			}
			else {
				ActorSource = NewObject<UFusionActorComponent>(Actor);
				ActorSource->RegisterComponent();

				ActorSource->GetTypes(this, Actor->GetComponents(), Actor->GetRootComponent(), TypesData);
			}
		
			RemoteSpawnedActors.Remove(Actor);

			if (StartClass->IsChildOf(AController::StaticClass()))
			{
				//If a controller spawns it will most likely also spawn an implicit player state/game state. We don't want this.
				RemoveSpawnBlockedCls(APlayerState::StaticClass());
			}

			FusionCore::TypeRef BaseTypeRef = TypesData[0].TypeRef;
			FFusionObjectActorPair Pair = RegisterObject(ActorSource, Actor, Actor, Obj, EFusionObjectPairType::Actor);

			//CopyLocalStateToObject(FFusionObjectActorPair{Actor, Actor, Actor->GetName(), Obj});
			//Obj->CopyPluginReceivedWordsToLocalWordBuffer();

			uint64 WordCount = 0;
			for (FTypeData Item : TypesData)
				WordCount += Item.TypeRef.WordCount;
			
			UFusionActorComponent* FusionComponent = Actor->GetComponentByClass<UFusionActorComponent>();

			Actor->SetRole(ROLE_SimulatedProxy);

			FCopyContext Context
			{
				Pair,
				this,
				FusionComponent ? FusionComponent->PackageSettings() : FPackagedSettings{},
			};
			
			//1. Copy initial network state into object.
			CopyRemoteStateToObject(Context, Pair, true);

			//2. Set initial shadow buffer value based on spawned in actor. This ensures we will only start diffing based on the initial state, otherwise there is a risk we will send all data when we start locally modifying a remote object.
			CopyToBackBuffer(Pair);

			FusionComponent->SubscribeEvents(Client, Obj->Id);
			FusionComponent->OnObjectReady.Broadcast();
			FUSION_LOG("Engine Object Created %s (obj-map:%i, client-map:%i ActorName: %s, Words: %llu)", *ObjectIdToString(Obj->Id), Client->GetRoot(Obj)->GetMap(), CurrentMapInstance.Sequence, *Actor->GetName(), WordCount);
		}
		else if (StartClass->IsChildOf(UActorComponent::StaticClass()))
		{
			if (FFusionObjectActorPair ParentPair = FindObjectPair(FusionCore::ObjectChild::GetParent(Obj)); ParentPair.Actor)
			{
				FUSION_LOG("Getting Parent Object: %s", *ParentPair.Actor->GetName());
				
				if (TStrongObjectPtr<UFusionTypeDescriptor> SubType = Lookup->HashToDescriptor.FindRef(Obj->Type.Hash))
				{
					FusionCore::ObjectChild* ObjChild = FusionCore::ObjectChild::Cast(Obj);

					//Ensure we fetch the correct component, since multiple of the same type can exist on the actor.
					UActorComponent* FoundComponent{nullptr};
					TSet<UActorComponent*> Components = ParentPair.Actor->GetComponents();
					for (UActorComponent* Component : Components)
					{
						FString SubObjectName = Component->GetName();
						const uint32 SubObjectHash = UFusionHelpers::SafeObjectNameHash(TCHAR_TO_ANSI(*SubObjectName), SubObjectName.Len());

						if (SubObjectHash == ObjChild->EngineHash) {
							FoundComponent = Component;
							break;
						}
					}

					if (!FoundComponent)
					{
						FName SubObjectName = FName(LoadedNames[0]);
						FoundComponent = NewObject<UActorComponent>(ParentPair.Actor, StartClass, SubObjectName);
						FoundComponent->RegisterComponent();
					}
					
					if (FoundComponent)
					{
						FFusionObjectActorPair Pair = RegisterObject(nullptr, ParentPair.Actor, FoundComponent, Obj, EFusionObjectPairType::Component);
						
						UFusionActorComponent* ActorSettings = ParentPair.Actor->GetComponentByClass<UFusionActorComponent>();

						FCopyContext Context
						{
							Pair,
							this,
							ActorSettings ? ActorSettings->PackageSettings() : FPackagedSettings{},
						};
						
						//1. Copy initial network state into object.
						CopyRemoteStateToObject(Context, Pair, true);

						//2. Set initial shadow buffer value based on spawned in actor. This ensures we will only start diffing based on the initial state, otherwise there is a risk we will send all data when we start locally modifying a remote object.
						CopyToBackBuffer(Pair);

						if (AActor* Actor = FoundComponent->GetOwner()) {
							UFusionPhysicsReplicationComponent* FusionPhysics = Cast<
								UFusionPhysicsReplicationComponent>(FoundComponent);

							// If using Forecast physics, then extrapolate the values so it spawns in a suitable position
							if (FusionPhysics) {
								if (UPrimitiveComponent* PrimComponent = Actor->GetComponentByClass<
									UPrimitiveComponent>(); PrimComponent && PrimComponent->IsSimulatingPhysics()) {
									FBodyInstance* BodyInstance = PrimComponent->GetBodyInstance(NAME_None);

									FRigidBodyState TargetState = FusionPhysicsUtils::GetRigidBodyState(FusionPhysics->BodyState);

									UFusionActorComponent* Settings = Actor->GetComponentByClass<UFusionActorComponent>();

									FRigidBodyState ResultState;

									FusionPhysicsReplication::ComputeExtrapolatedSnapshot(CurrentWorld.Get(), Settings, FusionPhysics, BodyInstance, TargetState, ResultState);

									FusionPhysicsReplication::PerformImmediateMove(ResultState, BodyInstance);
								}
							}
						}
					}
					else
					{
						FUSION_LOG("Unable to get component: %s on Actor: %s", *StartClass->GetName(), *ParentPair.Actor->GetName());
					}
				}
			}
		}
		else
		{
			FusionCore::ObjectId ParentId = FusionCore::ObjectId(FusionCore::ObjectChild::GetParent(Obj));

			if (UObject* ParentObject = FindObject(ParentId)) {
				FUSION_LOG("Getting Parent Object: %s", *ParentObject->GetName());
				
				if (UObject* CreatedObject = NewObject<UObject>(ParentObject, StartClass))
				{
					FUSION_LOG("Created New Custom Object: %s", *CreatedObject->GetName());
					
					FFusionObjectActorPair Pair = RegisterObject(nullptr, Cast<AActor>(ParentObject), CreatedObject, Obj, EFusionObjectPairType::CustomObject);

					FCopyContext Context
					{
						Pair,
						this,
						FPackagedSettings{},
					};
					
					//1. Copy initial network state into object.
					CopyRemoteStateToObject(Context, Pair, true);

					//2. Set initial shadow buffer value based on spawned in actor. This ensures we will only start diffing based on the initial state, otherwise there is a risk we will send all data when we start locally modifying a remote object.
					CopyToBackBuffer(Pair);
				}
			}
		}
	}

	return true;
}

UObject* UFusionClient::RemoveObjectPairs(const FusionCore::ObjectId Id)
{
	UObject* Object = FindObject(Id);

	if (Object)
	{
		ObjectToObjectId.Remove(Object);
		TempObjectToObjectId.Remove(Object);
	}

	// clear shared mode object references no matter what
	ObjectIdToPair.Remove(Id);
	TempObjectIdToPair.Remove(Id);

	if (FusionCore::ObjectRoot* Root = FusionCore::ObjectRoot::Cast(Client->FindObject(Id)))
	{
		NewRemoteObjectRoots.Remove(Root);
	}

	return Object;
}

UObject* UFusionClient::RemoveObjectRoot(const FusionCore::ObjectRoot* Root)
{
	if (!Root)
		return nullptr;

	NewRemoteObjectRoots.Remove(const_cast<FusionCore::ObjectRoot*>(Root));
	
	return RemoveObjectPairs(Root->Id);
}

void UFusionClient::OnSubObjectDestroyed(FusionCore::ObjectChild* Obj, const FusionCore::DestroyModes Mode)
{
	NewRemoteObjectChildren.Remove(Obj);
	if (UObject* Object = RemoveObjectPairs(Obj->Id))
	{
		if (Mode == FusionCore::DestroyModes::Remote || Mode == FusionCore::DestroyModes::Shutdown || Mode == FusionCore::DestroyModes::MapChange)
		{
			if (UActorComponent* Component = Cast<UActorComponent>(Object))
			{
				Component->DestroyComponent();
			}
		}
	}
}

void UFusionClient::OnObjectDestroyed(const FusionCore::ObjectRoot* Obj, const FusionCore::DestroyModes Mode)
{
	if (Obj && (Obj->EngineFlags & static_cast<uint32_t>(EObjectSpecialFlags::SceneObject)) != 0)
	{
		OnMapActorDestroyedRemote(Obj->GetMap(), Obj->Id, Mode);
	}
	else
	{
		if (Mode == FusionCore::DestroyModes::Remote || Mode == FusionCore::DestroyModes::Shutdown || Mode == FusionCore::DestroyModes::MapChange)
		{
			FFusionObjectActorPair Pair = FindObjectPair(Obj->Id);
			if (Pair.Actor)
			{
				//will hit OnEngineObjectDestroyed, ensure we ignore that path when remote destroying.
				RemoteDestroyedObjects.Add(Pair.Actor);
				
				Pair.Settings.Get()->OnObjectDestroyed.Broadcast(ToUnrealDestroyMode(Mode));
				Pair.Actor->Destroy(true);
			}
			RemoveObjectRoot(Obj);
		}
	}
}

void UFusionClient::OnMapActorDestroyedRemote(uint32 SceneSequence, const FusionCore::ObjectId Id, const FusionCore::DestroyModes Mode)
{
	FFusionObjectActorPair Pair = FindObjectPair(Id);
	
	if (Pair.IsValid())
	{
		if (Pair.Actor)
		{
			//will hit OnEngineObjectDestroyed, ensure we ignore that path when remote destroying.
			RemoteDestroyedObjects.Add(Pair.Actor);
		
			Pair.Settings.Get()->OnObjectDestroyed.Broadcast(ToUnrealDestroyMode(Mode));
			Pair.Actor->Destroy(true);
		}
	}
	else 
	{
		//We received destroyed map actor while being in room but not yet having started fusion session. 
		if (DestroyedMapActors.Contains(SceneSequence))
		{
			DestroyedMapActors[SceneSequence].Add(FKeyObjectId(Id));
		}
		else
		{
			DestroyedMapActors.Add(SceneSequence, {FKeyObjectId(Id)});
		}
	}

	RemoveObjectPairs(Id);
}

void UFusionClient::OnFusionStart()
{
	//This object will live as long as the room exists. Not tied to any map.
	FPendingObject Pending;
	Pending.Object = CurrentWorld.IsValid() ? UGameplayStatics::GetGameInstance(CurrentWorld.Get()) : nullptr;
	if (Pending.Object)
	{
		PendingObjects.Add(Pending);
	}
	else
	{
		FUSION_LOG_ERROR("Invalid world or GameInstance on room join");
	}
}

void UFusionClient::OnMapChange(const std::unordered_map<FusionCore::Map, FusionCore::Data> &Maps, bool Initial)
{
	FUSION_LOG("Fusion OnMapChange Initial=%d Maps.size=%d ClientType=%s",
		Initial ? 1 : 0,
		(int)Maps.size(),
		*UEnum::GetValueAsString(ClientInstanceType));

	if (Client->IsMasterClient())
	{
		//This will be called from Client->Start on initial mapload, will be in same callstack.
		if (Initial)
		{
			auto Pair = Maps.find(1);
			if (Pair != Maps.end())
			{
				const FString PayloadString = FString::ConstructFromPtrSize(reinterpret_cast<char*>(Pair->second.Ptr), Pair->second.Length);
				TSharedPtr<FJsonObject> JsonObject = UFusionHelpers::DeserializeMapPayload(PayloadString);
				
				if (JsonObject.IsValid())
				{
					FString InitialWorldName = JsonObject->GetStringField(TEXT("MapName"));
					ChangeWorld(InitialWorldName);
				}
				else
				{
					FUSION_LOG_ERROR("UFusionClient::OnMapChange Received invalid payload string: %s", *PayloadString);
					return;
				}
			}

		}
		return;
	}
	
	for (const auto &[MapSequence, Data] : Maps)
	{
		if (MapSequence == 0 || Data.Length == 0)
		{
			continue;
		}

		const FString PayloadString = FString::ConstructFromPtrSize(reinterpret_cast<char*>(Data.Ptr), Data.Length);
		TSharedPtr<FJsonObject> JsonObject = UFusionHelpers::DeserializeMapPayload(PayloadString);

		FString LevelNameCopy;
		bool bAttachCurrent;
		if (JsonObject.IsValid())
		{
			LevelNameCopy = JsonObject->GetStringField(TEXT("MapName"));
			bAttachCurrent = JsonObject->GetBoolField(TEXT("Attached"));
		}
		else
		{
			FUSION_LOG_ERROR("UFusionClient::OnSceneChange Received invalid payload string: %s", *PayloadString);
			return;
		}
		
		if (bAttachCurrent)
		{
			FString TargetMapName = FPackageName::GetShortName(LevelNameCopy);
			
			FString WorldName = CurrentWorld->GetName();
			FString CleanedName = UWorld::RemovePIEPrefix(WorldName);

			//Ensure we can only attach if the requested map is the current active one.
			if (CleanedName == TargetMapName)
			{
				//Attach map will not force any load, its local clients job to be on the correct world.
				CurrentMapInstance.Name = LevelNameCopy;
				CurrentMapInstance.Sequence = MapSequence;
				CurrentMapInstance.bAttachCurrent = true;

				TargetMapInstance.Name = TargetMapName;

				//Assume masterclient can directly connect all things in the active world.
				AttachCurrentMap_Internal(CurrentWorld.Get());

				//This will not work with additive map loads, need to fix later.
				return;
			}
		}

		FMapInstance RequestedMapInstance;
		RequestedMapInstance.Name = LevelNameCopy;
		RequestedMapInstance.Sequence = MapSequence;
		RequestedMapInstance.bAttachCurrent = false;

		//Will later be consumed in TriggerMapLoad called from Tick.
		RequestedMapInstances.HeapPush(RequestedMapInstance);
		
		//Since potential destroy calls for objects can start happening after this has fired we need to ensure we are no longer on LevelActive trying to iterate things.
		SetMapState(EMapState::HasRequestToChangeLevel);
		
		FUSION_LOG("Wanted Map Name: %s, total number of maps to load: %i", *LevelNameCopy, RequestedMapInstances.Num());
	}
}

auto UFusionClient::OnRpcReceived(const FusionCore::Rpc& Rpc) -> void
{
	if (Rpc.TargetObject.IsSome())
	{
		FFusionObjectActorPair Pair = FindObjectPair(Rpc.TargetObject);
		if (Pair.IsValid())
		{
			if (const auto Desc = Lookup->FindClassDescriptor(Pair.EngineObject.GetClass()))
			{
				if (const FString* EventNamePtr = Desc->EventHashToName.Find(Rpc.EventHash))
				{
					const FString EventName = *EventNamePtr;
					UFusionFunctionDescriptor* FunctionDescriptor = *Desc->EventFunctions.Find(EventName);
	
					void* Params = FMemory_Alloca(FunctionDescriptor->ParametersSize);
					FunctionDescriptor->DeserializeParams(this, Rpc, Params);

					if (FusionNetDriver)
					{
						if (FunctionDescriptor->Function->FunctionFlags & FUNC_NetServer)
						{
							FusionNetDriver->SetIsServer(true);
						}

						if (FusionNetDriver->IsServer() && PlayerController)
						{
							PlayerController->NetConnection = nullptr;
						}
					}

					Pair.EngineObject->ProcessEvent(FunctionDescriptor->Function, Params);

					if (PlayerController && FusionNetDriver)
					{
						PlayerController->NetConnection = FusionNetDriver->ServerConnection;
					}

					if (FusionNetDriver)
					{
						FusionNetDriver->SetIsServer(false);
					}
				}
				else
				{
					FUSION_LOG_WARN("OnRpcReceived: RPC EventHash: %llu not found, ensure source type has correct event output", Rpc.EventHash);
				}
			}
			else
			{
				FUSION_LOG_WARN("OnRpcReceived: Unable to find or create descriptor for type: %s", *Pair.EngineObject.GetClass()->GetName());
			}
		}
		else
		{
			FUSION_LOG_WARN("OnRpcReceived: target object: %s not found", *ObjectIdToString(Rpc.TargetObject));
		}
	}
	else
	{
		FUSION_LOG_WARN("OnRpcReceived: RPC TargetObject not set");
	}
}

bool UFusionClient::IsLoadingMap()
{
	return CurrentMapState == EMapState::IsLoading;
}

void UFusionClient::OnMapInit(UWorld* World)
{
	CurrentWorld = World;
	
	ApplyFusionStreamingSkipFlags(World);
	
	OnActorSpawnedHandle = CurrentWorld->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UFusionClient::OnActorSpawned));
	OnActorDestroyedHandle = CurrentWorld->AddOnActorDestroyedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UFusionClient::OnEngineObjectDestroyed));

	FUSION_LOG("UFusionClient::OnMapInit(%s)  LoadedWorldIndex: %d  Name: %s   ClientId: %s", *UEnum::GetValueAsString(ClientInstanceType), World->GetUniqueID(), *World->GetName(), *ClientInstanceId.ToString());
}

void UFusionClient::OnMapDestroy(UWorld* World)
{
	if (World != CurrentWorld)
	{
		return;
	}

	if (OnActorSpawnedHandle.IsValid())
	{
		CurrentWorld->RemoveOnActorSpawnedHandler(OnActorSpawnedHandle);
		OnActorSpawnedHandle.Reset();
	}
	
	if (OnActorDestroyedHandle.IsValid())
	{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
		CurrentWorld->RemoveOnActorDestroyededHandler(OnActorDestroyedHandle);
#else
		CurrentWorld->RemoveOnActorDestroyedHandler(OnActorDestroyedHandle);
#endif
		OnActorDestroyedHandle.Reset();
	}
	
	DependencyChecks.Empty();
	MapActors.Empty();
	
	for (auto It = PendingObjects.CreateIterator(); It; ++It)
	{
		FPendingObject& Pending = *It;

		if (Pending.Object)
		{
			if (Pending.Object->IsA(UGameInstance::StaticClass()))
			{
				continue;
			}
			if (Pending.Object->IsA(APlayerState::StaticClass()))
			{
				continue;
			}
		}
		
		It.RemoveCurrent();
	}

	TArray<AActor*> RemoteActors;
	for (auto It = ObjectIdToPair.CreateIterator(); It; ++It)
	{
		const FFusionObjectActorPair& Pair = It->Value;
		
		if (!Client->FindObject(Pair.ObjectId))
		{
			ObjectToObjectId.Remove(Pair.EngineObject);
			It.RemoveCurrent();
			continue;
		}
		
		if (!IsValid(Pair.EngineObject) || !Pair.EngineObject)
		{
			ObjectToObjectId.Remove(Pair.EngineObject);
			It.RemoveCurrent();
			continue;
		}

		FusionCore::ObjectRoot* Root = Pair.Object->Root();

		if (!Root)
		{
			ObjectToObjectId.Remove(Pair.EngineObject);
			It.RemoveCurrent();
			continue;
		}

		FFusionObjectActorPair RootPair = FindObjectPair(Root->Id);
		if (RootPair.Actor && (Pair.Object->EngineFlags & static_cast<uint32_t>(EObjectSpecialFlags::SceneObject)) == 0)
		{
			//Do no destroy proxies. Should be handled in OnObjectDestroy
			if (RootPair.Actor->GetLocalRole() == ROLE_SimulatedProxy)
			{
				continue;
			}
		}

		if (Root && Root->GetMap() == 0)
		{
			//These object have custom lifecycles, dont remove when map changes.
			continue;
		}
		
		if (Root && Root->GetMap() < CurrentMapInstance.Sequence)
		{
			ObjectToObjectId.Remove(Pair.EngineObject);
			It.RemoveCurrent();
		}
	}

	for (auto It = TempObjectIdToPair.CreateIterator(); It; ++It)
	{
		const FFusionObjectActorPair& Pair = It->Value;
		
		if (!Client->FindObject(Pair.ObjectId))
		{
			TempObjectToObjectId.Remove(Pair.EngineObject);
			It.RemoveCurrent();
			continue;
		}
		
		if (!IsValid(Pair.EngineObject) || !Pair.EngineObject)
		{
			TempObjectToObjectId.Remove(Pair.EngineObject);
			It.RemoveCurrent();
			continue;
		}
		
		FusionCore::ObjectRoot* Root = Pair.Object->Root();
		if (Root && Root->GetMap() == 0)
		{
			continue;
		}
		
		if (Root && Root->GetMap() < CurrentMapInstance.Sequence)
		{
			TempObjectToObjectId.Remove(Pair.EngineObject);
			It.RemoveCurrent();
		}
	}
	
	PlayerController = nullptr;
	BlockedClasses.Empty();
	CurrentWorld = nullptr;

	if (FusionNetDriver)
	{
		FusionNetDriver->Cleanup();
	}
	FusionNetDriver = nullptr; //Import we release reference so GC can destroy instance when changing map.
}

void UFusionClient::PreMapLoad(const FWorldContext& WorldContext, const FString& WorldName, bool bIsSeamlessTravel /*=false*/)
{
	bDoingSeamlessTravel = false;
	if (WorldContext.SeamlessTravelHandler.IsInTransition() || bIsSeamlessTravel)
	{
		//Preload determines if a preload -> mapinit -> postload need to check for target map.
		//This is only relevant for seamless travel where the map callbacks can give us the transition map back and we dont want to network that.
		bDoingSeamlessTravel = true;

		//Target maps will not be set when not using fusion API.
		//TODO: Implement feature to client can call seamless travel without relying on the implicit map transition.
		if (TargetMapInstance.Name.IsEmpty())
		{
			FUSION_LOG_ERROR("When using seamless travel, Please use 'UFusionOnlineSubsystem::ChangeWorld'. Attempted to load Map: '%s'", *UEnum::GetValueAsName(CurrentMapState).ToString(), *WorldName);
			return;
		}
	}
	else
	{
		//Manually destroy PlayerState belonging to current player
	}
	
	if (Client->IsMasterClient() && bDoingSeamlessTravel)
	{
		bool MasterClientProcessingChange = CurrentMapState == EMapState::MasterClientChangeWorld;

		if (!MasterClientProcessingChange)
		{
			FUSION_LOG_ERROR("Invalid State: '%s' when using seamless travel, Please use 'UFusionOnlineSubsystem::ChangeWorld'. Attempted to load Map: '%s'", *UEnum::GetValueAsName(CurrentMapState).ToString(), *WorldName);
			return;
		}
	}

	if (CurrentMapState == EMapState::IsLoading)
	{
		FUSION_LOG_ERROR("Invalid state, map already loading '%s' Map: '%s'", *UEnum::GetValueAsName(CurrentMapState).ToString(), *WorldName);
		return;
	}
	
	// Set state to now loading.
	// True for master and non-master.
	SetMapState(EMapState::IsLoading);

	TriggerLevelChanged(WorldName);
	
	SendSocketToBackgroundThread();
}

void UFusionClient::PostMapLoad(UWorld* LoadedWorld)
{
	FUSION_LOG("Map has finished loading. Maps still to load: %i", RequestedMapInstances.Num());

	ApplyFusionStreamingSkipFlags(LoadedWorld);

	// ReSharper disable once CppDeclaratorNeverUsed
	FusionPhysicsReplication* PhysicsReplication = FusionPhysicsUtils::CreateReplicationSetup(LoadedWorld);
	check(PhysicsReplication);
	
	if (CurrentMapState != EMapState::IsLoading)
	{
		FUSION_LOG_ERROR("Invalid state after loading a map. State: '%s'", *UEnum::GetValueAsName(CurrentMapState).ToString());
	}

	SetMapState(EMapState::ReadyToNotifyAboutLevelLoad);

	for (TActorIterator<AActor> It(LoadedWorld); It; ++It)
	{
		TObjectPtr<AActor> MapActor = *It;

		if (MapActor->HasAnyFlags(RF_Transient) || !MapActor->bNetStartup)
		{
			continue;
		}

		if (MapActor && MapActor->IsFullNameStableForNetworking() && !MapActor->GetComponentByClass<UFusionActorComponent>())
		{
			const unsigned int Hash = UFusionHelpers::SafeObjectNameHash(MapActor);
			MapActors.Add(Hash, MapActor);
		}
	}
	
	RetrieveSocketFromBackgroundThread();

	SetupNetDriver(LoadedWorld);
}

void UFusionClient::OnLevelAdded(ULevel* Level, UWorld* World)
{
	ApplyFusionStreamingSkipFlags(World);
}

void UFusionClient::OnLevelRemoved(ULevel* Level, UWorld* World)
{
	//No-op for now.
}

void UFusionClient::SetupNetDriver(UWorld* World)
{
	CreateNetDriver(World);
	
	if (FusionNetDriver != nullptr)
	{
		FusionNetDriver->SetWorld(World);
		CurrentWorld->SetNetDriver(FusionNetDriver);
	
		FusionNetDriver->InitConnectionClass();
	
		FString Error = TEXT("FusionNetDriver error");
		FusionNetDriver->InitConnect(nullptr, TEXT(""), Error);

		if (FLevelCollection* Collection = const_cast<FLevelCollection*>(CurrentWorld->GetActiveLevelCollection()); Collection != nullptr)
		{
			Collection->SetNetDriver(FusionNetDriver);
		}
		else
		{
			FUSION_LOG_ERROR("No LevelCollection found for created world");
		}
	}
}

void UFusionClient::TravelInternal(const FString& WorldName)
{
	TargetMapInstance.Name = FPackageName::GetShortName(WorldName);
	bDoingSeamlessTravel = true;
				
	CurrentWorld->SetNetDriver(nullptr); //Prevent seamless travel code from doing network checks.
	CurrentWorld->SeamlessTravel(WorldName, true);
}

UWorld* UFusionClient::GetCurrentWorld() const
{
	return CurrentWorld.Get();
}

void UFusionClient::SendSocketToBackgroundThread()
{
	if (bRunUnderOneProcess)
		return;
	
	Client->StateUpdatesPause();
	
	bSocketInBgThread = true;

	FusionCore::Client* Client2 = this->Client;

	std::atomic_bool* Mtr = &MainThreadReady;
	std::atomic_bool* Btd = &BackThreadDone;

	Mtr->store(false);
	Btd->store(false);

	AsyncThread([Mtr, Btd, Client2]
	{
		while (Mtr->load() == false)
		{
			Client2->UpdateServiceOnly();
		}

		Btd->store(true);
	}, 0, EThreadPriority::TPri_Highest);

	FUSION_LOG("Background Thread Has Socket");
}

void UFusionClient::RetrieveSocketFromBackgroundThread()
{
	if (bRunUnderOneProcess)
		return;
	
	if (bSocketInBgThread)
	{
		FUSION_LOG("Main Thread Has Socket Back !");
		MainThreadReady.store(true);

		while (BackThreadDone.load() == false)
		{
			// ...
		}

		bSocketInBgThread = false;

		// 
		Client->StateUpdatesResume();
	}
}

bool UFusionClient::ChangeWorld(const FString& WorldName)
{
	if (WorldChangeRequest.bIsActive)
	{
		FUSION_LOG_ERROR("World change request is already active!");
		return false;
	}

	if (!Client->IsMasterClient())
	{
		FUSION_LOG_ERROR("Only the master client is allowed to trigger a world change!");
		return false;
	}

	if (CurrentMapState == EMapState::IsLoading)
	{
		FUSION_LOG_ERROR("Another map is currently loading!");
		return false;
	}

	FUSION_LOG("UFusionClient::ChangeWorld: %s", *WorldName);

	WorldChangeRequest.bIsActive = true;
	WorldChangeRequest.WorldName = UWorld::RemovePIEPrefix(WorldName);

	SetMapState(EMapState::MasterClientChangeWorld);

	return true;
}

void UFusionClient::ClientTravel(const FString& LevelName)
{
	if (!CurrentWorld.IsValid())
	{
		FUSION_LOG_ERROR("UFusionClient::ClientTravel: No current world!");
		return;
	}

	if (Client->IsMasterClient())
	{
		FUSION_LOG_ERROR("UFusionClient::ClientTravel: Use ChangeWorld instead");
		return;
	}

	SetMapState(EMapState::IsLoading);
	TravelInternal(LevelName);
}

bool UFusionClient::IsTargetWorld(UWorld* World)
{
	if (bDoingSeamlessTravel)
	{
		if (TargetMapInstance.Name.IsEmpty())
			return false;

		FString WorldName = World->GetName();
		FString CleanedName = UWorld::RemovePIEPrefix(WorldName);

		//Fusion will ignore transition maps using this check.
		return CleanedName == TargetMapInstance.Name;
	}

	return true;
}

void UFusionClient::ToggleNetworkSend(UFusionActorComponent* FusionActorSettings, bool bToggle)
{
	AActor* Owner = FusionActorSettings->GetOwner();
	if (FusionCore::Object* Obj = FindObject(Owner))
	{
		Obj->SetSendUpdates(bToggle);
	}
}

void UFusionClient::Startup(UWorld* InitialWorld, UFusionTypeLookup* TypeLookup, const UFusionOnlineSubsystemSettings* Settings, TObjectPtr<UFusionRealtimeClient> FusionRealtimeClient)
{
	check (InitialWorld);
	check (TypeLookup);

	RealtimeClient = FusionRealtimeClient;
	
	auto& MatchmakingClient = *RealtimeClient->GetClient();
	auto Room = MatchmakingClient.GetCurrentRoom();
		
	Client = new FusionCore::Client(MatchmakingClient);
	CurrentWorld = InitialWorld;
	Lookup = TypeLookup;
	
	if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(InitialWorld))
	{
		ClientInstanceType = UFusionHelpers::WorldContextType(*WorldContext);
	}
	ClientInstanceId = FGuid::NewGuid();

	//This is done to support RunInOneProcess, otherwise client instances would share net driver. 
	DriverName = FString::Printf(TEXT("FusionNetDriver-%s"), *ClientInstanceId.ToString());

#if WITH_EDITOR
	if (GIsEditor)
	{
		const ULevelEditorPlaySettings* PlayInSettings = GetMutableDefault<ULevelEditorPlaySettings>();
		check(PlayInSettings);
		PlayInSettings->GetRunUnderOneProcess(bRunUnderOneProcess);
	}
#endif
	
	FUSION_LOG("Made Client Instance with Id: %s  Type: %s  InitialWorld: %s", *ClientInstanceId.ToString(), *UEnum::GetValueAsString(ClientInstanceType), *CurrentWorld->GetName());

	ClientSubscriptionBag += Client->OnForcedDisconnect.Subscribe([this](std::string message)
		{
			this->OnForcedDisconnect(FString(UTF8_TO_TCHAR(message.c_str())));
		});

	ClientSubscriptionBag += Client->OnObjectReady.Subscribe([this](FusionCore::ObjectRoot* Obj)
	{
		this->OnObjectReady(Obj);
	});

	ClientSubscriptionBag += Client->OnSubObjectCreated.Subscribe( [this](FusionCore::ObjectChild* Obj)
	{
		this->OnSubObjectCreated(Obj);
	});

	ClientSubscriptionBag += Client->OnSubObjectDestroyed.Subscribe( [this](FusionCore::ObjectChild* Obj, const FusionCore::DestroyModes Mode)
	{
		this->OnSubObjectDestroyed(Obj, Mode);
	});

	ClientSubscriptionBag += Client->OnObjectDestroyed.Subscribe( [this](const FusionCore::ObjectRoot* Obj, const FusionCore::DestroyModes Mode)
	{
		this->OnObjectDestroyed(Obj, Mode);
	});

	ClientSubscriptionBag += Client->OnMapChange.Subscribe( [this](const std::unordered_map<FusionCore::Map, FusionCore::Data>& Maps, bool Initial)
	{
		this->OnMapChange(Maps, Initial);
	});

	ClientSubscriptionBag += Client->OnRpc.Subscribe([this](const FusionCore::Rpc& RPC)
	{
		this->OnRpcReceived(RPC);
	});

	ClientSubscriptionBag += Client->OnDestroyedMapActor.Subscribe([this](const FusionCore::ObjectId Id)
	{
		this->OnMapActorDestroyedRemote(Id.Map, Id, FusionCore::DestroyModes::Remote);
	});

	ClientSubscriptionBag += Client->OnFusionStart.Subscribe([this]()
	{
		this->OnFusionStart();
	});

	Client->Start();
}

void UFusionClient::Shutdown()
{
	SetMapState(EMapState::Shutdown);

	RealtimeClient = nullptr;
	
	ClientSubscriptionBag.UnsubscribeAll();
	
	Client->Stop();

	PendingObjects.Empty();
	ObjectIdToPair.Empty();
	ObjectToObjectId.Empty();
	DependencyChecks.Empty();
	NewRemoteObjectRoots.Empty();
	NewRemoteObjectChildren.Empty();

	if (FusionNetDriver)
	{
		FusionNetDriver->Cleanup();
		GEngine->DestroyNamedNetDriver(CurrentWorld.Get(), FName(DriverName));
	}
	
	if (CurrentWorld.IsValid())
	{
		CurrentWorld->SetNetDriver(nullptr);

		FusionPhysicsUtils::ResetReplication(CurrentWorld.Get());
	}

	FusionNetDriver = nullptr;
	CurrentWorld = nullptr;
	Lookup = nullptr;
}
