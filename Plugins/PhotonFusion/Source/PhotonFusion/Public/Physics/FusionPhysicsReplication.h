// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <tuple>
#include "PhysicsReplicationInterface.h"
#include "CoreMinimal.h"
#include "PhysicsReplication.h"
#include "FusionPhysicsReplication.generated.h"

USTRUCT(BlueprintType)
struct FFusionBodyState
{
	GENERATED_BODY()

	
	UPROPERTY()
	FVector Position{FVector::ZeroVector};

	UPROPERTY()
	FVector LinVel{FVector::ZeroVector};

	UPROPERTY()
	FVector AngVel{FVector::ZeroVector};

	UPROPERTY()
	FQuat Quaternion{FQuat::Identity};

	UPROPERTY()
	int32 TeleportKey{0};

	UPROPERTY()
	int32 Flags{}; //TODO: Make fusion uint8 (Dont need 4 bytes here)

	UPROPERTY()
	int32 ServerFrame{}; 
};

USTRUCT(BlueprintType)
struct FFusionReplicatedPhysicsTarget
{
	GENERATED_BODY()
	
	FFusionReplicatedPhysicsTarget()
		: ArrivedTimeSeconds(0.0f)
		, AccumulatedErrorSeconds(0.0f)
		, AccumulatedRemoteSleepIgnoreTime(0.0f)
		, CollisionLastFrame(false)
		, CollisionThisFrame(false)
		, TimeSinceLastOnCollisionEnter(FLT_MAX)
		, PrevPosTarget(0)
		, PrevPos(0)
		, TeleportKey(0)
		, PrevTeleportKey(0)
		, ServerFrame(0)
	{ }

	/** The target state replicated by server */
	FRigidBodyState TargetState;

	/** The bone name used to find the body */
	FName BoneName;

	/** Client time when target state arrived */
	float ArrivedTimeSeconds;

	/** Physics sync error accumulation */
	float AccumulatedErrorSeconds;

	/** Track how long local has nto slept for */
	float AccumulatedRemoteSleepIgnoreTime;

	/** Used to keep track of collisions */
	bool CollisionLastFrame;
	bool CollisionThisFrame;
	float TimeSinceLastOnCollisionEnter;

	/** Correction values from previous update */
	FVector PrevPosTarget;
	FVector PrevPos;

	/** Key used to differentiate between several teleports */
	int32 TeleportKey;
	int32 PrevTeleportKey;

	/** ServerFrame this target was replicated on (must be converted to local frame prior to client-side use) */
	int32 ServerFrame;

#if !UE_BUILD_SHIPPING
	FDebugFloatHistory ErrorHistory;
#endif
};


class UFusionPhysicsReplicationComponent;

class FusionPhysicsReplication: public IPhysicsReplication
{
public:
	explicit FusionPhysicsReplication(FPhysScene* PhysScene);
		virtual ~FusionPhysicsReplication() override;
    
    	virtual void Tick(float DeltaSeconds) override;
    
    	virtual void SetReplicatedTarget(UPrimitiveComponent* Component, FName BoneName, const FRigidBodyState& ReplicatedTarget, int32 ServerFrame) override;
    
    	virtual void RemoveReplicatedTarget(UPrimitiveComponent* Component) override;

		bool ReplacePhysicsState(UFusionPhysicsReplicationComponent* Component, const FFusionBodyState& State);

		bool RegisterCollision(UFusionPhysicsReplicationComponent* Component);

		static FVector PredictPosition(float DeltaTime, const FVector& InitPos, const FVector& InitVel, const FVector& Gravity, float LinearDamping);

		static void ComputeExtrapolatedSnapshot(UWorld* World, class UFusionActorComponent* Settings, UFusionPhysicsReplicationComponent* Component, FBodyInstance* BodyInstance, FRigidBodyState& Remote, FRigidBodyState& Result);

		static void PerformImmediateMove(const FRigidBodyState& Target, FBodyInstance* BodyInstance);
private:
		static FVector PredictVelocity(float DeltaTime, const FVector& InitVel, const FVector& Gravity, float LinearDamping);

		FVector CalculateLinearVelocity(const UFusionActorComponent* Settings, float PhysicsFps, const FFusionReplicatedPhysicsTarget& LocalSnapshot, const FFusionReplicatedPhysicsTarget& ExtrapolatedSnapshot);

		FVector CalculateAngularVelocity(const UFusionActorComponent* Settings, const FFusionReplicatedPhysicsTarget& RemoteSnapshot, const FFusionReplicatedPhysicsTarget& ExtrapolatedSnapshot);

		TTuple<bool, bool> DetectError(const UFusionActorComponent* Settings, float PhysicsDt, const FFusionReplicatedPhysicsTarget& LocalSnapshot, const FFusionReplicatedPhysicsTarget& ExtrapolatedSnapshot, FFusionReplicatedPhysicsTarget& PhysicsTarget);

		void ApplyCorrection(UFusionActorComponent* Settings, float PhysicsDt, float PhysicsFps, FBodyInstance* PhysicsBody, float TimeSinceLastOnCollisionEnter,
		                     FFusionReplicatedPhysicsTarget& LocalSnapshot, FFusionReplicatedPhysicsTarget& RemoteSnapshot, FFusionReplicatedPhysicsTarget& ExtrapolatedSnapshot);

	TMap<TWeakObjectPtr<UFusionPhysicsReplicationComponent>, FFusionReplicatedPhysicsTarget> ComponentToTargets;
	FPhysScene_Chaos* PhysScene_Chaos{nullptr};
};
