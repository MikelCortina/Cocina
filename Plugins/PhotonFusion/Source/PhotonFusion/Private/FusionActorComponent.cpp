// Copyright 2026 Exit Games GmbH. All Rights Reserved.
// ReSharper disable CppUnusedIncludeDirective
#include "FusionActorComponent.h"

#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionShared.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Physics/FusionPhysicsReplicationComponent.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/FusionPhysicsUtils.h"
#include "PhotonOnlineSubsystemSettings.h"
#include "Engine/PackageMapClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
// ReSharper restore CppUnusedIncludeDirective

#define FUSION_SETTING_POST_EDIT_CHANGE(SETTING)\
if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UFusionActorComponent, SETTING))\
{\
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ToggleEditable)\
	{\
		if (Override ## SETTING)\
		{\
			SETTING = CachedOverride ## SETTING;\
		}\
		else\
		{\
			SETTING = SubSystemSettings->SETTING;\
		}\
	}\
\
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)\
	{\
		if (Override ## SETTING)\
		{\
			CachedOverride ## SETTING = SETTING;\
		}\
	}\
}\

#define FUSION_SETTING_POST_INIT(SETTING, INVALIDVALUE)\
if (SETTING == INVALIDVALUE)\
{\
	SETTING = SubSystemSettings->SETTING;\
}\
else if (!Override ## SETTING)\
{\
	SETTING = SubSystemSettings->SETTING;\
}\
\
if (CachedOverride ## SETTING == INVALIDVALUE)\
{\
	CachedOverride ## SETTING = SubSystemSettings->SETTING;\
}\

#define FUSION_SETTING_GETTER_DEFINITION(TYPE, SETTING)\
TYPE UFusionActorComponent::Get ## SETTING() const\
{\
	if (Override ## SETTING)\
	{\
		return SETTING;\
	}\
\
	const UPhotonOnlineSubsystemSettings* SubSettings = UPhotonOnlineSubsystemSettings::GetPhotonOnlineSettings();\
\
	return SubSettings->SETTING;\
};\

void FFusionComponentRef::GetComponent([[maybe_unused]] const AActor* Owner)
{

}

UFusionActorComponent::UFusionActorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

bool UFusionActorComponent::ShouldAddComponentType_Implementation(UActorComponent* Component)
{
	return true;
}

void UFusionActorComponent::BeginPlay()
{
	Super::BeginPlay();

	SetComponentTickEnabled(AutoDynamicOwnershipRange > 0);

	if (bSkipAutoAttach)
	{
		return;
	}
	
	AddActorSource();
}

void UFusionActorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);
	
	// explicit destroy
	if (Reason == EEndPlayReason::Type::Destroyed)
	{
		const auto OwnerActor = GetOwner();
		if (const auto Fusion = UGameplayStatics::GetGameInstance(OwnerActor)->GetSubsystem<UFusionOnlineSubsystem>())
		{
			if (Fusion->IsFusionRunning())
			{
				Fusion->GFusionClient->OnEngineObjectDestroyed(OwnerActor, true);
			}
		}
	}

	// changing map
	else if (Reason == EEndPlayReason::Type::LevelTransition)
	{
		
	}

	// level streaming
	else if (Reason == EEndPlayReason::Type::RemovedFromWorld)
	{
		
	}

	RemoveEvents();
}

void UFusionActorComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction){
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (const UFusionOnlineSubsystem* Fusion = UGameplayStatics::GetGameInstance(GetOwner())->GetSubsystem<
		UFusionOnlineSubsystem>())
	{
		if (AutoDynamicOwnershipRange > 0)
		{
			if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
			{
				const bool WantsOwner = FVector::Distance(GetOwner()->GetActorLocation(), Pawn->GetActorLocation()) <
					AutoDynamicOwnershipRange;
				Fusion->SetWantsOwner(GetOwner(), WantsOwner);	
			}
		}
	}
}

