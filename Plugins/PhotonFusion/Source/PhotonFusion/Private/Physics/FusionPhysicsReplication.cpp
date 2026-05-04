// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Physics/FusionPhysicsReplication.h"

#include "FusionOnlineSubsystem.h"
#include "FusionActorComponent.h"
#include "FusionUtils.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Physics/FusionPhysicsReplicationComponent.h"
#include "Engine/GameInstance.h"

void ComputeDeltas(const FVector& CurrentPos, const FQuat& CurrentQuat, const FVector& TargetPos, const FQuat& TargetQuat, FVector& OutLinDiff, float& OutLinDiffSize,
                   FVector& OutAngDiffAxis, float& OutAngDiff, float& OutAngDiffSize)
{
	OutLinDiff = TargetPos - CurrentPos;
	OutLinDiffSize = OutLinDiff.Size();
	const FQuat InvCurrentQuat = CurrentQuat.Inverse();
	const FQuat DeltaQuat = TargetQuat * InvCurrentQuat;
	DeltaQuat.ToAxisAndAngle(OutAngDiffAxis, OutAngDiff);
	OutAngDiff = FMath::RadiansToDegrees(FMath::UnwindRadians(OutAngDiff));
	OutAngDiffSize = FMath::Abs(OutAngDiff);
}

FusionPhysicsReplication::FusionPhysicsReplication(FPhysScene* physScene)
{
	if (FPhysScene_Chaos* Scene = static_cast<FPhysScene_Chaos*>(physScene))
	{
		PhysScene_Chaos = Scene;
	}
}

FusionPhysicsReplication::~FusionPhysicsReplication()
{
}

void FusionPhysicsReplication::Tick(float DeltaSeconds)
{
	if (!PhysScene_Chaos || !PhysScene_Chaos->GetOwningWorld())
	{
		return;
	}

	//Ensure we don't tick here unless the gameworld is properly running. Since maps can sometimes tick components.
	UWorld* World = PhysScene_Chaos->GetOwningWorld();
	if (!World->IsGameWorld() || !World->HasBegunPlay())
	{
		return;
	}

	UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(World)->GetSubsystem<
		UFusionOnlineSubsystem>();

	if (!OnlineSubsystem->IsConnected())
		return;

	// this type name is insane, leave it as auto
	for (auto Itr = ComponentToTargets.CreateIterator(); Itr; ++Itr)
	{
		if (UFusionPhysicsReplicationComponent* Component = Itr.Key().Get())
		{
			if (!Component->Primitive)
				continue;

			FFusionReplicatedPhysicsTarget& PhysicsTarget = Itr.Value();
			FBodyInstance* BodyInstance{nullptr};
			if (!Component->ShouldPredictPhysics(PhysicsTarget, BodyInstance))
			{
				continue;
			}

			PhysicsTarget.TimeSinceLastOnCollisionEnter += DeltaSeconds;

			if (BodyInstance)
			{
				//The client that can modify the physics body should obviously skip prediction.
				if (OnlineSubsystem->CanModify(Component->GetOwner()))
				{
					continue;
				}

				//Don't bother with predicting objects that have "sleeping" state updates.
				if (double TimeSinceStateUpdate = World->GetTimeSeconds() - PhysicsTarget.ArrivedTimeSeconds; TimeSinceStateUpdate > 1.0)
				{
					FRigidBodyState CurrentState;
					BodyInstance->GetRigidBodyState(CurrentState);
					
					//Update previous states while idling.
					PhysicsTarget.PrevPosTarget = CurrentState.Position;
					PhysicsTarget.PrevPos = CurrentState.Position;
				}
				else
				{
					if (AActor* ComponentOwner = Component->GetOwner(); OnlineSubsystem->HasOwner(ComponentOwner))
					{
						UFusionActorComponent* Settings = ComponentOwner->GetComponentByClass<UFusionActorComponent>();

						if (!Settings)
						{
							FUSION_LOG_ERROR("Missing FusionActorSettings component");
							return;
						}
						
						FFusionReplicatedPhysicsTarget LocalSnapshot;
						BodyInstance->GetRigidBodyState(LocalSnapshot.TargetState);

						FFusionReplicatedPhysicsTarget ExtrapolatedSnapshot;
						ComputeExtrapolatedSnapshot(World, Settings, Component, BodyInstance, PhysicsTarget.TargetState, ExtrapolatedSnapshot.TargetState);

						//FUSION_LOG_WARN("Target: %s, result: %s", *PhysicsTarget.TargetState.Position.ToString(), *extrapolatedSnapshot.TargetState.Position.ToString());

						bool bCorrect = true;
						bool bHardSnap = false;

						if (PhysicsTarget.TeleportKey != PhysicsTarget.PrevTeleportKey)
						{
							FUSION_LOG("Teleport");
							bHardSnap = true;
						}
						else
						{
							TTuple<bool, bool> Results = DetectError(Settings, DeltaSeconds, LocalSnapshot,
							                                         ExtrapolatedSnapshot, PhysicsTarget);

							bCorrect = Results.Get<0>();
							bHardSnap = Results.Get<1>();
						}

						const bool bShouldSleep = (ExtrapolatedSnapshot.TargetState.Flags & ERigidBodyFlags::Sleeping) != 0;

						if (!bCorrect)
						{
							//FUSION_LOG_WARN("Not large enough error, so not correcting");
						}
						else if (PhysicsTarget.TimeSinceLastOnCollisionEnter < Settings->GetImpactStartCorrectionTime())
						{
							//FUSION_LOG("Do nothing immediately after collisions");
						}
						else if (bHardSnap)
						{
							PerformImmediateMove(ExtrapolatedSnapshot.TargetState, BodyInstance);
						}
						else
						{
							ApplyCorrection(Settings, DeltaSeconds, 1.0f / DeltaSeconds, BodyInstance, PhysicsTarget.TimeSinceLastOnCollisionEnter,
								LocalSnapshot, PhysicsTarget, ExtrapolatedSnapshot);
						}

						if (bShouldSleep)
						{
							//FUSION_LOG("PutInstanceToSleep");
							BodyInstance->PutInstanceToSleep();
						}

						PhysicsTarget.CollisionLastFrame = PhysicsTarget.CollisionThisFrame;
						PhysicsTarget.CollisionThisFrame = false;

						PhysicsTarget.PrevPosTarget = ExtrapolatedSnapshot.TargetState.Position;
						PhysicsTarget.PrevPos = FVector(LocalSnapshot.TargetState.Position);

						PhysicsTarget.PrevTeleportKey = PhysicsTarget.TeleportKey;
					}
				}
			}
		}
		else
		{
			Itr.RemoveCurrent();
		}
	}
}

