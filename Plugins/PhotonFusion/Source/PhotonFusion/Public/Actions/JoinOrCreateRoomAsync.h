// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionOnlineSubsystem.h"
#include "PhotonRoomActionBase.h"
#include "JoinOrCreateRoomAsync.generated.h"

UCLASS()
class PHOTONFUSION_API UJoinOrCreateRoomAsync : public UPhotonRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UJoinOrCreateRoomAsync* JoinOrCreateRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UJoinOrCreateRoomAsync* JoinOrCreateRandomRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject);

	virtual void Activate() override;

private:
	bool bRandom{false};
};
