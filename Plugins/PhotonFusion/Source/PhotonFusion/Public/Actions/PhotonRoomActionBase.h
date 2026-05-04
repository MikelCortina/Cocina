// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "FusionGlobals.h"
#include "CoreMinimal.h"
#include "Fusion/SubscriptionBag.h"
#include "FusionOnlineSubsystem.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "PhotonRoomActionBase.generated.h"

UENUM(BlueprintType)
enum class EActionFailureCodes : uint8
{
	Unknown = 0 UMETA(DisplayName = "Unknown Error"),
	Connecting = 1 UMETA(DisplayName = "Connecting"),
	Error = 2 UMETA(DisplayName = "Error"),
	Disconnected = 3 UMETA(DisplayName = "Disconnected"),
	JoiningRoom = 5 UMETA(DisplayName = "Joining Room"),
	InRoom = 6 UMETA(DisplayName = "In Room"),
	TimeOut = 7 UMETA(DisplayName = "Time Out"),
	InvalidRegion = 8 UMETA(DisplayName = "Invalid Region"),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPhotonSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPhotonFailure, EActionFailureCodes, Code);

UCLASS(Abstract, NotBlueprintable)
class PHOTONFUSION_API UPhotonRoomActionBase : public UBlueprintAsyncActionBase
{
	
	GENERATED_BODY()
	
	int32 ChecksDone{0};
	FTimerHandle Handle{};
	
public:
	UPhotonRoomActionBase();
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	virtual void Activate() override;

	virtual void BeginDestroy() override;

	UPROPERTY(BlueprintAssignable)
	FOnPhotonFailure OnFailure;
	
	UPROPERTY(BlueprintAssignable)
	FOnPhotonSuccess OnSuccess;

	bool bFailInstantly{false};

protected:
	void WaitForRoomJoined();
	void OnJoinedRoom();
	void OnWorldLoaded();

	void TimerCallback();
	
	bool bHasJoinedRoom {false};

	bool bHasRemovedFromRoot {false};

	void DestroyAction();

	FDelegateHandle CleanupHandle;

	PhotonCommon::SubscriptionBag RoomJoinedSubscriptions;

	UPROPERTY()
	TObjectPtr<UObject> WorldContextObjectBase{nullptr};

	FFusionRoomOptions RoomOptions;
};