void UFusionActorComponent::AddActorSource()
{
	if (UFusionOnlineSubsystem* Fusion = UGameplayStatics::GetGameInstance(GetOwner())->GetSubsystem<UFusionOnlineSubsystem>())
	{
		if (Fusion->IsFusionRunning())
		{
			Fusion->GFusionClient->AddActorSource(this);
		}
	}
}

void UFusionActorComponent::CopyLocalStateNextFrame()
{
	bLocalStateCopyPending = true;
}

bool UFusionActorComponent::ConsumePendingLocalStateCopy()
{
	const bool Requested = bLocalStateCopyPending;
	bLocalStateCopyPending = false;
	return Requested;
}

void UFusionActorComponent::ToggleNetworkSend(bool bToggle)
{
	if (UFusionOnlineSubsystem* Fusion = UGameplayStatics::GetGameInstance(GetOwner())->GetSubsystem<UFusionOnlineSubsystem>())
	{
		if (Fusion->IsFusionRunning())
		{
			Fusion->GFusionClient->ToggleNetworkSend(this, bToggle);
		}
	}
}

SharedMode::ObjectOwnerModes UFusionActorComponent::GetOwnerMode()
{
	return static_cast<SharedMode::ObjectOwnerModes>(Ownership);
}

SharedMode::ObjectOwnerModes UFusionActorComponent::GetTypes(const UFusionClient* Client, TSet<UActorComponent*> Components, USceneComponent* RootComponent, TArray<FTypeData>& OutTypeData)
{
	AActor* Actor = GetOwner();

	bool bDisableRootTransformUpdates = false;
	if (bForecastPhysicsEnabled && RootComponent && RootComponent->GetClass()->IsChildOf(UPrimitiveComponent::StaticClass()) && RootComponent->IsSimulatingPhysics()) {
		bDisableRootTransformUpdates = true;
	}

	if (Actor->GetComponentByClass<UMovementComponent>())
	{
		bDisableRootTransformUpdates = true;
	}
	
	FPropertyBuildOptions DefaultBuildOptions = UTypeLookup::GetDefaultBuildOptions();

	const UTypeDescriptor* ActorDesc = Client->Lookup->CreateTypeDescriptor(Actor->GetClass(), DefaultBuildOptions);

	const FTypeData BaseData{
		SharedMode::TypeRef{ActorDesc->TypeHash, ActorDesc->WordCount},
		Actor
	};
	OutTypeData.Add(BaseData);
	
	for (TSet<UActorComponent*>::TIterator It = Components.CreateIterator(); It; ++It)
	{
		UActorComponent* ActorComponent = *It;
		UClass* Cls = ActorComponent->GetClass();
		
		if (Cls->IsChildOf(StaticClass()))
		{
			continue;
		}
		
		FPropertyBuildOptions ComponentBuildOptions = UTypeLookup::GetDefaultBuildOptions();
		SharedMode::ObjectSpecialFlags SpecialFlags{};
		if (ActorComponent == RootComponent)
		{
			SpecialFlags |= SharedMode::ObjectSpecialFlags::IsRootTransform;
			if (bDisableRootTransformUpdates)
			{
				SpecialFlags |= SharedMode::ObjectSpecialFlags::IgnoreRootTransformProperties;
			}

			ComponentBuildOptions.IsRootTransform = true;
		}
		else
		{
			//Movement component is implicitly networked and always allowed through.
			bool IsMovementComponent = ActorComponent->GetClass()->IsChildOf(UMovementComponent::StaticClass());
			if (!IsMovementComponent && !ActorComponent->GetIsReplicated())
			{
				continue;
			}
		}
			
		if (const UTypeDescriptor* CompDescriptor = Client->Lookup->CreateTypeDescriptor(Cls, ComponentBuildOptions); CompDescriptor) {
			if (ShouldAddComponentType(ActorComponent))
			{
				FTypeData SubObjectData{
					SharedMode::TypeRef{CompDescriptor->TypeHash, CompDescriptor->WordCount},
					ActorComponent,
					SpecialFlags
				};
				OutTypeData.Add(SubObjectData);
			}
			else
			{
				FUSION_LOG("Skipping component class: %s, component is disallowed", *Cls->GetName());
			}
		}
	}

	if (bForecastPhysicsEnabled)
	{
		//If physics is replicated the last type we check for (or add) is the physics replication component.
		CheckPhysicsReplication(Client, Actor, OutTypeData);
	}

	return GetOwnerMode();
}