void FusionPhysicsReplication::SetReplicatedTarget(UPrimitiveComponent* Component, FName BoneName,const FRigidBodyState& ReplicatedTarget, int32 ServerFrame)
{
}

void FusionPhysicsReplication::RemoveReplicatedTarget(UPrimitiveComponent* Component)
{

}

// Called by client receiving state update
bool FusionPhysicsReplication::ReplacePhysicsState(UFusionPhysicsReplicationComponent* Component, const FFusionBodyState& State)
{
	if (!Component)
		return false;
	
	if (PhysScene_Chaos && PhysScene_Chaos->GetOwningWorld())
	{
		const TWeakObjectPtr<UFusionPhysicsReplicationComponent> TargetKey(Component);
		FFusionReplicatedPhysicsTarget* Target = ComponentToTargets.Find(TargetKey);
		if (!Target)
		{
			// First time we add a target, set it's previous and correction
			// positions to the target position to avoid math with uninitialized
			// memory.
			Target = &ComponentToTargets.Add(TargetKey);
			
			Target->PrevPos = State.Position;
			Target->PrevPosTarget = State.Position;
		}
	
		FRigidBodyState targetState;
		targetState.Position = State.Position;
		targetState.Quaternion = State.Quaternion;
		targetState.LinVel = State.LinVel;
		targetState.AngVel = State.AngVel;
		targetState.Flags = State.Flags;
	
		Target->TeleportKey = State.TeleportKey;
		Target->ServerFrame = State.ServerFrame;
		Target->TargetState = targetState;
		Target->BoneName = NAME_None;
		Target->ArrivedTimeSeconds = PhysScene_Chaos->GetOwningWorld()->GetTimeSeconds();
		
		return true;
	}
	
	return false;
}


