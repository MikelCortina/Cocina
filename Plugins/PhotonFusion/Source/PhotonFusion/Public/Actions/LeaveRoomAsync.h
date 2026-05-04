// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FFusionTimerState.h"
#include "PhotonRoomActionBase.h"
#include "LeaveRoomAsync.generated.h"

UCLASS()
class PHOTONFUSION_API ULeaveRoomAsync : public UPhotonRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static ULeaveRoomAsync* LeaveRoom(TSoftObjectPtr<UWorld> LobbyWorld, UObject* WorldContextObject);

	virtual void Activate() override;

private:
	void WaitLeaveRoom();

	UPROPERTY()
	TSoftObjectPtr<UWorld> LobbyWorld;

	FFusionTimerState TimerState;
};
