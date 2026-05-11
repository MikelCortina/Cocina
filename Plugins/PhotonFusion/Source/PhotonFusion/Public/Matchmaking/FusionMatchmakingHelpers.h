// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionOnlineSubsystem.h"
#include "Fusion/CreateRoomOptions.h"

class UObject;
class UFusionRealtimeClient;

class PHOTONFUSION_API FusionMatchmakingHelpers
{
public:
	static bool StartFusion(TObjectPtr<UObject> WorldContextObjectBase, TObjectPtr<UFusionRealtimeClient>);
	
	static PhotonMatchmaking::CreateRoomOptions CreatePhotonRoomOptions(const FFusionRoomOptions& InputRoomOptions);
};