bool FusionPhysicsReplication::RegisterCollision(UFusionPhysicsReplicationComponent* Component)
{
	if (!Component)
		return false;

	if (PhysScene_Chaos && PhysScene_Chaos->GetOwningWorld())
	{
		const TWeakObjectPtr<UFusionPhysicsReplicationComponent> TargetKey(Component);

		if (FFusionReplicatedPhysicsTarget* Target = ComponentToTargets.Find(TargetKey))
		{
			if (!Target->CollisionLastFrame)
			{
				FUSION_LOG_TRACE("Register new hit");
				Target->TimeSinceLastOnCollisionEnter = 0;
			}

			Target->CollisionThisFrame = true;
			return true;
		}
	}

	return false;
}

void FusionPhysicsReplication::ComputeExtrapolatedSnapshot(UWorld* World, UFusionActorComponent* Settings, UFusionPhysicsReplicationComponent* Component, FBodyInstance* BodyInstance,
	FRigidBodyState& Remote, FRigidBodyState& Result)
{

	FVector RemotePos = Remote.Position;
	FQuat RemoteRot = Remote.Quaternion;
	FVector RemoteLinVel = Remote.LinVel;
	FVector RemoteAngVel = Remote.AngVel;

	//auto world = _physScene_Chaos->GetOwningWorld();

	FVector Gravity(0, 0, World->GetGravityZ());
	
	if (Settings->GetGravityForecast() == EFusionGravityForecast::None) {
		Gravity = FVector::Zero();
	}
	else if (Settings->GetGravityForecast() == EFusionGravityForecast::Auto) {
		Gravity = BodyInstance->bEnableGravity ? Gravity : FVector::Zero();
	}

	// early out as velocities will be zero when sleeping
	// if we do this gravity is not applied
	if ([[maybe_unused]] bool bRemoteIsSleeping = Remote.Flags & ERigidBodyFlags::Sleeping) {
		if (!Remote.LinVel.IsNearlyZero()) {
			FUSION_LOG_WARN("Sleeping, but non-Zero LinearVelocity: %s", *Remote.LinVel.ToString());
		}
		if (!Remote.AngVel.IsNearlyZero()) {
			FUSION_LOG_WARN("Sleeping, but non-Zero AngularVelocity: %s", *Remote.AngVel.ToString());
		}

		Result.Position = RemotePos;
		Result.Quaternion = RemoteRot;
		Result.LinVel = RemoteLinVel;
		Result.AngVel = RemoteAngVel;
		//FUSION_LOG_WARN("Sleeping");
		return;
	}

	UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(World)->GetSubsystem<
		UFusionOnlineSubsystem>();

	double ActorNetworkTime = OnlineSubsystem->ActorNetworkTime(Component->GetOwner());
	double NetworkTime = OnlineSubsystem->NetworkTime();

	double TimeDifference = NetworkTime - ActorNetworkTime;

	float ExtrapolationDeltaTime = FMath::Clamp(TimeDifference, 0, Settings->GetMaxExtrapolationTime());

	FVector ExtrapolatedPosition = PredictPosition(ExtrapolationDeltaTime, RemotePos, RemoteLinVel, Gravity, BodyInstance->LinearDamping);

	float RemoteAngSpeedRadianPerSecond = RemoteAngVel.Size();
	FVector RemoteAngVelAxis = RemoteAngVel.GetSafeNormal();

	// Extrapolate rotation
	float DeltaRotationScale = 0.0174532924f;

	FQuat ExtrapolationDeltaRotation = FQuat(RemoteAngVelAxis,
	                                         RemoteAngSpeedRadianPerSecond * ExtrapolationDeltaTime *
	                                         DeltaRotationScale);
	FQuat ExtrapolatedRotation = ExtrapolationDeltaRotation * RemoteRot;

	// Extrapolate linear velocity
	FVector ExtrapolatedLinearVelocity = PredictVelocity(ExtrapolationDeltaTime, RemoteLinVel, Gravity,
	                                                     BodyInstance->LinearDamping);

	// Extrapolate angular velocity

	// this is predicting the ang vel will speed up over time?! 
	FVector ExtrapolatedAngVelRadiansPerSecond = RemoteAngVel + RemoteAngVelAxis * (RemoteAngSpeedRadianPerSecond * ExtrapolationDeltaTime);

	// Fill and return
	Result.Position = ExtrapolatedPosition;
	Result.Quaternion = ExtrapolatedRotation;
	Result.LinVel = ExtrapolatedLinearVelocity;
	Result.AngVel = ExtrapolatedAngVelRadiansPerSecond;
}

