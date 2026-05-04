// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PhotonRoomActionBase.h"
#include "ConnectAndJoinRoomAsync.generated.h"

UCLASS()
class PHOTONFUSION_API UConnectAndJoinRoomAsync : public UPhotonRoomActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"), Category = "Photon")
	static UConnectAndJoinRoomAsync* ConnectAndJoinRoom(const FFusionConnectOptions ConnectOptions, const FFusionRoomOptions RoomOptions, UObject* WorldContextObject);

	UFUNCTION()
	void OnConnectToPhotonSuccess();

	UFUNCTION()
	void OnAsyncFailure(EActionFailureCodes FailureCode);

	UFUNCTION()
	void OnJoinOrCreateRoomSuccess();

	virtual void Activate() override;

private:

	UPROPERTY()
	FFusionConnectOptions ConnectOptions;

	UPROPERTY()
	TObjectPtr<UConnectToPhotonAsync> ConnectPhotonAsync = nullptr;
	
	UPROPERTY()
	TObjectPtr<UJoinOrCreateRoomAsync> JoinOrCreateRoomAsync = nullptr;
};
