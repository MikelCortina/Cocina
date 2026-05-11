// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionRoomActionBase.h"
#include "FusionConnectAndJoinRoomAsync.generated.h"

UCLASS()
class PHOTONFUSION_API UFusionConnectAndJoinRoomAsync : public UFusionRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UFusionConnectAndJoinRoomAsync* ConnectAndJoinRoom(const FFusionConnectOptions ConnectOptions, const FFusionRoomOptions RoomOptions, UObject* WorldContextObject);

	UFUNCTION()
	void OnConnectToPhotonSuccess();

	UFUNCTION()
	void OnAsyncFailure(EFusionActionFailureCodes FailureCode);

	UFUNCTION()
	void OnJoinOrCreateRoomSuccess();

	virtual void Activate() override;

private:

	UPROPERTY()
	FFusionConnectOptions ConnectOptions;

	UPROPERTY()
	TObjectPtr<UFusionConnectToPhotonAsync> ConnectPhotonAsync = nullptr;
	
	UPROPERTY()
	TObjectPtr<UFusionJoinOrCreateRoomAsync> JoinOrCreateRoomAsync = nullptr;
};
