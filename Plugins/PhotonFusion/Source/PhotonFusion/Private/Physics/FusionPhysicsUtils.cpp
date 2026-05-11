// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Physics/FusionPhysicsUtils.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Physics/FusionPhysicsReplicationFactory.h"
#include "Physics/FusionPhysicsReplication.h"
#include "Physics/FusionPhysicsExtensions.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/FusionPhysicsReplicationComponent.h"


bool FusionPhysicsUtils::GetPhysicsBodyState(const class UWorld* World, class UPrimitiveComponent* Component, struct FRigidBodyState& OutState, int& OutServerFrame)
{
	if (!Component)
		return false;
	
	//int ServerFrame = -1; 
	if ([[maybe_unused]] FPhysScene_Chaos* Scene = World->GetPhysicsScene())
	{
		//Temp workaround: for now where we have to create stack object of extensions since they include some funky headers that does not make friends with the linker.
		//PhysicsExtensions utils;
		//ServerFrame = utils.GetSolverFrame(Scene);
	}
	
	// if (ServerFrame <= 0)
	// 	return false;
	
	if (Component->GetRigidBodyState(OutState))
	{
		//outServerFrame = ServerFrame;

		OutServerFrame = 0;
		
		return true;
	}

	return false;
}

FusionPhysicsReplication* FusionPhysicsUtils::CreateReplicationSetup(const UWorld* World)
{
	if (!World)
		return nullptr;
	
	if (FPhysScene_Chaos* Scene = World->GetPhysicsScene())
	{
		const TSharedPtr<IPhysicsReplicationFactory> ReplicationFactory =  MakeShared<FusionPhysicsReplicationFactory>();
		FPhysScene_Chaos::PhysicsReplicationFactory = ReplicationFactory;
		IPhysicsReplication* Replication = Scene->CreatePhysicsReplication();
		return static_cast<FusionPhysicsReplication*>(Replication);
	}

	return nullptr;
}

void FusionPhysicsUtils::ResetReplication(const UWorld* World)
{
	if (!World)
		return;

	if (FPhysScene_Chaos* Scene = World->GetPhysicsScene())
	{
		const TSharedPtr<IPhysicsReplicationFactory> ReplicationFactory =  MakeShared<FusionPhysicsReplicationFactory>();
		FPhysScene_Chaos::PhysicsReplicationFactory = nullptr;
		//Creates default replication object.
		Scene->CreatePhysicsReplication();
	}
}

FusionPhysicsReplication* FusionPhysicsUtils::GetReplication(const UWorld* World)
{
	if (!World)
		return nullptr;
	
	if (FPhysScene_Chaos* Scene = World->GetPhysicsScene())
	{
		if (IPhysicsReplication* Replication = Scene->GetPhysicsReplication())
		{
			if (FPhysScene_Chaos::PhysicsReplicationFactory.IsValid())
			{
				return static_cast<FusionPhysicsReplication*>(Replication);
			}
		}
	}

	return nullptr;
}

void FusionPhysicsUtils::InitiatePhysicsReplication(UFusionPhysicsReplicationComponent* Component)
{
	const AActor* Owner = Component->GetOwner();
	if (!Owner)
		return;
	
	if (!Component->Primitive)
	{
		const USceneComponent* RootComponent = Owner->GetRootComponent();
		
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Owner->GetComponents(PrimitiveComponents);

		TArray<UFusionPhysicsReplicationComponent*> ReplicationComponents;
		Owner->GetComponents(ReplicationComponents);
	
		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (ReplicationComponents.FindByPredicate([&Primitive] (const UFusionPhysicsReplicationComponent* Comp)
			{
				if (Comp->Primitive)
				{
					//Some other replication component is already referencing this physics body/primitive?
					return Comp->Primitive == Primitive;
				}
				return false;
			}))
			{
				continue;
			}

			if (RootComponent == Primitive)
			{
				Component->Primitive = Primitive;
				break;
			}
			
			Component->Primitive = Primitive;
		}
	}
}

FRigidBodyState FusionPhysicsUtils::GetRigidBodyState(const struct FFusionBodyState& State)
{
	FRigidBodyState Result;
	Result.Position = State.Position;
	Result.Quaternion = State.Quaternion;
	Result.LinVel = State.LinVel;
	Result.AngVel = State.AngVel;
	Result.Flags = State.Flags;

	return Result;
}


