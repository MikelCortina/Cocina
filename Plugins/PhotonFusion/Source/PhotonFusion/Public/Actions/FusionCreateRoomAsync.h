// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRoomActionBase.h"
#include "UObject/Object.h"
#include "FusionCreateRoomAsync.generated.h"

UCLASS()
class PHOTONFUSION_API UFusionCreateRoomAsync : public UFusionRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UFusionCreateRoomAsync* CreateRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject);

	virtual void Activate() override;
};
