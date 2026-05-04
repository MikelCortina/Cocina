// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/PhotonRoomActionBase.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "FusionUtils.h"
#include "PhotonOnlineSubsystemSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "TimerManager.h"
#include "Types/TypeLookup.h"

UPhotonRoomActionBase::UPhotonRoomActionBase()
{
	if ( HasAnyFlags(RF_ClassDefaultObject) == false )
	{
		bHasRemovedFromRoot = false;
		AddToRoot();

		CleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UPhotonRoomActionBase::OnWorldCleanup);
	}
}

void UPhotonRoomActionBase::OnWorldCleanup([[maybe_unused]] UWorld* World, [[maybe_unused]] bool bSessionEnded, [[maybe_unused]] bool bCleanupResources)
{
	if (CleanupHandle.IsValid())
		FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);

	DestroyAction();
}

void UPhotonRoomActionBase::Activate()
{
}

void UPhotonRoomActionBase::BeginDestroy()
{
	DestroyAction();
	Super::BeginDestroy();
}

void UPhotonRoomActionBase::WaitForRoomJoined()
{
	Handle = FTimerHandle{};
	ChecksDone = 0;
	bHasJoinedRoom = false;

	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase);
	if (!GameInstance)
	{
		FUSION_LOG_ERROR("Invalid GameInstance during WaitForRoomJoined");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::TimeOut);
		DestroyAction();
		return;
	}

	const UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem || !OnlineSubsystem->RealtimeClient || !OnlineSubsystem->RealtimeClient->IsValid())
	{
		FUSION_LOG_ERROR("FusionOnlineSubsystem or RealtimeClient not available during WaitForRoomJoined");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::TimeOut);
		DestroyAction();
		return;
	}

	RoomJoinedSubscriptions += OnlineSubsystem->RealtimeClient->GetClient()->OnRoomJoined.Subscribe([this] { this->OnJoinedRoom(); });

	WorldContextObjectBase->GetWorld()->GetTimerManager().SetTimer(Handle, this, &UPhotonRoomActionBase::TimerCallback, 0.1f, true);

	FUSION_LOG("Waiting for room join");
}

void UPhotonRoomActionBase::OnJoinedRoom()
{
	bHasJoinedRoom = true;
}

void UPhotonRoomActionBase::OnWorldLoaded()
{
}

void UPhotonRoomActionBase::TimerCallback()
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase);
	if (!GameInstance)
	{
		FUSION_LOG_ERROR("Invalid GameInstance during TimerCallback");
		WorldContextObjectBase->GetWorld()->GetTimerManager().ClearTimer(Handle);
		RoomJoinedSubscriptions.UnsubscribeAll();
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	const UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem)
	{
		FUSION_LOG_ERROR("FusionOnlineSubsystem not available during TimerCallback");
		WorldContextObjectBase->GetWorld()->GetTimerManager().ClearTimer(Handle);
		RoomJoinedSubscriptions.UnsubscribeAll();
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	if (!OnlineSubsystem->IsConnected())
	{
		FUSION_LOG("Lost connection while waiting for room join");

		RoomJoinedSubscriptions.UnsubscribeAll();

		WorldContextObjectBase->GetWorld()->GetTimerManager().ClearTimer(Handle);

		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);

		DestroyAction();

		return;
	}

	if (constexpr int32 MaxChecks = 50; ++ChecksDone >= MaxChecks)
	{
		FUSION_LOG("Room Join timed out");

		RoomJoinedSubscriptions.UnsubscribeAll();

		WorldContextObjectBase->GetWorld()->GetTimerManager().ClearTimer(Handle);

		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::TimeOut);

		DestroyAction();

		return;
	}

	if (bHasJoinedRoom)
	{
		FUSION_LOG("Room Successfully joined");

		RoomJoinedSubscriptions.UnsubscribeAll();

		WorldContextObjectBase->GetWorld()->GetTimerManager().ClearTimer(Handle);

		OnSuccess.Broadcast();

		DestroyAction();
	}
}

void UPhotonRoomActionBase::DestroyAction()
{
	if (!bHasRemovedFromRoot)
		RemoveFromRoot();

	bHasRemovedFromRoot = true;
}
