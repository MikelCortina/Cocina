// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRoomActionBase.h"
#include "FusionJoinOrCreateRoomAsync.generated.h"

UCLASS()
class PHOTONFUSION_API UFusionJoinOrCreateRoomAsync : public UFusionRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UFusionJoinOrCreateRoomAsync* JoinOrCreateRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UFusionJoinOrCreateRoomAsync* JoinOrCreateRandomRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject);

	virtual void Activate() override;

private:
	bool bRandom{false};
};