void FusionPhysicsReplication::PerformImmediateMove(const FRigidBodyState& Target, FBodyInstance* BodyInstance)
{
	// Immediate move
	const FQuat TargetQuat = Target.Quaternion;
	const FVector_NetQuantize100 TargetPos = Target.Position;
	constexpr bool bAutoWake = false;

	const FTransform IdealWorldTM(TargetQuat, TargetPos);
	BodyInstance->SetBodyTransform(IdealWorldTM, ETeleportType::ResetPhysics, bAutoWake);

	// Set the new velocities
	BodyInstance->SetLinearVelocity(Target.LinVel, false, bAutoWake);
	BodyInstance->SetAngularVelocityInRadians(FMath::DegreesToRadians(Target.AngVel), false, bAutoWake);

	FUSION_LOG_WARN("Immediate move");
}

FVector FusionPhysicsReplication::PredictPosition(const float DeltaTime, const FVector& InitPos, const FVector& InitVel, const FVector& Gravity, const float LinearDamping)
{ 
	if (DeltaTime < 0)
	{
		FUSION_LOG_ERROR("'deltaTime' must be positive or zero, but is: %f", DeltaTime);
		return {};
	}

	FVector Result;

	if (LinearDamping <= 0.0f)
	{
		constexpr float GravityAdjustment = 0.033f;
		Result = InitPos + InitVel * DeltaTime + Gravity * (DeltaTime * (DeltaTime + GravityAdjustment) * 0.5f);
	}
	else
	{
		constexpr float GravityAdjustment = 0.05f;
		const float p = FMath::Pow(1.0f - LinearDamping * GravityAdjustment, DeltaTime / GravityAdjustment);
		const FVector k = Gravity * (GravityAdjustment - 1.0f / LinearDamping);
		const float o = (1.0f - p) / LinearDamping;
		const FVector s = Gravity * (DeltaTime / LinearDamping);

		Result = InitPos + (InitVel + k) * o + s;
	}

	return Result;
}

FVector FusionPhysicsReplication::PredictVelocity(const float DeltaTime, const FVector& InitVel, const FVector& Gravity, const float LinearDamping)
{
	if (DeltaTime < 0)
	{
		FUSION_LOG_ERROR("'deltaTime' must be positive or zero, but is: %f", DeltaTime);
		return {};
	}

	FVector Result;

	if (LinearDamping <= 0.0f)
	{
		Result = InitVel + Gravity * DeltaTime;
	}
	else
	{
		constexpr float GravityAdjustment = 0.05f;

		const float p = FMath::Pow(1.0f - LinearDamping * GravityAdjustment, DeltaTime / GravityAdjustment);
		const FVector k = Gravity * (GravityAdjustment - 1.0f / LinearDamping);

		Result = (InitVel + k) * p - k;
	}

	return Result;
}

FVector FusionPhysicsReplication::CalculateLinearVelocity(const UFusionActorComponent* Settings, const float PhysicsFps, const FFusionReplicatedPhysicsTarget& LocalSnapshot, const FFusionReplicatedPhysicsTarget& ExtrapolatedSnapshot)
{
	const FVector PositionError = ExtrapolatedSnapshot.TargetState.Position - LocalSnapshot.TargetState.Position;
	const float PositionErrorMagnitude = PositionError.Size();

	// Velocity correction       
	float VelCorrectionMagnitude = PositionErrorMagnitude * Settings->GetLinearVelCorrectionMul();

	// Max correction magnitude, to not overshoot
	const float MaxPossibleCorrectionMag = PositionErrorMagnitude * PhysicsFps;

	VelCorrectionMagnitude = FMath::Clamp(VelCorrectionMagnitude, 0.0f, MaxPossibleCorrectionMag);

	return ExtrapolatedSnapshot.TargetState.LinVel + (VelCorrectionMagnitude * PositionError.GetSafeNormal());
}

FVector FusionPhysicsReplication::CalculateAngularVelocity(const UFusionActorComponent* Settings, const FFusionReplicatedPhysicsTarget& RemoteSnapshot, const FFusionReplicatedPhysicsTarget& ExtrapolatedSnapshot)
{
	return RemoteSnapshot.TargetState.AngVel + (ExtrapolatedSnapshot.TargetState.AngVel - RemoteSnapshot.TargetState.AngVel) * Settings->GetAngularVelCorrectionMul();
}

