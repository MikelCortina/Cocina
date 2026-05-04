// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionOnlineSubsystem.h"
#include "PhotonRoomActionBase.h"
#include "UObject/Object.h"
#include "CreateRoomAsync.generated.h"

UCLASS()
class PHOTONFUSION_API UCreateRoomAsync : public UPhotonRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UCreateRoomAsync* CreateRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject);

	virtual void Activate() override;
};
