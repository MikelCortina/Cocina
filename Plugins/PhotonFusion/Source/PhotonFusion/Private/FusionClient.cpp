// Copyright 2026 Exit Games GmbH. All Rights Reserved.
// ReSharper disable CppUnusedIncludeDirective

#include "FusionClient.h"
#include "FusionCVars.h"

#include <codecvt>

#include "Misc/AssertionMacros.h"
#include "EngineUtils.h"
#include "FusionActorComponent.h"
#include "FusionHelpers.h"
#include "Physics/FusionPhysicsReplicationComponent.h"
#include "FusionUtils.h"
#include "Physics/FusionPhysicsUtils.h"
#include "PhotonOnlineSubsystemSettings.h"
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
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Types/FusionNetworkedArrayBuilder.h"
#include "Engine/GameEngine.h"
#include "Engine/NetDriver.h"
#include "Types/TypeLookup.h"

#if WITH_EDITOR
#include "Settings/LevelEditorPlaySettings.h"
#endif

// ReSharper restore CppUnusedIncludeDirective


FKeyObjectId::operator uint64() const
{
	return static_cast<uint64>(Origin) | static_cast<uint64>(Counter) << 32;
}

static EFusionObjectDestroyMode ToUnrealDestroyMode(SharedMode::DestroyModes Mode)
{
	switch (Mode)
	{
	case SharedMode::DestroyModes::Local:            return EFusionObjectDestroyMode::Local;
	case SharedMode::DestroyModes::Remote:           return EFusionObjectDestroyMode::Remote;
	case SharedMode::DestroyModes::SceneChange:      return EFusionObjectDestroyMode::SceneChange;
	case SharedMode::DestroyModes::Shutdown:          return EFusionObjectDestroyMode::Shutdown;
	case SharedMode::DestroyModes::RejectedNotOwner: return EFusionObjectDestroyMode::RejectedNotOwner;
	case SharedMode::DestroyModes::ForceDestroy:     return EFusionObjectDestroyMode::ForceDestroy;
	default:                                         return EFusionObjectDestroyMode::Local;
	}
}

void UFusionClient::AttachCurrentMap(UWorld* World)
{
	if (Client->IsMasterClient())
	{
		TriggerLevelChanged(World->GetName(), true);
		
		//Assume masterclient can directly connect all things in the active world.
		AttachCurrentMap_Internal(World);
	}
	else
	{
		//Wait for current map state to change before we can attach none master clients.
		if (CurrentMapInstance.bAttachCurrent && World->GetName() == CurrentMapInstance.Name)
		{
			//Connecting clients have to wait for OnSceneChange to populate current world settings before connecting objects.
			AttachCurrentMap_Internal(World);
		}
	}
}