void UFusionActorComponent::CheckPhysicsReplication(const UFusionClient* Client, AActor* Actor, TArray<FTypeData>& OutTypeData)
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents(PrimitiveComponents);

	TArray<UFusionPhysicsReplicationComponent*> ReplicationComponents;
	Actor->GetComponents(ReplicationComponents);
	
	for (UPrimitiveComponent* Primitive : PrimitiveComponents)
	{
		if (!Primitive->IsSimulatingPhysics())
			continue;
		
		if (ReplicationComponents.FindByPredicate([&Primitive] (const UFusionPhysicsReplicationComponent* Comp)
		{
			return Comp->Primitive == Primitive;
		}))
		{
			continue;
		}

		UFusionPhysicsReplicationComponent* ReplicationComponent = NewObject<UFusionPhysicsReplicationComponent>(Actor, TEXT("FusionPhysicsReplication"));
		ReplicationComponent->SetNetAddressable();
		ReplicationComponent->SetIsReplicated(true);
		ReplicationComponent->RegisterComponent();
		
		FusionPhysicsUtils::InitiatePhysicsReplication(ReplicationComponent);

		FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
		
		if (const UTypeDescriptor* CompDescriptor = Client->Lookup->CreateTypeDescriptor(
			UFusionPhysicsReplicationComponent::StaticClass(), BuildOptions); CompDescriptor)
		{
			FTypeData SubObjectData{
				SharedMode::TypeRef{CompDescriptor->TypeHash, CompDescriptor->WordCount},
				ReplicationComponent
			};
			OutTypeData.Add(SubObjectData);
		}
	}
}

FPackagedSettings UFusionActorComponent::PackageSettings()
{
	FPackagedSettings PackagedSettings;
	
	PackagedSettings.bForecastPhysicsEnabled = bForecastPhysicsEnabled;
	PackagedSettings.bSkipPreNetReceive = bSkipPreNetReceive;
	PackagedSettings.bSkipPostNetReceive = bSkipPostNetReceive;
	PackagedSettings.ActorSettings = this;

	return PackagedSettings;
}

void UFusionActorComponent::RemoveEvents()
{
	BridgeSubscriptions.UnsubscribeAll();
}

void UFusionActorComponent::SubscribeEvents(SharedMode::Client* Client, SharedMode::ObjectId Id)
{
	RemoveEvents();
	
	BridgeSubscriptions += Client->OnObjectOwnerChanged.Subscribe([this, Id](SharedMode::ObjectRoot* Obj)
	{
		if (Obj->Id == Id)
		{
			this->OnOwnerChanged.Broadcast();
		}
	});

	BridgeSubscriptions += Client->OnOwnerWasGiven.Subscribe([this, Id](SharedMode::ObjectRoot* Obj)
	{
		if (Obj->Id == Id)
		{
			this->OnOwnerWasGiven.Broadcast();
		}
	});

	BridgeSubscriptions += Client->OnInterestEnter.Subscribe([this, Id](SharedMode::ObjectRoot* Obj)
	{
		if (Obj->Id == Id)
		{
			this->OnInterestEnter.Broadcast();
		}
	});

	BridgeSubscriptions += Client->OnInterestExit.Subscribe([this, Id](SharedMode::ObjectRoot* Obj)
	{
		if (Obj->Id == Id)
		{
			this->OnInterestExit.Broadcast();
		}
	});
}

