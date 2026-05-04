// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Physics/FusionPhysicsReplicationFactory.h"
#include "Physics/FusionPhysicsReplication.h"

TUniquePtr<IPhysicsReplication> FusionPhysicsReplicationFactory::CreatePhysicsReplication(FPhysScene* OwningPhysScene)
{
	return MakeUnique<FusionPhysicsReplication>(OwningPhysScene);
}
