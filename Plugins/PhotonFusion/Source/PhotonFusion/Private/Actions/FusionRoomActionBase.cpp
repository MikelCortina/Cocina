// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/FusionRoomActionBase.h"

#include <Fusion/FusionRoomProperties.h>

#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "FusionUtils.h"
#include "FusionOnlineSubsystemSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "TimerManager.h"
#include "Matchmaking/FusionMatchmakingHelpers.h"

UFusionRoomActionBase::UFusionRoomActionBase()
{
	if ( HasAnyFlags(RF_ClassDefaultObject) == false )
	{
		bHasRemovedFromRoot = false;
		AddToRoot();

		CleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UFusionRoomActionBase::OnWorldCleanup);
	}
}

void UFusionRoomActionBase::OnWorldCleanup([[maybe_unused]] UWorld* World, [[maybe_unused]] bool bSessionEnded, [[maybe_unused]] bool bCleanupResources)
{
	// FWorldDelegates::OnWorldCleanup is global — it fires for every PIE world.
	if (!WorldContextObjectBase || World != WorldContextObjectBase->GetWorld())
	{
		return;
	}

	if (CleanupHandle.IsValid())
		FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);

	DestroyAction();
}

void UFusionRoomActionBase::Activate()
{
}

void UFusionRoomActionBase::BeginDestroy()
{
	DestroyAction();
	Super::BeginDestroy();
}

void UFusionRoomActionBase::WaitForRoomJoined()
{
	Handle = FTimerHandle{};
	ChecksDone = 0;
	bHasJoinedRoom = false;

	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase);
	if (!GameInstance)
	{
		FUSION_LOG_ERROR("Invalid GameInstance during WaitForRoomJoined");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::TimeOut);
		DestroyAction();
		return;
	}

	const UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem || !OnlineSubsystem->RealtimeClient || !OnlineSubsystem->RealtimeClient->IsValid())
	{
		FUSION_LOG_ERROR("FusionOnlineSubsystem or RealtimeClient not available during WaitForRoomJoined");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::TimeOut);
		DestroyAction();
		return;
	}

	RoomJoinedSubscriptions += OnlineSubsystem->RealtimeClient->GetClient()->OnRoomJoined.Subscribe([this] { this->OnJoinedRoom(); });

	WorldContextObjectBase->GetWorld()->GetTimerManager().SetTimer(Handle, this, &UFusionRoomActionBase::TimerCallback, 0.1f, true);

	FUSION_LOG("Waiting for room join");
}

void UFusionRoomActionBase::OnJoinedRoom()
{
	bHasJoinedRoom = true;
}

void UFusionRoomActionBase::OnWorldLoaded()
{
}

void UFusionRoomActionBase::TimerCallback()
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase);
	if (!GameInstance)
	{
		FUSION_LOG_ERROR("Invalid GameInstance during TimerCallback");
		WorldContextObjectBase->GetWorld()->GetTimerManager().ClearTimer(Handle);
		RoomJoinedSubscriptions.UnsubscribeAll();
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::Disconnected);
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
			OnFailure.Broadcast(EFusionActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	if (!OnlineSubsystem->IsConnected())
	{
		FUSION_LOG("Lost connection while waiting for room join");

		RoomJoinedSubscriptions.UnsubscribeAll();

		WorldContextObjectBase->GetWorld()->GetTimerManager().ClearTimer(Handle);

		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::Disconnected);

		DestroyAction();

		return;
	}

	if (constexpr int32 MaxChecks = 300; ++ChecksDone >= MaxChecks)
	{
		FUSION_LOG("Room Join timed out");

		RoomJoinedSubscriptions.UnsubscribeAll();

		WorldContextObjectBase->GetWorld()->GetTimerManager().ClearTimer(Handle);

		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::TimeOut);

		DestroyAction();

		return;
	}

	if (bHasJoinedRoom)
	{
		FUSION_LOG("Room Successfully joined");

		RoomJoinedSubscriptions.UnsubscribeAll();

		WorldContextObjectBase->GetWorld()->GetTimerManager().ClearTimer(Handle);

		if (FusionMatchmakingHelpers::StartFusion(WorldContextObjectBase, OnlineSubsystem->RealtimeClient))
		{
			if (OnSuccess.IsBound())
				OnSuccess.Broadcast();
			
			DestroyAction();
		}
		else
		{
			if (OnFailure.IsBound())
				OnFailure.Broadcast(EFusionActionFailureCodes::TimeOut);

			DestroyAction();
		}
	}
}

void UFusionRoomActionBase::DestroyAction()
{
	if (!bHasRemovedFromRoot)
		RemoveFromRoot();

	bHasRemovedFromRoot = true;
}
