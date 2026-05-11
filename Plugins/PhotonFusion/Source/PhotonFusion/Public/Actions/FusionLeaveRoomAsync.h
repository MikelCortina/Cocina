// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionTimerState.h"
#include "FusionRoomActionBase.h"
#include "FusionLeaveRoomAsync.generated.h"

UCLASS()
class PHOTONFUSION_API UFusionLeaveRoomAsync : public UFusionRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UFusionLeaveRoomAsync* LeaveRoom(TSoftObjectPtr<UWorld> LobbyWorld, UObject* WorldContextObject);

	virtual void Activate() override;

private:
	void WaitLeaveRoom();

	UPROPERTY()
	TSoftObjectPtr<UWorld> LobbyWorld;

	FusionTimerState TimerState;
};
