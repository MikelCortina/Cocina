// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FFusionTimerState.h"
#include "ConnectToPhotonAsync.h"
#include "PhotonRoomActionBase.h"
#include "DisconnectFromPhotonAsync.generated.h"

UCLASS()
class PHOTONFUSION_API UDisconnectFromPhotonAsync : public UPhotonRoomActionBase
{
	GENERATED_BODY()


public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UDisconnectFromPhotonAsync* DisconnectFromPhoton(UObject* WorldContextObject);

	virtual void Activate() override;

private:
	void TryDisconnect();
	void WaitDisconnect();

	UPROPERTY()
	FFusionTimerState TimerState;
};
