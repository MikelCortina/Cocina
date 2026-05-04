// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/LeaveRoomAsync.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "FusionUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"

ULeaveRoomAsync* ULeaveRoomAsync::LeaveRoom(TSoftObjectPtr<UWorld> InLobbyWorld, UObject* WorldContextObject)
{
	ULeaveRoomAsync* Action = NewObject<ULeaveRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->LobbyWorld = InLobbyWorld;

	return Action;
}

void ULeaveRoomAsync::Activate()
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase);
	if (!GameInstance)
	{
		FUSION_LOG_ERROR("Invalid GameInstance during LeaveRoom");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem || !OnlineSubsystem->RealtimeClient || !OnlineSubsystem->RealtimeClient->IsValid())
	{
		FUSION_LOG_ERROR("Not connected during LeaveRoom");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	if (!OnlineSubsystem->RealtimeClient->IsInRoom())
	{
		FUSION_LOG_ERROR("Not in a room during LeaveRoom");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	// Stop Fusion if active
	if (OnlineSubsystem->GFusionClient)
	{
		OnlineSubsystem->StopFusionSession();
	}

	OnlineSubsystem->RealtimeClient->PendingVoidTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->LeaveRoom());

	constexpr float CheckInterval = 0.1f;
	TimerState.ChecksDone = 0;

	FTimerManager& TimerManager = WorldContextObjectBase->GetWorld()->GetTimerManager();
	TimerManager.SetTimer(TimerState.Handle, this, &ULeaveRoomAsync::WaitLeaveRoom, CheckInterval, true);
}

void ULeaveRoomAsync::WaitLeaveRoom()
{
	FTimerManager& TimerManager = WorldContextObjectBase->GetWorld()->GetTimerManager();
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase);
	if (!GameInstance)
	{
		FUSION_LOG_ERROR("Invalid GameInstance during WaitLeaveRoom");
		TimerManager.ClearTimer(TimerState.Handle);
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	const UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();

	if (!OnlineSubsystem || !OnlineSubsystem->RealtimeClient || !OnlineSubsystem->RealtimeClient->IsConnected())
	{
		FUSION_LOG("Lost connection while waiting for leave room");
		TimerManager.ClearTimer(TimerState.Handle);
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	if (!OnlineSubsystem->RealtimeClient->IsInRoom())
	{
		FUSION_LOG("Successfully left room");
		TimerManager.ClearTimer(TimerState.Handle);

		if (!LobbyWorld.IsNull())
		{
			const FString MapPath = LobbyWorld.ToSoftObjectPath().GetLongPackageName();
			if (!MapPath.IsEmpty())
			{
				UGameplayStatics::OpenLevel(WorldContextObjectBase, FName(*MapPath));
			}
		}

		if (OnSuccess.IsBound())
			OnSuccess.Broadcast();

		DestroyAction();
		return;
	}

	TimerState.ChecksDone++;

	if (constexpr int32 MaxChecks = 50; TimerState.ChecksDone >= MaxChecks)
	{
		FUSION_LOG("Leave room timed out");
		TimerManager.ClearTimer(TimerState.Handle);
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::TimeOut);
		DestroyAction();
	}
}
