// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionTimerState.h"
#include "FusionRoomActionBase.h"
#include "Fusion/ErrorCode.h"
#include "Fusion/RegionInfo.h"
#include <optional>
#include <vector>
#include "FusionConnectToPhotonAsync.generated.h"

UCLASS(BlueprintType)
class PHOTONFUSION_API UFusionConnectToPhotonAsync : public UFusionRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UFusionConnectToPhotonAsync* ConnectToPhoton(const FFusionConnectOptions ConnectOptions, UObject* WorldContextObject);

	virtual void Activate() override;
	
private:
	void TryConnect();

	void WaitConnect();

	UPROPERTY()
	FFusionConnectOptions ConnectOptions;
	
	UPROPERTY()
	FusionTimerState TimerState;

	enum class EConnectPhase : uint8
	{
		WaitingForRegions,
		WaitingForConnect
	};

	EConnectPhase ConnectPhase = EConnectPhase::WaitingForConnect;

	std::optional<PhotonMatchmaking::Task<PhotonMatchmaking::Result<std::vector<PhotonMatchmaking::RegionInfo>>>> PendingRegionsTask;
};
