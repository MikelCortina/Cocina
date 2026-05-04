// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "FFusionTimerState.generated.h"

USTRUCT()
struct FFusionTimerState
{
	GENERATED_BODY()

	int32 ChecksDone = 0;
	FTimerHandle Handle;
};
