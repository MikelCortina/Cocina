// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Physics/PhysicsInterfaceUtils.h"

class FusionPhysicsReplicationFactory : public IPhysicsReplicationFactory
{
public:
	PHOTONFUSION_API virtual TUniquePtr<IPhysicsReplication> CreatePhysicsReplication(FPhysScene* OwningPhysScene) override;
};