void UFusionClient::AttachCurrentMap_Internal(UWorld* World)
{
	if (CurrentWorld.IsValid())
	{
		TArray<SharedMode::ObjectId> PairsToRemove;
		for (auto Pair : ObjectIdToPair)
		{
			SharedMode::Object* Object = Client->FindObject(Pair.Key);

			if (!Object)
			{
				PairsToRemove.Add(Pair.Key);
				continue;
			}
			
			if (Object && (Object->SpecialFlags & SharedMode::ObjectSpecialFlags::ExistsOnClient) != SharedMode::ObjectSpecialFlags::None)
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
		
	OnMapDestroy(CurrentWorld.Get());

	//Destroy any remote proxy objects incase this is called multiple times.
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

				if (RootObject && (RootObject->SpecialFlags & SharedMode::ObjectSpecialFlags::ExistsOnClient) == SharedMode::ObjectSpecialFlags::None)
				{
					//Destroys any player owned objects that are not part of client init (map actors and GameInstance)
					Client->DestroyObjectLocal(RootObject, true);
				}
			}
		}
		
		UObject* GameInstance = UGameplayStatics::GetGameInstance(CurrentWorld.Get());
		AttachGlobalInstanceActor(GameInstance);

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
			if (RootObject && (RootObject->SpecialFlags & SharedMode::ObjectSpecialFlags::SceneObject) != SharedMode::ObjectSpecialFlags::None)
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

				for (auto SubObjectId : RootObject->SubObjects)
				{
					FObjectActorPair SubObjectPair = FindObjectPair(SubObjectId);
					if (SubObjectPair.IsValid()) //Check if we already have this connected
						continue;

					SharedMode::Object* SubObject = Client->FindObject(SubObjectId);
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

void UFusionClient::SendUserRpc(const int64 Id, const SharedMode::PlayerId Player, const AActor* Actor, const char* Data, SIZE_T DataLength)
{
	if (Actor == nullptr)
	{
		Client->SendUserRpc(Client->CreateUserRpc(Id, Player, SharedMode::ObjectId{}, 0, 0, Data, DataLength));
	}
	else
	{
		if (const SharedMode::Object* Obj = FindObject(Actor))
		{
			Client->SendUserRpc(Client->CreateUserRpc(Id, Player, Obj->Id, 0, 0, Data, DataLength));
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
	if (const UTypeDescriptor* Descriptor = Lookup->FindClassDescriptor(Source->GetClass()); Descriptor && Descriptor->EventFunctions.Contains(EventName))
	{
		if (const SharedMode::ObjectId ObjectId = FindObjectId(Source); ObjectId.IsSome())
		{
			//So we can find the actual event when receiving the rpc.
			const uint64 EventHash = Descriptor->EventNameToHash[EventName];

			SharedMode::PlayerId TargetPlayer;
			if (Target == EFusionRPCTarget::SendToMasterClient)
			{
				TargetPlayer = SharedMode::MasterClientPlayerId;
			}
			else if (Target == EFusionRPCTarget::SendToObjectOwner)
			{
				TargetPlayer = SharedMode::ObjectOwnerPlayerId;
			}
			else
			{
				//What do we use to donate no specific player as target and just for all clients?
				TargetPlayer = SharedMode::PlayerId{};

				//Immediate dispatch to self, since everyone is target, (perhaps with small artificial delay to not make local stuff to snappy...)
			}

			const SharedMode::Object* EngineObject = FindObject(Source);

			//Reason why we are sending an Int64 is because it's passed from potential Blueprint source and
			const SharedMode::Rpc Rpc = Client->CreateUserRpc(RPCId, TargetPlayer, ObjectId, Descriptor->TypeHash,
			                                                  EventHash,
			                                                  reinterpret_cast<const char*>(Buffer.GetData()),
			                                                  Buffer.Num());
			
			//UE_LOG(LogTemp, Warning, TEXT("Sending RPC id: %llu  ClientId: %s"), Rpc.Id, *ClientInstanceId.ToString());
			
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
				bool TargetMasterClient = TargetPlayer == SharedMode::MasterClientPlayerId;
				bool TargetOwner = TargetPlayer == SharedMode::ObjectOwnerPlayerId;
				bool TargetPlugin = TargetPlayer == SharedMode::PluginPlayerId;
				
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

FObjectActorPair UFusionClient::RegisterObject(UFusionActorComponent* Source, AActor* OwningActor, UObject* Object, SharedMode::Object* FusionObject, EObjectPairType Type)
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

	FObjectActorPair Pair = FObjectActorPair{Type, OwningActor, Object, Source, FusionObject, FusionObject->Id };

	// Build property state mapping from type descriptor
	if (const TStrongObjectPtr<UTypeDescriptor>* DescPtr = Lookup->HashToDescriptor.Find(FusionObject->Type.Hash))
	{
		if (DescPtr->IsValid())
		{
			const UTypeDescriptor* Desc = DescPtr->Get();
			Pair.PropertyStates.Reserve(Desc->Properties.Num());
			for (const Property* Prop : Desc->Properties)
			{
				Pair.PropertyStates.Add({Prop->EngineProperty, Prop->WordOffset, Prop->WordCount});
			}
		}
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

FObjectActorPair UFusionClient::RegisterRuntimeObject(UFusionActorComponent* Source, AActor* OwningActor, UObject* Object, SharedMode::Object* FusionObject, EObjectPairType Type)
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

	FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
	
	const UTypeDescriptor* Desc = Lookup->CreateTypeDescriptor(Object->GetClass(), BuildOptions);

	//
	FUSION_LOG("Shared Mode Object Registered %s (%s) [hash/id: %llu]", *ObjectIdToString(FusionObject->Id), *Object->GetName(), Desc->TypeHash);

	// store actor reference on object also, but this will
	// not keep the object alive from GC pov.
	FusionObject->Engine = Object;
	
	// mapping of AActor* to object Id
	TempObjectToObjectId.Add(Object, FusionObject->Id);

	FObjectActorPair Pair = FObjectActorPair{ Type, OwningActor, Object, Source, FusionObject, FusionObject->Id };

	// Build property state mapping from type descriptor
	Pair.PropertyStates.Reserve(Desc->Properties.Num());
	for (const Property* Prop : Desc->Properties)
	{
		Pair.PropertyStates.Add({Prop->EngineProperty, Prop->WordOffset, Prop->WordCount});
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
		FKeyObjectId MapActorId(0, MapActorHash);
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
	SharedMode::ObjectOwnerModes OwnerMode = Source->GetTypes(this, Components, Owner->GetRootComponent(), TypesData);

	uint64 WordCount = 0;
	for (FTypeData Item : TypesData)
		WordCount += Item.TypeRef.WordCount;
	
	FTypeData BaseTypeData = TypesData[0];
	const int32 SubObjectCount = TypesData.Num() - 1;
	
	FUSION_LOG("Attempt To Attach Map Actor: %s Index: %d, Scene: %d, words %llu",  *Source->GetOwner()->GetName(), MapActorHash, MapSequence, WordCount);
	
	SharedMode::ObjectSpecialFlags SceneObjectFlags = SharedMode::ObjectSpecialFlags::SceneObject | SharedMode::ObjectSpecialFlags::ExistsOnClient;
	
	bool bExisting{false};
	if (SharedMode::ObjectRoot* Obj = Client->CreateSceneObject(bExisting, BaseTypeData.TypeRef.WordCount, BaseTypeData.TypeRef, nullptr, 0, MapSequence, MapActorHash, OwnerMode, SceneObjectFlags, SubObjectCount))
	{
		Obj->SetSendUpdates(SendUpdates);
		
		Source->SubscribeEvents(Client, Obj->Id);
		
		AActor* Actor = Cast<AActor>(BaseTypeData.Object.Get());
		FObjectActorPair Pair = RegisterObject(Source, Actor, BaseTypeData.Object.Get(), Obj, EObjectPairType::Actor);

		PhotonCommon::StringType ObjectIdString = Obj->Id;
		FUSION_LOG("Created Object Id: %s   Scene: %d   WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(ObjectIdString.c_str()), Obj->Scene, Obj->Words.Length, *BaseTypeData.Object->GetName());

		SharedMode::ObjectId* RequiredObjects = Obj->RequiredObjects();
		int32 RequiredObjectIndex = 0;

		//iterate subobjects
		for (int i = 1; i < TypesData.Num(); i++)
		{
			int SubObjectIndex = i - 1;
			FTypeData SubObjectTypeData = TypesData[i];

			const uint32 SubObjectHash = UFusionHelpers::SafeObjectNameHash(SubObjectTypeData.Object.Get());

			if (TStrongObjectPtr<UTypeDescriptor> Descriptor = Lookup->HashToDescriptor.FindRef(SubObjectTypeData.TypeRef.Hash))
			{
				FString const SubObjectClassPath = Descriptor->Type.Get()->GetPathName();

				TArray SubObjectsTypesData{SubObjectTypeData};
				FString SubObjectTypesJson = UFusionHelpers::GetTypesHeader(this, SubObjectsTypesData);
				const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> SubObjectTypesJsonUTF8 = StringCast<UTF8CHAR>(*SubObjectTypesJson);

				//Similar to parent container object we check if the subobjects just needs to get registered or created.
				//We dont register mapactors or their subobjects in the OnObjectCreatedFinalize.
				//This could potentially run in paralell with a remote scene object being created on the client.
				SharedMode::Object* ExistingSubObject = Client->FindSubObjectWithHash(Obj, SubObjectHash);

				if (!ExistingSubObject)
				{
					SharedMode::ObjectId SubObjectId{ MapActorHash, SubObjectHash};

					SharedMode::ObjectSpecialFlags SubObjectFlags = SceneObjectFlags | SubObjectTypeData.SpecialFlags;
					
					SharedMode::ObjectChild* SubObject = Client->CreateSubObject(Obj->Id,
														 SubObjectTypeData.TypeRef.WordCount,
														 SubObjectTypeData.TypeRef,
														 reinterpret_cast<const PhotonCommon::CharType*>(SubObjectTypesJsonUTF8.Get()),
														 SubObjectTypesJsonUTF8.Length(),
														 SubObjectHash,
														 SubObjectId,
														 SubObjectFlags);

					if (Client->AddSubObject(Obj, SubObject))
					{
						if (RequiredObjects && RequiredObjectIndex < SubObjectCount)
						{
							RequiredObjects[RequiredObjectIndex++] = SubObject->Id;
						}

						FObjectActorPair SubObjectPair = RegisterObject(Source, Actor, SubObjectTypeData.Object.Get(), SubObject, EObjectPairType::Component);
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
					FObjectActorPair SubObjectPair = RegisterObject(Source, Actor, SubObjectTypeData.Object.Get(), ExistingSubObject, EObjectPairType::Component);
					
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

void UFusionClient::AttachGlobalInstanceActor(UObject* Object)
{
	constexpr SharedMode::ObjectOwnerModes OwnerMode = SharedMode::ObjectOwnerModes::GameGlobal;
	
	FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
	const UTypeDescriptor* TypeDescriptor = Lookup->CreateTypeDescriptor(Object->GetClass(), BuildOptions);
	const SharedMode::TypeRef TypeRef = SharedMode::TypeRef{TypeDescriptor->TypeHash, TypeDescriptor->WordCount};

	//Should produce same id on all clients since game instance will not change at runtime.
	// const TStringPointer<char> Name = StringCast<char>(*Object->GetClass()->GetName());
	// const unsigned int Index = Crc32(reinterpret_cast<const unsigned char*>(Name.Get()), strlen(Name.Get()));

	FString Name = *Object->GetClass()->GetName();
	const uint32 Hash = UFusionHelpers::SafeObjectNameHash(TCHAR_TO_ANSI(*Name), Name.Len());

	bool bExisting{false};

	SharedMode::ObjectSpecialFlags Flags = SharedMode::ObjectSpecialFlags::ExistsOnClient;
	
	if (auto* Obj = Client->CreateGlobalInstanceObject(bExisting, TypeRef.WordCount, TypeRef, nullptr, 0, 0, Hash, OwnerMode, Flags))
	{
		FObjectActorPair ObjectPair = RegisterObject(nullptr, nullptr, Object, Obj, EObjectPairType::GlobalInstance);
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
		FUSION_LOG("Created Global Instance Object Id: %s  Scene: %d   WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(ObjectIdString.c_str()), Obj->Scene, Obj->Words.Length, *Object->GetName());
	}
}

void UFusionClient::AttachSpawnedActor(UFusionActorComponent* Source, const uint32 Scene, bool SendUpdates)
{
	AActor* Owner = Source->GetOwner();
		
	TArray<FTypeData> TypesData{};
	SharedMode::ObjectOwnerModes OwnerMode = Source->GetTypes(this, Owner->GetComponents(), Owner->GetRootComponent(), TypesData);

	uint64 WordCount = 0;
	for (FTypeData Item : TypesData)
		WordCount += Item.TypeRef.WordCount;
	
	FUSION_LOG("Attempt To Attach Spawned Actor: %s  Scene: %d, words %llu", *Source->GetOwner()->GetName(), Scene, WordCount);

	//Condense the spawned actor into json
	FString TypesJson = UFusionHelpers::GetTypesHeader(this, TypesData);
	const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> TypesJsonUTF8 = StringCast<UTF8CHAR>(*TypesJson);

	FTypeData BaseTypeData = TypesData[0];

	const int32 SubObjectCount = TypesData.Num() - 1;

	SharedMode::ObjectId PreconfiguredId;
	uint32 ActiveScene = Scene;
	if (Owner->IsA<APlayerState>())
	{
		if (SharedMode::ObjectRoot* Existing = Client->FindObjectRoot(CurrentPlayerStateId))
		{
			//Since player states have a fixed hash we must sure only 1 exists per player between map changes, it will not manually destroy itself.
			Client->DestroyObjectLocal(Existing, true);
			RemoveObjectRoot(Existing);
		}
		
		PreconfiguredId = Client->GetNewObjectId();
		ActiveScene = 0;
		CurrentPlayerStateId = PreconfiguredId;
	}
	
	if (SharedMode::ObjectRoot* Obj = Client->CreateObject(BaseTypeData.TypeRef.WordCount, BaseTypeData.TypeRef,
														   reinterpret_cast<const PhotonCommon::CharType*>(TypesJsonUTF8.Get()), TypesJsonUTF8.Length(),
														   ActiveScene, OwnerMode, Source->FusionObjectFlags, SubObjectCount, PreconfiguredId))
	{
		Obj->SetSendUpdates(SendUpdates);
		
		Source->SubscribeEvents(Client, Obj->Id);

		AActor* Actor = Cast<AActor>(BaseTypeData.Object.Get());
		FObjectActorPair ObjectPair = RegisterObject(Source, Actor, BaseTypeData.Object.Get(), Obj, EObjectPairType::Actor);
		CopyLocalStateToObject(ObjectPair);

		PhotonCommon::StringType ObjectIdString = Obj->Id;
		FUSION_LOG("Created Object Id: %s  Scene: %d   WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(ObjectIdString.c_str()), Obj->Scene, Obj->Words.Length, *BaseTypeData.Object->GetName());

		SharedMode::ObjectId* RequiredObjects = Obj->RequiredObjects();
		int32 RequiredObjectIndex = 0;

		for (int i = 1; i < TypesData.Num(); i++) {
			int SubObjectIndex = i - 1;
			FTypeData SubObjectTypeData = TypesData[i];

			const uint32 SubObjectHash = UFusionHelpers::SafeObjectNameHash(SubObjectTypeData.Object.Get());

			if (TStrongObjectPtr<UTypeDescriptor> Descriptor = Lookup->HashToDescriptor.FindRef(SubObjectTypeData.TypeRef.Hash))
			{
				FString SubObjectClassPath = Descriptor->Type.Get()->GetPathName();
				TArray SubObjectsTypesData{SubObjectTypeData};
				FString SubObjectTypesJson = UFusionHelpers::GetTypesHeader(this, SubObjectsTypesData);
				const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> SubObjectTypesJsonUTF8 = StringCast<UTF8CHAR>(*SubObjectTypesJson);

				//Dynamic subobjects will get their ids same way as parent object.
				SharedMode::ObjectId SubObjectId = Client->GetNewObjectId();

				SharedMode::ObjectChild* SubObject = Client->CreateSubObject(Obj->Id, SubObjectTypeData.TypeRef.WordCount,
					SubObjectTypeData.TypeRef,
					reinterpret_cast<const PhotonCommon::CharType*>(SubObjectTypesJsonUTF8.Get()),
					SubObjectTypesJsonUTF8.Length(),
					SubObjectHash,
					SubObjectId,
					SubObjectTypeData.SpecialFlags);

				if (Client->AddSubObject(Obj, SubObject))
				{
					if (RequiredObjects && RequiredObjectIndex < SubObjectCount)
					{
						RequiredObjects[RequiredObjectIndex++] = SubObject->Id;
					}

					auto SubObjectPair = RegisterObject(Source, Actor, SubObjectTypeData.Object.Get(), SubObject, EObjectPairType::Component);
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

SharedMode::ObjectRoot* UFusionClient::FindRootParent(SharedMode::ObjectId Id)
{
	if (SharedMode::Object* Object = Client->FindObject(Id))
	{
		if (Object->ObjectType == SharedMode::ObjectType::Root)
		{
			return SharedMode::ObjectRoot::Cast(Object);
		}

		if (const SharedMode::ObjectChild* Child = SharedMode::ObjectChild::Cast(Object))
		{
			if (SharedMode::ObjectRoot* FoundParent = FindRootParent(Child->Parent))
			{
				return SharedMode::ObjectRoot::Cast(FoundParent);
			}
		}
	}
	
	return nullptr;
}

SharedMode::Object* UFusionClient::CreateCustomObject(const FCopyContext& Context, UObject* Object, const UTypeDescriptor* Descriptor, uint32 Scene)
{
	SharedMode::ObjectRoot* ParentObject{nullptr};
	if (const UObject* Outer = Object->GetOuter()) {
		if (SharedMode::ObjectRoot* FoundObject = Context.FusionClient->FindObjectRoot(Outer))
		{
			ParentObject = FoundObject;
		}
	}
	
	if (!ParentObject) {
		//Disabled for now, we only ever allow custom objects (subobjects) to be created if they have a network parent.
		//Backup, use the object sitting at the top/root of the copy chain
		//ParentObject = FindRootParent(Context.Pair.Object->Id);
	}

	SharedMode::ObjectId ParentId = ParentObject ? ParentObject->Id : SharedMode::ObjectId();

	if (ParentId.IsSome())
	{
		FObjectActorPair ParentPair = FindObjectPair(ParentId);
		const SharedMode::TypeRef TypeRef = SharedMode::TypeRef{Descriptor->TypeHash, Descriptor->WordCount};
		
		TArray<FTypeData> TypesData{};
		const FTypeData TypeData {
			TypeRef,
			Object
		};
		TypesData.Add(TypeData);
		
		const uint32 SubObjectHash = UFusionHelpers::SafeObjectNameHash(Object);
		
		const FString TypesJson = UFusionHelpers::GetTypesHeader(this, TypesData);
		const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> TypesJsonUTF8 = StringCast<UTF8CHAR>(*TypesJson);
		
		if (!MapActors.Contains(ParentId.Counter))
		{
			SharedMode::ObjectId SubObjectId =  Client->GetNewObjectId();

			SharedMode::ObjectChild* SubObject = Client->CreateSubObject(ParentObject->Id,
															 TypeRef.WordCount,
															 TypeRef,
															 reinterpret_cast<const PhotonCommon::CharType*>(TypesJsonUTF8.Get()),
															 TypesJsonUTF8.Length(),
															 SubObjectHash,
															 SubObjectId,
															 {});
			Client->AddSubObject(ParentObject, SubObject);
			const FObjectActorPair Pair = RegisterRuntimeObject(nullptr, ParentPair.Actor, Object, SubObject, EObjectPairType::CustomObject);
			const PhotonCommon::StringType ObjectIdString = SubObject->Id;
			FUSION_LOG("Created Custom Object Id: %s   Object Hash: %d    WordCount:%llu  From: %s ",  UTF8_TO_TCHAR(ObjectIdString.c_str()), SubObjectHash,  SubObject->Words.Length, *Object->GetName());

			return Pair.Object;
		}
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
	CurrentMapInstance.bAttachCurrent = NewLevelInstance.bAttachCurrent;
	
	TargetMapInstance.Name = FPackageName::GetShortName(NewLevelInstance.Name);

	if (NewLevelInstance.bAttachCurrent)
	{
		//Move directly to active state, no map change will fire.
		//From this state the local client can choose to AttachCurrentWorld (if not master)
		SetMapState(EMapState::LevelActive);
		return;
	}

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

	if (const UPhotonOnlineSubsystemSettings* Settings =
		UPhotonOnlineSubsystemSettings::GetPhotonOnlineSettings()) {
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
		SharedMode::ObjectRoot* Obj = NewRemoteObjectRoots[i];

		//Assume local client has locally connected these objects.
		if ((Obj->SpecialFlags & SharedMode::ObjectSpecialFlags::ExistsOnClient) != SharedMode::ObjectSpecialFlags::None)
		{
			NewRemoteObjectRoots.RemoveAt(i);
			i--;
			continue;
		}

		// belongs to an old scene, ignore.
		if (Obj->Scene < CurrentMapInstance.Sequence && Obj->Scene != 0)
		{
			NewRemoteObjectRoots.RemoveAt(i);
			i--;
			continue;
		}
		
		// belongs to a newer scene, ignore but don't remove
		if (Obj->Scene > CurrentMapInstance.Sequence)
		{
			continue;
		}

		if (Obj->Scene == CurrentMapInstance.Sequence || Obj->Scene == 0)
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
		SharedMode::ObjectChild* Obj = NewRemoteObjectChildren[i];
		const SharedMode::ObjectRoot* Root = Client->GetRoot(Obj);

		if (Root == nullptr)
		{
			NewRemoteObjectChildren.RemoveAt(i);
			i--;
			continue;
		}

		//Assume local client has locally connected these objects.
		if ((Obj->SpecialFlags & SharedMode::ObjectSpecialFlags::ExistsOnClient) != SharedMode::ObjectSpecialFlags::None)
		{
			NewRemoteObjectChildren.RemoveAt(i);
			i--;
			continue;
		}
		
		if (Root->Scene < CurrentMapInstance.Sequence && Root->Scene != 0)
		{
			NewRemoteObjectChildren.RemoveAt(i);
			i--;
			continue;
		}
		
		// belongs to a newer scene, ignore but don't remove
		if (Root->Scene > CurrentMapInstance.Sequence)
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
		const TObjectPtr<UFusionActorComponent> Source = Pending.Source;
		
		if (IsValid(Pending.Object) == false)
		{
			FUSION_LOG_ERROR("Pending Actor Object is invalid");
			PendingObjects.RemoveAt(i);
			i--;
			continue;
		}
	
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
			AttachGlobalInstanceActor(Pending.Object);
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

		SharedMode::Object* Current = Client->FindObject(Pair.ObjectId);

		if (!Current)
		{
			RemoveAfterEndBeginFrame.Add(Pair.ObjectId);
			continue;
		}

		//Since not every tick will have a network/plugin update we reset the received bytes
		if (!Client->HasBeenUpdatedByPlugin(Current, false))
		{
			Current->ResetReceivedBytes();
		}
		
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::BeginFrame::UpdateRemoteObjectsActorState::GetRoot);
			const SharedMode::ObjectRoot* Root = Client->GetRoot(Current);
			if (!Root) {
				FUSION_LOG_ERROR("GetRoot returned null for object [%d:%d]", Pair.Object->Id.Origin, Pair.Object->Id.Counter);
				continue;
			}

			if (Root->Scene != CurrentMapInstance.Sequence && Root->Scene != 0)
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

			if (SharedMode::ObjectChild* Child = SharedMode::ObjectChild::Cast(Pair.Object)) {
				const SharedMode::ObjectId ParentId = SharedMode::ObjectId(Child->Parent);
				if (const FObjectActorPair ParentPair = FindObjectPair(ParentId); ParentPair.Actor) {
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

void UFusionClient::UpdateRemoteState(const FObjectActorPair& Pair, const FPackagedSettings& Settings, const double Dt)
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
		for (SharedMode::ObjectId Id : RemoveAfterEndBeginFrame)
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

	TArray<SharedMode::ObjectChild*> SubObjectsToDestroy;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects);

		for (auto& [_, Pair] : ObjectIdToPair)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::ValidateAndLookup);

			const SharedMode::Object* Current;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::FindObject);
				Current = Client->FindObject(Pair.ObjectId);
			}

			if (!Current)
			{
				RemoveAfterEndBeginFrame.Add(Pair.ObjectId);
				continue;
			}

			const SharedMode::ObjectRoot* Root;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::GetRoot);
				Root = Client->GetRoot(Current);
			}
			if (!Root) {
				FUSION_LOG_ERROR("GetRoot returned null for object [%d:%d]", Pair.Object->Id.Origin, Pair.Object->Id.Counter);
				continue;
			}

			if (Root->Scene != CurrentMapInstance.Sequence && Root->Scene != 0)
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

			if (Pair.ObjectType == EObjectPairType::Actor && Pair.Actor)
			{
				if (CanModify)
				{
					//Find deleted subobjects
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyAndSubObjects::FindDeletedSubObjects);
						const std::vector<SharedMode::ObjectId>& RegisteredSubObjects = Client->GetSubObject(Pair.Object);

						if (!RegisteredSubObjects.empty())
						{
							const TSet<UActorComponent*>& ActorComponents = Pair.Actor->GetComponents();

							for (const auto& SubObjectId : RegisteredSubObjects)
							{
								if (const FObjectActorPair* SubObjectActorPair = ObjectIdToPair.Find(SubObjectId);
									SubObjectActorPair && SubObjectActorPair->ObjectType == EObjectPairType::Component)
								{
									if (!ActorComponents.Contains(Cast<UActorComponent>(SubObjectActorPair->EngineObject.Get())))
									{
										if (SharedMode::ObjectChild* Child = SharedMode::ObjectChild::Cast(SubObjectActorPair->Object))
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
						SharedMode::ObjectId ObjectId = FindObjectId(ReplicatedComponent);
						SharedMode::Object* Object = Client->FindObject(ObjectId);

						if (!Object)
						{
							FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
							UTypeDescriptor* Descriptor = Lookup->CreateTypeDescriptor(ReplicatedComponent->GetClass(), BuildOptions);

							SharedMode::TypeRef TypeRef{
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

							SharedMode::ObjectRoot* PairRoot = SharedMode::ObjectRoot::Cast(Pair.Object);

							//Similar to parent container object we check if the subobjects just needs to get registered or created.
							//We dont register mapactors or their subobjects in the OnObjectCreatedFinalize.
							//This could potentially run in paralell with a remote scene object being created on the client.
							SharedMode::Object* ExistingSubObject = Client->FindSubObjectWithHash(PairRoot, SubObjectHash);

							//We dont allow adding dynamic component adding on map actors (for now)
							if (!ExistingSubObject && !MapActors.Contains(Pair.Object->Id.Counter))
							{
								SharedMode::ObjectId SubObjectId = Client->GetNewObjectId();

								auto SubObject = Client->CreateSubObject(Pair.Object->Id, Descriptor->WordCount,
											TypeRef,
											reinterpret_cast<const PhotonCommon::CharType*>(SubObjectTypesJsonUTF8.Get()),
											SubObjectTypesJsonUTF8.Length(),
											SubObjectHash,
											SubObjectId,
											SubObjectTypeData.SpecialFlags);


								if (Client->AddSubObject(PairRoot, SubObject))
								{
									auto NewSubObjectPair = RegisterRuntimeObject(nullptr, Pair.Actor, SubObjectTypeData.Object.Get(), SubObject, EObjectPairType::Component);
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

		for (SharedMode::ObjectChild* SubObject : SubObjectsToDestroy)
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

void UFusionClient::CheckForMasterChange()
{
	const bool bIsMasterClient = Client->IsMasterClient();

	if (bIsMasterClient && !bPreviousMasterClientState)
	{
		SequenceIncrementAmount = 1000;
		FUSION_LOG("Just became Master Client");
	}

	bPreviousMasterClientState = bIsMasterClient;
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

			CheckForMasterChange();

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

			// 5. clean up old/invalid actor pairs
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
		CurrentMapInstance.Sequence += SequenceIncrementAmount;
		CurrentMapInstance.Name = MapName;
		
		FUSION_LOG("Starting to load map (mc): %s (%i)", *CurrentMapInstance.Name, CurrentMapInstance.Sequence);
		
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("MapName"), CurrentMapInstance.Name);
		Payload->SetBoolField(TEXT("Attached"), AttachCurrent);

		FString PayloadString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadString);
		FJsonSerializer::Serialize(Payload, Writer);

		const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> MapNameUTF8 = StringCast<UTF8CHAR>(*PayloadString);
			
		// trigger level change for other clients
		Client->ChangeScene(0, CurrentMapInstance.Sequence, reinterpret_cast<const PhotonCommon::CharType*>(MapNameUTF8.Get()));

		if (SequenceIncrementAmount > 1)
		{
			SequenceIncrementAmount = 1;
		}
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
	if (const SharedMode::ObjectId Id = FindObjectId(Actor); Id.IsSome())
	{
		if (const FObjectActorPair* Pair = ObjectIdToPair.Find(Id))
		{
			return Client->GetTime(Pair->Object);
		}
	}

	return 0;
}

void UFusionClient::CopyRemoteStateToObject(FCopyContext& Context, const FObjectActorPair& Pair, const bool IsInitialUpdate)
{
	SharedMode::TypeRef TypeRef = Pair.Object->Type;
	const TStrongObjectPtr<UTypeDescriptor>* DescPtr = Lookup->HashToDescriptor.Find(TypeRef.Hash);

	if (!DescPtr || !DescPtr->IsValid() || !DescPtr->Get()->Type) {
		//FUSION_LOG("Failed to find class of type %llu", TypeRef.Hash);
		return;
	}

	const UTypeDescriptor* Desc = DescPtr->Get();

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
		
		bool IsRootTransform = (Pair.Object->SpecialFlags & SharedMode::ObjectSpecialFlags::IsRootTransform) != SharedMode::ObjectSpecialFlags::None;
		bool IgnoreRootTransformProperties = (Pair.Object->SpecialFlags & SharedMode::ObjectSpecialFlags::IgnoreRootTransformProperties) != SharedMode::ObjectSpecialFlags::None;

		for (const Property* Prop : Desc->Properties) {
			check(Prop->WordOffset < Pair.Object->Words.Length);

			//Special case where initial values are updated for properties when object is created.
			//Subsequent updates will be ignored for this property. This is mostly useful for root transform properties that have alternate update paths, eg: being updated by physics or movement component.
			if (!IsInitialUpdate)
			{
				if (Prop->IsTransformProperty && IsRootTransform && IgnoreRootTransformProperties)
				{
					continue;
				}
			}
	
			Prop->CopyFrom(this, Context, Container, Pair.Object->Words.Ptr + Prop->WordOffset, Pair.Object->Shadow.Ptr + Prop->WordOffset, false);
		}
		
		if (!bSkipPostNetReceive) {
			Container->PostNetReceive();
		}

		InvokeOnReps(Container, Context.OnReps);

		//FUSION_LOG("Updating Container: %s State", *Container->GetName());
		Container->PostRepNotifies();

		//Object has completed a full state update.
		Pair.Object->SetHasValidData(true); 

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

void UFusionClient::CopyLocalStateToObject(FObjectActorPair& Pair)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::EndFrame::CopyLocalStateToObject);
	SharedMode::TypeRef TypeRef = Pair.Object->Type;
	const TStrongObjectPtr<UTypeDescriptor>* DescPtr = Lookup->HashToDescriptor.Find(TypeRef.Hash);

	if (!DescPtr || !DescPtr->IsValid() || !DescPtr->Get()->Type) {
		//FUSION_LOG("Failed to find class of type %llu", TypeRef.Hash);
		return;
	}

	const UTypeDescriptor* Desc = DescPtr->Get();

	if (Pair.EngineObject) {
		FCopyContext Root
		{
			Pair,
			this
		};

		const bool bHasShadow = Pair.Object->Shadow.Ptr != nullptr && Pair.Object->Shadow.Length == Pair.Object->Words.Length;
		const bool bHasMatchingStates = Pair.PropertyStates.Num() == Desc->Properties.Num();

		for (int32 Index = 0; Index < Desc->Properties.Num(); ++Index)
		{
			const Property* Prop = Desc->Properties[Index];
			const int32 Offset = Prop->WordOffset;
			check(Offset < Pair.Object->Words.Length);

			Prop->CopyTo(this, Root, Pair.EngineObject, Pair.Object->Words.Ptr + Offset);

			if (bHasMatchingStates)
			{
				FPropertyWordState& State = Pair.PropertyStates[Index];
				int32 ChangedWords = 0;

				if (bHasShadow)
				{
					const SharedMode::Word* Current = Pair.Object->Words.Ptr + Offset;
					const SharedMode::Word* Shadow = Pair.Object->Shadow.Ptr + Offset;
					for (int32 WordIndex = 0; WordIndex < Prop->WordCount; ++WordIndex)
					{
						if (Current[WordIndex] != Shadow[WordIndex])
						{
							++ChangedWords;
						}
					}
				}

				State.ChangedWordCount = ChangedWords;
			}
		}
	}
}

void UFusionClient::OnActorSpawned(AActor* SpawnedActor)
{
	if (APlayerController* const SpawnedPlayerController = Cast<APlayerController>(SpawnedActor))
	{
		PlayerController = SpawnedPlayerController;

		if (FusionNetDriver)
		{
			PlayerController->NetConnection = FusionNetDriver->ServerConnection;
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

void UFusionClient::OnEngineObjectDestroyed(const UObject* DestroyedObject, const bool bEngineObjectDestroyed)
{
	if (bBlockNextDestroy)
	{
		bBlockNextDestroy = false;
		return;
	}
	
	SharedMode::ObjectId id = FindObjectId(DestroyedObject);
	if (id.IsSome())
	{
		FObjectActorPair Pair = FindObjectPair(id);
		if (Pair.IsValid())
		{
			Pair.Settings->OnObjectDestroyed.Broadcast(EFusionObjectDestroyMode::Local);
			
			SharedMode::ObjectRoot* RootObject = SharedMode::ObjectRoot::Cast(Pair.Object);
			if (Client->DestroyObjectLocal(RootObject, bEngineObjectDestroyed) == false)
			{
				FUSION_LOG_WARN("Engine destroyed actor %s which we don't have authority to destroy", *DestroyedObject->GetName());
			}
		}
		else
		{
			FUSION_LOG_ERROR("Engine destroyed actor %s which doesnt have a mapped object pair", *DestroyedObject->GetName());

			if (SharedMode::ObjectRoot* RootObject = FindObjectRoot(DestroyedObject))
			{
				if (Client->DestroyObjectLocal(RootObject, bEngineObjectDestroyed) == false)
				{
					FUSION_LOG_WARN("Engine destroyed actor %s which we don't have authority to destroy", *DestroyedObject->GetName());
				}
			}
		}
	}

}

void UFusionClient::UpdateOwnedActorAreaInterestKeys(TFunctionRef<uint64(const AActor*)> KeyFunc)
{
	for (auto& [_, Pair] : ObjectIdToPair)
	{
		if (SharedMode::ObjectRoot::Cast(Pair.Object) == nullptr) continue;
		if (!Pair.Actor) continue;

		bool CanModify;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::UpdateOwnedActorAreaInterestKeys::CanModify);
			CanModify = Client->CanModify(Pair.Object);
		}

		if (!CanModify) continue;

		if (Client->HasSetInterestKey(Pair.Object) && Client->GetInterestKeyType(Pair.Object) != SharedMode::InterestKeyType::Area) {
			continue;
			}

		if (const uint64 Key = KeyFunc(Pair.Actor); Key != 0) {
			Client->SetAreaInterestKey(Pair.Object, Key);
		}
	}
}

void UFusionClient::AddDependencyCheck(const SharedMode::ObjectId Id, const FCopyContext& Root, const TFunction<bool()>& Callback)
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

SharedMode::ObjectId UFusionClient::FindObjectId(const UObject* Object)
{
	if (Object)
	{
		if (const uint64* Result = ObjectToObjectId.Find(Object); Result)
		{
			return SharedMode::ObjectId(*Result);
		}

		if (const uint64* Result = TempObjectToObjectId.Find(Object); Result)
		{
			return SharedMode::ObjectId(*Result);
		}
	}

	return SharedMode::ObjectId();
}

UObject* UFusionClient::FindObject(const SharedMode::ObjectId Id)
{
	if (const FObjectActorPair* Result = ObjectIdToPair.Find(Id))
	{
		return Result->EngineObject;
	}

	if (const FObjectActorPair* Result = TempObjectIdToPair.Find(Id))
	{
		return Result->EngineObject;
	}

	return nullptr;
}

FObjectActorPair UFusionClient::FindObjectPair(SharedMode::ObjectId Id)
{
	if (const FObjectActorPair* Result = ObjectIdToPair.Find(Id))
	{
		return *Result;
	}

	return FObjectActorPair();
}

SharedMode::Object* UFusionClient::FindObject(const UObject* Object)
{
	if (Client)
	{
		if (const SharedMode::ObjectId Id = FindObjectId(Object); Id.IsSome())
		{
			return Client->FindObject(Id);
		}
	}

	return nullptr;
}

SharedMode::ObjectRoot* UFusionClient::FindObjectRoot(const UObject* Actor)
{
	if (Client)
	{
		if (const SharedMode::ObjectId Id = FindObjectId(Actor); Id.IsSome())
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

void UFusionClient::OnObjectReady(SharedMode::ObjectRoot* Obj)
{
	NewRemoteObjectRoots.Add(Obj);

	SharedMode::ObjectId* required = Obj->RequiredObjects();
	const int32_t count = Obj->RequiredObjectsCount();
	for (int32_t i = 0; i < count; i++)
	{
		if (auto* child = SharedMode::ObjectChild::Cast(Client->FindObject(required[i])))
		{
			NewRemoteObjectChildren.Add(child);
		}
	}
}

void UFusionClient::OnSubObjectCreated(SharedMode::ObjectChild* Obj)
{
	NewRemoteObjectChildren.Add(Obj);
}

void UFusionClient::CopyToBackBuffer(const FObjectActorPair& Pair)
{
	SharedMode::TypeRef TypeRef = Pair.Object->Type;
	const TStrongObjectPtr<UTypeDescriptor> Desc = Lookup->HashToDescriptor.FindRef(TypeRef.Hash);

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
		
		for (const Property* Prop : Desc->Properties)
		{
			check(Prop->WordOffset < Pair.Object->Words.Length);
			Prop->CopyTo(this, Context, Pair.EngineObject, Pair.Object->Shadow.Ptr + Prop->WordOffset);
		}
	}
}

bool UFusionClient::OnObjectCreatedFinalize(SharedMode::Object* Obj)
{
	if (!CurrentWorld.IsValid())
	{
		FUSION_LOG_ERROR("OnObjectCreatedFinalize: No world set!");
		return false;
	}
	
	if (Obj->Header.Length > 0)
	{
		TArray<UClass*> LoadedClasses;
		TArray<FString> LoadedNames;
		
		FString TypesJson(Obj->Header.Length, reinterpret_cast<char*>(Obj->Header.Ptr));
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
						FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
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

			SharedMode::TypeRef BaseTypeRef = TypesData[0].TypeRef;
			FObjectActorPair Pair = RegisterObject(ActorSource, Actor, Actor, Obj, EObjectPairType::Actor);

			//CopyLocalStateToObject(FObjectActorPair{Actor, Actor, Actor->GetName(), Obj});
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

			FUSION_LOG("Engine Object Created %s (obj-lvlseq:%i, client-lvlseq:%i ActorName: %s, Words: %llu)", *ObjectIdToString(Obj->Id), Client->GetRoot(Obj)->Scene, CurrentMapInstance.Sequence, *Actor->GetName(), WordCount);
		}
		else if (StartClass->IsChildOf(UActorComponent::StaticClass()))
		{
			if (FObjectActorPair ParentPair = FindObjectPair(SharedMode::ObjectChild::GetParent(Obj)); ParentPair.Actor)
			{
				FUSION_LOG("Getting Parent Object: %s", *ParentPair.Actor->GetName());
				
				if (TStrongObjectPtr<UTypeDescriptor> SubType = Lookup->HashToDescriptor.FindRef(Obj->Type.Hash))
				{
					SharedMode::ObjectChild* ObjChild = SharedMode::ObjectChild::Cast(Obj);

					//Ensure we fetch the correct component, since multiple of the same type can exist on the actor.
					UActorComponent* FoundComponent{nullptr};
					TSet<UActorComponent*> Components = ParentPair.Actor->GetComponents();
					for (UActorComponent* Component : Components)
					{
						FString SubObjectName = Component->GetName();
						const uint32 SubObjectHash = UFusionHelpers::SafeObjectNameHash(TCHAR_TO_ANSI(*SubObjectName), SubObjectName.Len());

						if (SubObjectHash == ObjChild->TargetObjectHash) {
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
						FObjectActorPair Pair = RegisterObject(nullptr, ParentPair.Actor, FoundComponent, Obj, EObjectPairType::Component);
						
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
			SharedMode::ObjectId ParentId = SharedMode::ObjectId(SharedMode::ObjectChild::GetParent(Obj));

			if (UObject* ParentObject = FindObject(ParentId)) {
				FUSION_LOG("Getting Parent Object: %s", *ParentObject->GetName());
				
				if (UObject* CreatedObject = NewObject<UObject>(ParentObject, StartClass))
				{
					FUSION_LOG("Created New Custom Object: %s", *CreatedObject->GetName());
					
					FObjectActorPair Pair = RegisterObject(nullptr, Cast<AActor>(ParentObject), CreatedObject, Obj, EObjectPairType::CustomObject);

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

UObject* UFusionClient::RemoveObjectPairs(const SharedMode::ObjectId Id)
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

	if (SharedMode::ObjectRoot* Root = SharedMode::ObjectRoot::Cast(Client->FindObject(Id)))
	{
		NewRemoteObjectRoots.Remove(Root);
	}

	return Object;
}

UObject* UFusionClient::RemoveObjectRoot(const SharedMode::ObjectRoot* Root)
{
	if (!Root)
		return nullptr;

	NewRemoteObjectRoots.Remove(const_cast<SharedMode::ObjectRoot*>(Root));
	
	return RemoveObjectPairs(Root->Id);
}

void UFusionClient::OnSubObjectDestroyed(SharedMode::ObjectChild* Obj, const SharedMode::DestroyModes Mode)
{
	NewRemoteObjectChildren.Remove(Obj);
	if (UObject* Object = RemoveObjectPairs(Obj->Id))
	{
		if (Mode == SharedMode::DestroyModes::Remote || Mode == SharedMode::DestroyModes::Shutdown || Mode == SharedMode::DestroyModes::SceneChange)
		{
			if (UActorComponent* Component = Cast<UActorComponent>(Object))
			{
				Component->DestroyComponent();
			}
		}
	}
}

void UFusionClient::OnObjectDestroyed(const SharedMode::ObjectRoot* Obj, const SharedMode::DestroyModes Mode)
{
	if (Obj &&  (Obj->SpecialFlags & SharedMode::ObjectSpecialFlags::SceneObject) != SharedMode::ObjectSpecialFlags::None)
	{
		OnMapActorDestroyedRemote(Obj->Scene, Obj->Id, Mode);
	}
	else
	{
		if (Mode == SharedMode::DestroyModes::Remote || Mode == SharedMode::DestroyModes::Shutdown || Mode == SharedMode::DestroyModes::SceneChange)
		{
			bBlockNextDestroy = true;

			FObjectActorPair Pair = FindObjectPair(Obj->Id);
			if (Pair.Actor)
			{
				Pair.Settings.Get()->OnObjectDestroyed.Broadcast(ToUnrealDestroyMode(Mode));
				Pair.Actor->Destroy(true);
			}
			RemoveObjectRoot(Obj);

			bBlockNextDestroy = false;
		}
	}
}

void UFusionClient::OnMapActorDestroyedRemote(uint32 SceneSequence, const SharedMode::ObjectId Id, const SharedMode::DestroyModes Mode)
{
	FObjectActorPair Pair = FindObjectPair(Id);
	
	if (Pair.IsValid())
	{
		bBlockNextDestroy = true;

		if (Pair.Actor)
		{
			Pair.Settings.Get()->OnObjectDestroyed.Broadcast(ToUnrealDestroyMode(Mode));
			Pair.Actor->Destroy(true);
		}

		bBlockNextDestroy = false;
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

void UFusionClient::OnSceneChange([[maybe_unused]] uint32 Index, const uint32 Sequence, const SharedMode::Data Data)
{
	if (CurrentMapInstance.Sequence >= Sequence)
	{
		// The current map we have loaded is from further in the future than this requested map change.
		// So ignore it as we are already more up to date than this request.
		return;
	}

	const FString PayloadString = FString::ConstructFromPtrSize(reinterpret_cast<char*>(Data.Ptr), Data.Length);

	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadString);
	FJsonSerializer::Deserialize(Reader, JsonObject);

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

	FMapInstance RequestedMapInstance;
	RequestedMapInstance.Name = LevelNameCopy;
	RequestedMapInstance.Sequence = Sequence;
	RequestedMapInstance.bAttachCurrent = bAttachCurrent;
	
	RequestedMapInstances.HeapPush(RequestedMapInstance);

	if (!bAttachCurrent)
	{
		//Since potential destroy calls for objects can start happening after this has fired we need to ensure we are no longer on LevelActive trying to iterate things.
		SetMapState(EMapState::HasRequestToChangeLevel);
	}

	FUSION_LOG("Wanted Map Name: %s, total number of maps to load: %i", *LevelNameCopy, RequestedMapInstances.Num());
}

auto UFusionClient::OnRpcReceived(const SharedMode::Rpc& Rpc) -> void
{
	//UE_LOG(LogTemp, Warning, TEXT("Received RPC id: %llu   ClientId: %s"), Rpc.Id, *ClientInstanceId.ToString());
	
	if (Rpc.TargetObject.IsSome())
	{
		if (UObject* Object = FindObject(Rpc.TargetObject))
		{
			if (const TStrongObjectPtr<UTypeDescriptor> Desc = Lookup->HashToDescriptor.FindRef(Rpc.DescriptorTypeHash))
			{
				if (const FString* EventNamePtr = Desc->EventHashToName.Find(Rpc.EventHash))
				{
					const FString EventName = *EventNamePtr;
					UFunctionDescriptor* FunctionDescriptor = *Desc->EventFunctions.Find(EventName);
	
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

					Object->ProcessEvent(FunctionDescriptor->Function, Params);

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
					FUSION_LOG_WARN("RPC EventHash: %llu not found, ensure source type has correct event output", Rpc.EventHash);
				}
			}
			else
			{
				FUSION_LOG_WARN("RPC Descriptor with hash: %llu not found, make sure the type sending the RPC is among mapped types.", Rpc.DescriptorTypeHash);
			}
		}
		else
		{
			FUSION_LOG_WARN("RPC target object: %s not found", *ObjectIdToString(Rpc.TargetObject));
		}
	}
	else
	{
		FUSION_LOG_WARN("RPC TargetObject not set");
	}
}

bool UFusionClient::IsLoadingMap()
{
	return CurrentMapState == EMapState::IsLoading;
}

void UFusionClient::OnMapInit(UWorld* World)
{
	CurrentWorld = World;

	FUSION_LOG("UFusionClient::OnMapInit(%s)  LoadedWorldIndex: %d  Name: %s   ClientId: %s", *UEnum::GetValueAsString(ClientInstanceType), World->GetUniqueID(), *World->GetName(), *ClientInstanceId.ToString());
}

void UFusionClient::OnMapDestroy(UWorld* World)
{
	if (World != CurrentWorld)
	{
		return;
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
		const FObjectActorPair& Pair = It->Value;
		
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

		SharedMode::ObjectRoot* Root = Pair.Object->Root();

		if (!Root)
		{
			ObjectToObjectId.Remove(Pair.EngineObject);
			It.RemoveCurrent();
			continue;
		}

		FObjectActorPair RootPair = FindObjectPair(Root->Id);
		if (RootPair.Actor &&  (Pair.Object->SpecialFlags & SharedMode::ObjectSpecialFlags::SceneObject) == SharedMode::ObjectSpecialFlags::None)
		{
			//Do no destroy proxies. Should be handled in OnObjectDestroy
			if (RootPair.Actor->GetLocalRole() == ROLE_SimulatedProxy)
			{
				continue;
			}
		}

		if (Root && Root->Scene == 0)
		{
			//These object have custom lifecycles, dont remove when map changes.
			continue;
		}
		
		if (Root && Root->Scene < CurrentMapInstance.Sequence)
		{
			ObjectToObjectId.Remove(Pair.EngineObject);
			It.RemoveCurrent();
		}
	}

	for (auto It = TempObjectIdToPair.CreateIterator(); It; ++It)
	{
		const FObjectActorPair& Pair = It->Value;
		
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
		
		SharedMode::ObjectRoot* Root = Pair.Object->Root();
		if (Root && Root->Scene == 0)
		{
			continue;
		}
		
		if (Root && Root->Scene < CurrentMapInstance.Sequence)
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

	SharedMode::Client* Client2 = this->Client;

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
	if (SharedMode::Object* Obj = FindObject(Owner))
	{
		Obj->SetSendUpdates(bToggle);
	}
}

void UFusionClient::Startup(UWorld* InitialWorld, UTypeLookup* TypeLookup, const UPhotonOnlineSubsystemSettings* Settings, PhotonMatchmaking::RealtimeClient& RealtimeClient)
{
	check (InitialWorld);
	check (TypeLookup);

	Client = new SharedMode::Client(RealtimeClient);

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
	const ULevelEditorPlaySettings* PlayInSettings = GetMutableDefault<ULevelEditorPlaySettings>();
	check(PlayInSettings);
	PlayInSettings->GetRunUnderOneProcess(bRunUnderOneProcess);
#endif
	
	FUSION_LOG("Made Client Instance with Id: %s  Type: %s  InitialWorld: %s", *ClientInstanceId.ToString(), *UEnum::GetValueAsString(ClientInstanceType), *CurrentWorld->GetName());

	ClientSubscriptionBag += Client->OnForcedDisconnect.Subscribe([this](std::string message)
		{
			this->OnForcedDisconnect(FString(UTF8_TO_TCHAR(message.c_str())));
		});

	ClientSubscriptionBag += Client->OnObjectReady.Subscribe([this](SharedMode::ObjectRoot* Obj)
	{
		this->OnObjectReady(Obj);
	});

	ClientSubscriptionBag += Client->OnSubObjectCreated.Subscribe( [this](SharedMode::ObjectChild* Obj)
	{
		this->OnSubObjectCreated(Obj);
	});

	ClientSubscriptionBag += Client->OnSubObjectDestroyed.Subscribe( [this](SharedMode::ObjectChild* Obj, const SharedMode::DestroyModes Mode)
	{
		this->OnSubObjectDestroyed(Obj, Mode);
	});

	ClientSubscriptionBag += Client->OnObjectDestroyed.Subscribe( [this](const SharedMode::ObjectRoot* Obj, const SharedMode::DestroyModes Mode)
	{
		this->OnObjectDestroyed(Obj, Mode);
	});

	ClientSubscriptionBag += Client->OnSceneChange.Subscribe( [this](const uint32_t Index, const uint32_t Sequence, const SharedMode::Data& Scene)
	{
		this->OnSceneChange(Index, Sequence, Scene);
	});

	ClientSubscriptionBag += Client->OnRpc.Subscribe([this](const SharedMode::Rpc& RPC)
	{
		this->OnRpcReceived(RPC);
	});

	ClientSubscriptionBag += Client->OnDestroyedMapActor.Subscribe([this](uint32 SceneSequence, const SharedMode::ObjectId Id)
	{
		this->OnMapActorDestroyedRemote(SceneSequence, Id, SharedMode::DestroyModes::Remote);
	});

	ClientSubscriptionBag += Client->OnFusionStart.Subscribe([this]()
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
	});

	Client->Start();
}

void UFusionClient::Shutdown()
{
	SetMapState(EMapState::Shutdown);

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
	}
	
	FusionNetDriver = nullptr;
	CurrentWorld = nullptr;
	Lookup = nullptr;
}