TTuple<bool, bool> FusionPhysicsReplication::DetectError(const UFusionActorComponent* Settings, const float PhysicsDt, const FFusionReplicatedPhysicsTarget& LocalSnapshot, const FFusionReplicatedPhysicsTarget& ExtrapolatedSnapshot, FFusionReplicatedPhysicsTarget& PhysicsTarget)
{
	const double PositionErrorMagnitude = FVector::Distance(LocalSnapshot.TargetState.Position,
	                                                  ExtrapolatedSnapshot.TargetState.Position);
	const double AngularErrorDegrees = LocalSnapshot.TargetState.Quaternion.AngularDistance(
		ExtrapolatedSnapshot.TargetState.Quaternion);

	// Detect min distance
	const bool bCorrect = PositionErrorMagnitude >= Settings->GetMinLinearDetectedError() || AngularErrorDegrees >= Settings->
		GetMinAngularDetectedError();

	if (bCorrect == false) {
		PhysicsTarget.AccumulatedErrorSeconds = 0;
		return MakeTuple(false, false);
	}

	// Check hard snap by max distance.
	// Check if error limits exist and if one of them is true.
	const bool bNotAllowedError =
		(Settings->GetMaxLinearError() > 0 && PositionErrorMagnitude > Settings->GetMaxLinearError()) ||
		(Settings->GetMaxAngularError() > 0 && AngularErrorDegrees > Settings->GetMaxAngularError());
	if (bNotAllowedError) {
		return MakeTuple(true, true);
	}

	const FVector PreviousError = PhysicsTarget.PrevPosTarget - PhysicsTarget.PrevPos;
	const FVector PositionErrorVector = ExtrapolatedSnapshot.TargetState.Position - LocalSnapshot.
		TargetState.Position;

	// Project the change in position from the previous tick onto the
	// linear error from the previous tick. This value roughly represents
	// how much correction was performed over the previous physics tick.
	const float PrevProgress = (LocalSnapshot.TargetState.Position - PhysicsTarget.PrevPos).Dot(PreviousError.GetSafeNormal());

	// Dot product the current linear error with the linear error from the
	// previous tick. This value roughly represents how little the direction
	// of the linear error state has changed, and how big the error is.

	// If the previous progress is below a threshold and the previous similarity is above another threshold
	// the correction was not good enough, so we accumulate error time for possible hard snap.
	// Otherwise, we reduce the accumulate error time because the correction worked.
	if (const float PrevSimilarity = PositionErrorVector.Dot(PreviousError); PrevProgress < Settings->GetLowCorrectionProgressThreshold() &&
		PrevSimilarity > Settings->GetHighErrorSimilarityThreshold()) {
		PhysicsTarget.AccumulatedErrorSeconds += PhysicsDt;
	}
	else {
		PhysicsTarget.AccumulatedErrorSeconds = FMath::Max(PhysicsTarget.AccumulatedErrorSeconds - PhysicsDt, 0.0f);
	}

	if (PhysicsTarget.AccumulatedErrorSeconds > Settings->GetMaxErrorTotalTime()) {
		PhysicsTarget.AccumulatedErrorSeconds = 0;
		return MakeTuple(true, true);
	}

	const bool bRemoteIsSleeping = PhysicsTarget.TargetState.Flags & ERigidBodyFlags::Sleeping;

	if (const bool bLocalIsSleeping = LocalSnapshot.TargetState.Flags & ERigidBodyFlags::Sleeping; bRemoteIsSleeping && bRemoteIsSleeping != bLocalIsSleeping) {
		PhysicsTarget.AccumulatedRemoteSleepIgnoreTime += PhysicsDt;
		if (PhysicsTarget.AccumulatedRemoteSleepIgnoreTime >= Settings->GetMaxRemoteSleepIgnoreTime()) {
			PhysicsTarget.AccumulatedRemoteSleepIgnoreTime = 0;
			return MakeTuple(true, true);
		}
	}
	else {
		PhysicsTarget.AccumulatedRemoteSleepIgnoreTime = 0;
	}

	return MakeTuple(true, false);
}


