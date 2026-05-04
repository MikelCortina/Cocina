// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FFusionTimerState.h"
#include "PhotonRoomActionBase.h"
#include "Fusion/RegionInfo.h"
#include "Fusion/Result.h"
#include "Fusion/Task.h"
#include <optional>
#include <vector>
#include "ConnectToPhotonAsync.generated.h"

UCLASS(BlueprintType)
class PHOTONFUSION_API UConnectToPhotonAsync : public UPhotonRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UConnectToPhotonAsync* ConnectToPhoton(const FFusionConnectOptions ConnectOptions, UObject* WorldContextObject);

	virtual void Activate() override;
	
private:
	void TryConnect();

	void WaitConnect();

	UPROPERTY()
	FFusionConnectOptions ConnectOptions;
	
	UPROPERTY()
	FFusionTimerState TimerState;

	enum class EConnectPhase : uint8
	{
		WaitingForRegions,
		WaitingForConnect
	};

	EConnectPhase ConnectPhase = EConnectPhase::WaitingForConnect;

	std::optional<PhotonMatchmaking::Task<PhotonMatchmaking::Result<std::vector<PhotonMatchmaking::RegionInfo>>>> PendingRegionsTask;
};
