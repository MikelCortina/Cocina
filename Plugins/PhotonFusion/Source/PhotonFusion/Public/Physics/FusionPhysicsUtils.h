// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

class FusionPhysicsUtils
{
public:
	static bool GetPhysicsBodyState(const class UWorld* World, class UPrimitiveComponent* Component, struct FRigidBodyState& OutState, int& OutServerFrame);

	static class FusionPhysicsReplication* CreateReplicationSetup(const UWorld* World);

	static void ResetReplication(const UWorld* World);

	static FusionPhysicsReplication* GetReplication(const UWorld* World);
	
	static void InitiatePhysicsReplication(class UFusionPhysicsReplicationComponent* Component);

	static FRigidBodyState GetRigidBodyState(const struct FFusionBodyState& State);
};