#if WITH_EDITOR
void UFusionActorComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const UPhotonOnlineSubsystemSettings* SubSystemSettings = UPhotonOnlineSubsystemSettings::GetPhotonOnlineSettings();

	if (PropertyChangedEvent.Property)
	{
		// Start of post edit change property checks that override those found in PhotonOnlineSubsystemSettings.h
		
		FUSION_SETTING_POST_EDIT_CHANGE(LinearVelCorrectionMul);
		FUSION_SETTING_POST_EDIT_CHANGE(AngularVelCorrectionMul);
		FUSION_SETTING_POST_EDIT_CHANGE(ImpactStartCorrectionTime);
		FUSION_SETTING_POST_EDIT_CHANGE(ImpactCorrectionTimeComplete);
		FUSION_SETTING_POST_EDIT_CHANGE(PositionCorrectionLerp);
		FUSION_SETTING_POST_EDIT_CHANGE(RotationCorrectionLerp);
		FUSION_SETTING_POST_EDIT_CHANGE(Spring);
		FUSION_SETTING_POST_EDIT_CHANGE(Damper);
		FUSION_SETTING_POST_EDIT_CHANGE(MinLinearDetectedError);
		FUSION_SETTING_POST_EDIT_CHANGE(MinAngularDetectedError);
		FUSION_SETTING_POST_EDIT_CHANGE(MaxLinearError);
		FUSION_SETTING_POST_EDIT_CHANGE(MaxAngularError);
		FUSION_SETTING_POST_EDIT_CHANGE(LowCorrectionProgressThreshold);
		FUSION_SETTING_POST_EDIT_CHANGE(HighErrorSimilarityThreshold);
		FUSION_SETTING_POST_EDIT_CHANGE(MaxErrorTotalTime);
		FUSION_SETTING_POST_EDIT_CHANGE(MaxRemoteSleepIgnoreTime);
		FUSION_SETTING_POST_EDIT_CHANGE(MaxExtrapolationTime);
		FUSION_SETTING_POST_EDIT_CHANGE(MaxSpawnExtrapolationTime);
		FUSION_SETTING_POST_EDIT_CHANGE(DeltaRotationScale);
		FUSION_SETTING_POST_EDIT_CHANGE(GravityForecast);
		FUSION_SETTING_POST_EDIT_CHANGE(ErrorCorrectionType);

		// End of post edit change property checks that override those found in PhotonOnlineSubsystemSettings.h
	}
}

