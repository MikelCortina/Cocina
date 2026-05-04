// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "FusionGlobals.h"
#include "CoreMinimal.h"
#include "FusionOnlineSubsystem.h"
#include "Fusion/RealtimeClient.h"

#include "FusionRealtimeClient.generated.h"

UCLASS(BlueprintType)
class PHOTONFUSION_API UFusionRealtimeClient : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Photon")
	static UFusionRealtimeClient* CreateRealtimeClient(FFusionConnectOptions Options);
	static UFusionRealtimeClient* FromExisting(PhotonMatchmaking::RealtimeClient* InClient);

	// Access
	PhotonMatchmaking::RealtimeClient* GetClient() const;
	bool IsValid() const;
	bool OwnsClient() const;

	// Status (delegates to RealtimeClient)
	UFUNCTION(BlueprintPure, Category = "Photon")
	bool IsConnected() const;

	UFUNCTION(BlueprintPure, Category = "Photon")
	bool IsInRoom() const;

	UFUNCTION(BlueprintPure, Category = "Photon")
	EFusionStatus GetStatus() const;

	UFUNCTION(BlueprintPure, Category = "Photon")
	int32 GetLocalPlayerId() const;

	UFUNCTION(BlueprintPure, Category = "Photon")
	bool IsMasterClient() const;

	UFUNCTION(BlueprintPure, Category = "Photon")
	int32 PlayerCount() const;

	UFUNCTION(BlueprintPure, Category = "Photon")
	bool GetRoomInfo(FString& Name, int32& Players) const;

	// Service + task management (called by subsystem's WorldTick)
	void Service();
	void CleanupCompletedTasks();

	// Task storage (moved from subsystem)
	std::vector<PhotonMatchmaking::Task<PhotonMatchmaking::Result<void>>> PendingVoidTasks;
	std::vector<PhotonMatchmaking::Task<PhotonMatchmaking::Result<PhotonMatchmaking::MutableRoomView>>> PendingRoomTasks;

	virtual void BeginDestroy() override;

private:
	PhotonMatchmaking::RealtimeClient* Client = nullptr;
	bool bOwnsClient = false;
};
