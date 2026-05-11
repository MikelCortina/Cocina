// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionTimerState.h"
#include "FusionConnectToPhotonAsync.h"
#include "FusionRoomActionBase.h"
#include "FusionDisconnectFromPhotonAsync.generated.h"

UCLASS()
class PHOTONFUSION_API UFusionDisconnectFromPhotonAsync : public UFusionRoomActionBase
{
	GENERATED_BODY()


public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UFusionDisconnectFromPhotonAsync* DisconnectFromPhoton(UObject* WorldContextObject);

	virtual void Activate() override;

private:
	void TryDisconnect();
	void WaitDisconnect();

	UPROPERTY()
	FusionTimerState TimerState;
};
