// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PhotonRoomActionBase.h"
#include "JoinRoomAsync.generated.h"


UCLASS()
class PHOTONFUSION_API UJoinRoomAsync : public UPhotonRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UJoinRoomAsync* JoinRoom(const FString RoomName, UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UJoinRoomAsync* JoinRandomRoom(UObject* WorldContextObject);

	virtual void Activate() override;

private:
	FString RoomName;
	bool bRandomRoom {false};
};
