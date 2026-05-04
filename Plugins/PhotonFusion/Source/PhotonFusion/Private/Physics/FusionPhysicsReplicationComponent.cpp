// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Physics/FusionPhysicsReplicationComponent.h"
#include "FusionOnlineSubsystem.h"
#include "FusionShared.h"
#include "Components/SphereComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Physics/FusionPhysicsUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"  

UFusionPhysicsReplicationComponent::UFusionPhysicsReplicationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	SetIsReplicatedByDefault(true); // This ensures the component replicates by default
}

void UFusionPhysicsReplicationComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
		return;

	FusionPhysicsUtils::InitiatePhysicsReplication(this);
	
	if (!Primitive)
		return;
	
	//Ensure default movement replication code does not run for the actor.
	//Fusion will run its own logic for this. (Previous bug when running the vehicle/car demo)
	Owner->SetReplicatingMovement(false);

	// Register hit events so we can disable forecast correction for a period after collision.
	// This requires "generate hit events" to be enabled.
	if (Primitive)
	{
		Primitive->OnComponentHit.AddDynamic(this, &UFusionPhysicsReplicationComponent::OnHitCallback);

		if (USceneComponent* Root = Owner->GetRootComponent(); Root != nullptr)
		{
			Root->TransformUpdated.AddUObject(this, &UFusionPhysicsReplicationComponent::OnTransformUpdated);
		}
	}
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void UFusionPhysicsReplicationComponent::OnHitCallback([[maybe_unused]] UPrimitiveComponent* HitComp, AActor* OtherActor, [[maybe_unused]] UPrimitiveComponent* OtherComp, [[maybe_unused]] FVector NormalImpulse, [[maybe_unused]] const FHitResult& Hit)
{
	if (OtherActor)
	{
		if (const UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(GetOwner())->GetSubsystem<
			UFusionOnlineSubsystem>())
		{
			OnlineSubsystem->RegisterForecastCollision(this);
		}
	}
}

void UFusionPhysicsReplicationComponent::OnTransformUpdated([[maybe_unused]] USceneComponent* InRootComponent, [[maybe_unused]] EUpdateTransformFlags UpdateTransformFlags, const ETeleportType Teleport)
{
	// Detect if this was an intentional teleport
	if (const UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(GetWorld())->GetSubsystem<
		UFusionOnlineSubsystem>(); Teleport == ETeleportType::TeleportPhysics && OnlineSubsystem->CanModify(GetOwner()))
	{
		// Increase the Teleport key so that teleports can be identified on the remote clients.
		// This value is synced with the BodyState in the next tick.
		// The BodyState.TeleportKey value is not set directly here as otherwise the teleport key change may be replicated before/after the position change.
		TeleportKey++;

		FUSION_LOG("Teleport. New teleport key: %i", BodyState.TeleportKey);
	}
}

void UFusionPhysicsReplicationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (Primitive)
	{
		Primitive->OnComponentHit.RemoveDynamic(this, &UFusionPhysicsReplicationComponent::OnHitCallback);
	}

	AActor* Owner = GetOwner();

	if (Owner != nullptr)
	{
		if (USceneComponent* Root = Owner->GetRootComponent(); Root != nullptr)
		{
			Root->TransformUpdated.RemoveAll(this);
		}
	}
}

void UFusionPhysicsReplicationComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(GetWorld())->GetSubsystem<
		UFusionOnlineSubsystem>();
	const AActor* ComponentOwner = GetOwner();

    if (!Primitive)
    {
	    return;
    }
	
	// If we can modify the physics object we harvest the rigid bodies state.
	// This state will be sent over to the other none owner clients.
	if (OnlineSubsystem->CanModify(ComponentOwner))
	{
		int ServerFrame = 0;
		if (FRigidBodyState State; FusionPhysicsUtils::GetPhysicsBodyState(GetWorld(), Primitive, State, ServerFrame))
		{
			const bool bTeleportKeyChanged = TeleportKey != BodyState.TeleportKey;
			const bool bSleepChanged = (State.Flags & ERigidBodyFlags::Sleeping) != (BodyState.Flags & ERigidBodyFlags::Sleeping);

			// Always capture sleep transitions immediately — the receiving client
			// uses the Sleeping flag to skip gravity extrapolation. A stale "Awake"
			// flag causes the extrapolated target to drift underground, triggering
			// hard-snap corrections that bounce the object into the air.
			if (bSleepChanged || bTeleportKeyChanged)
			{
				BodyState.LinVel = State.LinVel;
				BodyState.AngVel = State.AngVel;
				BodyState.Quaternion = State.Quaternion;
				BodyState.Position = State.Position;
				BodyState.Flags = State.Flags;
				BodyState.TeleportKey = TeleportKey;
			}
			else
			{
				const FVector PosDelta = State.Position - BodyState.Position;
				const FVector LinVelDelta = State.LinVel - BodyState.LinVel;
				const FVector AngVelDelta = State.AngVel - BodyState.AngVel;
				const FQuat QuatDelta = State.Quaternion * BodyState.Quaternion.Inverse();
				const bool bQuatChanged = !QuatDelta.IsIdentity(0.0001f);

				if (!PosDelta.IsNearlyZero(0.01f) || !LinVelDelta.IsNearlyZero(0.01f) || !AngVelDelta.IsNearlyZero(0.01f) || bQuatChanged)
				{
					BodyState.LinVel = State.LinVel;
					BodyState.AngVel = State.AngVel;
					BodyState.Quaternion = State.Quaternion;
					BodyState.Position = State.Position;
					BodyState.Flags = State.Flags;
					BodyState.TeleportKey = TeleportKey;
				}
			}
		}
	}
}

bool UFusionPhysicsReplicationComponent::ShouldPredictPhysics(const FFusionReplicatedPhysicsTarget& Target, FBodyInstance*& BodyInstance)
{
	BodyInstance = Primitive->GetBodyInstance(Target.BoneName);
	if (BodyInstance)
	{
		return UpdatePhysicsPrediction(*BodyInstance);
	}

	return false;
}

bool UFusionPhysicsReplicationComponent::UpdatePhysicsPrediction_Implementation(FBodyInstance& BodyInstance)
{
	return BodyInstance.IsInstanceSimulatingPhysics();
}

bool UFusionPhysicsReplicationComponent::OnComponentProject(float DeltaSeconds, double TimeDifference,
                                                            FBodyInstance* BodyInstance, FFusionReplicatedPhysicsTarget& ReplicatedTarget)
{
	return false;
}

void UFusionPhysicsReplicationComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFusionPhysicsReplicationComponent, BodyState);
}

void UFusionPhysicsReplicationComponent::PostRepNotifies()
{
	Super::PostRepNotifies();

	const UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(GetWorld())->GetSubsystem<
		UFusionOnlineSubsystem>();
	if (!OnlineSubsystem->IsConnected())
		return;

	const AActor* ComponentOwner = GetOwner();

	if (!Primitive)
		return;

	bool bReceiveStateUpdates;
	if (OnlineSubsystem->HasOwner(ComponentOwner)) {
		bReceiveStateUpdates = !OnlineSubsystem->IsOwner(ComponentOwner);
	}
	else {
		bReceiveStateUpdates = !OnlineSubsystem->IsMasterClient();
	}

	//If owner of physics object we harvest the rigid bodies state.
	//This state will be sent over to the other none owner clients.
	if (bReceiveStateUpdates)
	{
		if (FusionPhysicsReplication* Replication = FusionPhysicsUtils::GetReplication(GetWorld()))
		{
			if (Replication->ReplacePhysicsState(this, BodyState)) {
				//FUSION_LOG("Rot X: %f, Rot Y: %f, Rot Z: %f", BodyState.LinVel.X, BodyState.LinVel.Y, BodyState.LinVel.Z);

				// Set the local key to match the key from the BodyState so we can increase it if this client becomes the owner
				TeleportKey = BodyState.TeleportKey;
			}
		}
	}
}