void FusionPhysicsReplication::ApplyCorrection(UFusionActorComponent* Settings, [[maybe_unused]] float PhysicsDt, float PhysicsFps, FBodyInstance* PhysicsBody, float TimeSinceLastOnCollisionEnter,
	FFusionReplicatedPhysicsTarget& LocalSnapshot, FFusionReplicatedPhysicsTarget& RemoteSnapshot, FFusionReplicatedPhysicsTarget& ExtrapolatedSnapshot)
{
	// do nothing immediately after collisions
	if (TimeSinceLastOnCollisionEnter < Settings->GetImpactStartCorrectionTime()) {
		//FUSION_LOG("do nothing immediately after collisions");
		return;
	}

	float Alpha = FMath::Clamp(
		(TimeSinceLastOnCollisionEnter - Settings->GetImpactStartCorrectionTime()) / (Settings->
			GetImpactCorrectionTimeComplete() - Settings->GetImpactStartCorrectionTime()), 0, 1);

	//FUSION_LOG("Alpha: %f", alpha);

	if (Settings->GetErrorCorrectionType() == EFusionPhysicsCorrection::Velocity ||
		Settings->GetErrorCorrectionType() == EFusionPhysicsCorrection::PositionRotation) {

		FVector NewPosition = LocalSnapshot.TargetState.Position;

		if (Settings->GetErrorCorrectionType() == EFusionPhysicsCorrection::PositionRotation) {
			FVector_NetQuantize100 DesiredPosition = FMath::Lerp(LocalSnapshot.TargetState.Position,
			                                                     ExtrapolatedSnapshot.TargetState.Position,
			                                                     Settings->GetPositionCorrectionLerp());
			NewPosition = FMath::Lerp(LocalSnapshot.TargetState.Position, DesiredPosition, Alpha);
		}
		else {
			// we do not change the local position
			//_physicsBody.Position = _localKSnap.WorldPosition;
		}

		FQuat DesiredRotation = FQuat::Slerp(LocalSnapshot.TargetState.Quaternion,
		                                                       ExtrapolatedSnapshot.TargetState.Quaternion,
		                                                       Settings->GetRotationCorrectionLerp());
		FQuat NewRotation =
			FQuat::Slerp(LocalSnapshot.TargetState.Quaternion, DesiredRotation, Alpha);

		FVector DesiredVelocity = CalculateLinearVelocity(Settings, PhysicsFps, LocalSnapshot, ExtrapolatedSnapshot);
		FVector NewVelocity = FMath::Lerp(LocalSnapshot.TargetState.LinVel, DesiredVelocity, Alpha);

		FVector DesiredAngular = CalculateAngularVelocity(Settings, RemoteSnapshot, ExtrapolatedSnapshot);
		FVector NewAngularVelocity = FMath::Lerp(LocalSnapshot.TargetState.AngVel, DesiredAngular,
		                                                           Alpha);

		constexpr bool bAutoWake = false;
		const FTransform IdealWorldTM(NewRotation, NewPosition);

		PhysicsBody->SetBodyTransform(IdealWorldTM, ETeleportType::ResetPhysics, bAutoWake);
		PhysicsBody->SetLinearVelocity(NewVelocity, false, bAutoWake);
		PhysicsBody->SetAngularVelocityInRadians(FMath::DegreesToRadians(NewAngularVelocity), false, bAutoWake);

	}
	else if (Settings->GetErrorCorrectionType() == EFusionPhysicsCorrection::SpringDamping)
	{
		FQuat NewRotation = FQuat::Slerp(LocalSnapshot.TargetState.Quaternion, ExtrapolatedSnapshot.TargetState.Quaternion, Settings->GetRotationCorrectionLerp());
		FVector_NetQuantize100 NewLinearVelocity = ExtrapolatedSnapshot.TargetState.LinVel;
		[[maybe_unused]] FVector NewAngularVelocity = CalculateAngularVelocity(
			Settings, RemoteSnapshot, ExtrapolatedSnapshot);
		FVector PositionError = ExtrapolatedSnapshot.TargetState.Position - LocalSnapshot.TargetState.
			Position;

		FVector DistanceForce = PositionError * Settings->GetSpring();
		FVector DamperForce;
		if (!PositionError.IsNearlyZero()) {
			FVector PreviousError = RemoteSnapshot.PrevPosTarget - RemoteSnapshot.PrevPos;
			FVector PositionVelocity = (PositionError - PreviousError) * PhysicsFps;
			DamperForce = PositionVelocity * Settings->GetDamper();
		}

		constexpr bool bAutoWake = false;
		const FTransform IdealWorldTM(NewRotation, LocalSnapshot.TargetState.Position);

		PhysicsBody->SetBodyTransform(IdealWorldTM, ETeleportType::ResetPhysics, bAutoWake);
		PhysicsBody->SetLinearVelocity(NewLinearVelocity, false, bAutoWake);

		PhysicsBody->AddForce((DistanceForce + DamperForce) * PhysicsBody->GetBodyMass());
	}
}


