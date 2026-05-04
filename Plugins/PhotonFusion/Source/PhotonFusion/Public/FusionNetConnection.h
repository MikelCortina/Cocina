// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Engine/NetConnection.h"
#include "Misc/EngineVersionComparison.h"

#include "FusionNetConnection.generated.h"

UCLASS(transient, config = Engine, MinimalAPI)
class UFusionNetConnection : public UNetConnection
{
	GENERATED_BODY()

public:

	virtual FString LowLevelGetRemoteAddress(bool bAppendPort = false) override;

#if UE_VERSION_OLDER_THAN(5, 6, 0)
	virtual int32 IsNetReady(bool Saturate) override { return 1; }
#else
	virtual bool IsNetReady() const override { return true; }
#endif

};