void UFusionActorComponent::PostLoad()
{
	const UPhotonOnlineSubsystemSettings* SubSystemSettings = UPhotonOnlineSubsystemSettings::GetPhotonOnlineSettings();

	// Start of post init property checks that override those found in PhotonOnlineSubsystemSettings.h
	
	FUSION_SETTING_POST_INIT(LinearVelCorrectionMul, FLT_MAX);
	FUSION_SETTING_POST_INIT(AngularVelCorrectionMul, FLT_MAX);
	FUSION_SETTING_POST_INIT(ImpactStartCorrectionTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(ImpactCorrectionTimeComplete, FLT_MAX);
	FUSION_SETTING_POST_INIT(PositionCorrectionLerp, FLT_MAX);
	FUSION_SETTING_POST_INIT(RotationCorrectionLerp, FLT_MAX);
	FUSION_SETTING_POST_INIT(Spring, FLT_MAX);
	FUSION_SETTING_POST_INIT(Damper, FLT_MAX);
	FUSION_SETTING_POST_INIT(MinLinearDetectedError, FLT_MAX);
	FUSION_SETTING_POST_INIT(MinAngularDetectedError, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxLinearError, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxAngularError, FLT_MAX);
	FUSION_SETTING_POST_INIT(LowCorrectionProgressThreshold, FLT_MAX);
	FUSION_SETTING_POST_INIT(HighErrorSimilarityThreshold, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxErrorTotalTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxRemoteSleepIgnoreTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxExtrapolationTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxSpawnExtrapolationTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(DeltaRotationScale, FLT_MAX);
	FUSION_SETTING_POST_INIT(GravityForecast, EFusionGravityForecast::Invalid);
	FUSION_SETTING_POST_INIT(ErrorCorrectionType, EFusionPhysicsCorrection::Invalid);

	// End of post init property checks that override those found in PhotonOnlineSubsystemSettings.h

	Super::PostLoad();

}
#endif // WITH_EDITOR


void UFusionActorComponent::PostInitProperties()
{
	const UPhotonOnlineSubsystemSettings* SubSystemSettings = UPhotonOnlineSubsystemSettings::GetPhotonOnlineSettings();

	// Start of post init property checks that override those found in PhotonOnlineSubsystemSettings.h
	
	FUSION_SETTING_POST_INIT(LinearVelCorrectionMul, FLT_MAX);
	FUSION_SETTING_POST_INIT(AngularVelCorrectionMul, FLT_MAX);
	FUSION_SETTING_POST_INIT(ImpactStartCorrectionTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(ImpactCorrectionTimeComplete, FLT_MAX);
	FUSION_SETTING_POST_INIT(PositionCorrectionLerp, FLT_MAX);
	FUSION_SETTING_POST_INIT(RotationCorrectionLerp, FLT_MAX);
	FUSION_SETTING_POST_INIT(Spring, FLT_MAX);
	FUSION_SETTING_POST_INIT(Damper, FLT_MAX);
	FUSION_SETTING_POST_INIT(MinLinearDetectedError, FLT_MAX);
	FUSION_SETTING_POST_INIT(MinAngularDetectedError, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxLinearError, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxAngularError, FLT_MAX);
	FUSION_SETTING_POST_INIT(LowCorrectionProgressThreshold, FLT_MAX);
	FUSION_SETTING_POST_INIT(HighErrorSimilarityThreshold, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxErrorTotalTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxRemoteSleepIgnoreTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxExtrapolationTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(MaxSpawnExtrapolationTime, FLT_MAX);
	FUSION_SETTING_POST_INIT(DeltaRotationScale, FLT_MAX);
	FUSION_SETTING_POST_INIT(GravityForecast, EFusionGravityForecast::Invalid);
	FUSION_SETTING_POST_INIT(ErrorCorrectionType, EFusionPhysicsCorrection::Invalid);

	// End of post init property checks that override those found in PhotonOnlineSubsystemSettings.h

	Super::PostInitProperties();
}

// Start of getters for settings

	FUSION_SETTING_GETTER_DEFINITION(float, LinearVelCorrectionMul);
	FUSION_SETTING_GETTER_DEFINITION(float, AngularVelCorrectionMul);
	FUSION_SETTING_GETTER_DEFINITION(float, ImpactStartCorrectionTime);
	FUSION_SETTING_GETTER_DEFINITION(float, ImpactCorrectionTimeComplete);
	FUSION_SETTING_GETTER_DEFINITION(float, PositionCorrectionLerp);
	FUSION_SETTING_GETTER_DEFINITION(float, RotationCorrectionLerp);
	FUSION_SETTING_GETTER_DEFINITION(float, Spring);
	FUSION_SETTING_GETTER_DEFINITION(float, Damper);
	FUSION_SETTING_GETTER_DEFINITION(float, MinLinearDetectedError);
	FUSION_SETTING_GETTER_DEFINITION(float, MinAngularDetectedError);
	FUSION_SETTING_GETTER_DEFINITION(float, MaxLinearError);
	FUSION_SETTING_GETTER_DEFINITION(float, MaxAngularError);
	FUSION_SETTING_GETTER_DEFINITION(float, LowCorrectionProgressThreshold);
	FUSION_SETTING_GETTER_DEFINITION(float, HighErrorSimilarityThreshold);
	FUSION_SETTING_GETTER_DEFINITION(float, MaxErrorTotalTime);
	FUSION_SETTING_GETTER_DEFINITION(float, MaxRemoteSleepIgnoreTime);
	FUSION_SETTING_GETTER_DEFINITION(float, MaxExtrapolationTime);
	FUSION_SETTING_GETTER_DEFINITION(float, MaxSpawnExtrapolationTime);
	FUSION_SETTING_GETTER_DEFINITION(float, DeltaRotationScale);
	FUSION_SETTING_GETTER_DEFINITION(EFusionGravityForecast, GravityForecast);
	FUSION_SETTING_GETTER_DEFINITION(EFusionPhysicsCorrection, ErrorCorrectionType);
	
// End of getters for settings
