// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionRoomActionBase.h"
#include "FusionJoinRoomAsync.generated.h"


UCLASS()
class PHOTONFUSION_API UFusionJoinRoomAsync : public UFusionRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UFusionJoinRoomAsync* JoinRoom(const FString RoomName, UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UFusionJoinRoomAsync* JoinRandomRoom(UObject* WorldContextObject);

	virtual void Activate() override;

private:
	FString RoomName;
	bool bRandomRoom {false};
};
