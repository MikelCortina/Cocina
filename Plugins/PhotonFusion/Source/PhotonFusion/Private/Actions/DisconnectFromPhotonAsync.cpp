// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/DisconnectFromPhotonAsync.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "FusionUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h" 
#include "Engine/World.h"

UDisconnectFromPhotonAsync* UDisconnectFromPhotonAsync::DisconnectFromPhoton(UObject* WorldContextObject)
{
	UDisconnectFromPhotonAsync* Action = NewObject<UDisconnectFromPhotonAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	
	return Action;
}

void UDisconnectFromPhotonAsync::Activate()
{
	TryDisconnect();
}

void UDisconnectFromPhotonAsync::TryDisconnect()
{
	FUSION_LOG("Try Disconnecting from Photon");

	UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(WorldContextObjectBase)->GetSubsystem<UFusionOnlineSubsystem>();
	if (OnlineSubsystem->GFusionClient)
	{
		OnlineSubsystem->StopFusionSession();
	}

	if (OnlineSubsystem->RealtimeClient && OnlineSubsystem->RealtimeClient->IsValid())
	{
		OnlineSubsystem->RealtimeClient->PendingVoidTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->Disconnect());
	}

	constexpr float CheckInterval = 0.1f;

	TimerState.ChecksDone = 0;

	FTimerManager& TimerManager = WorldContextObjectBase->GetWorld()->GetTimerManager();
	TimerManager.SetTimer(TimerState.Handle, this, &UDisconnectFromPhotonAsync::WaitDisconnect, CheckInterval, true);
}

void UDisconnectFromPhotonAsync::WaitDisconnect()
{
	FTimerManager& TimerManager = WorldContextObjectBase->GetWorld()->GetTimerManager();
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase);
	UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();

	if (OnlineSubsystem && !OnlineSubsystem->IsConnected())
	{
		FUSION_LOG("Disconnected from photon");

		OnlineSubsystem->RealtimeClient = nullptr;

		if (OnSuccess.IsBound())
		{
			OnSuccess.Broadcast();
		}

		TimerManager.ClearTimer(TimerState.Handle);
		DestroyAction();

		return;
	}

	TimerState.ChecksDone++;

	if (constexpr int32 MaxChecks = 50; TimerState.ChecksDone >= MaxChecks)
	{
		FUSION_LOG("Disconnecting time out");

		OnlineSubsystem->RealtimeClient = nullptr;

		if (OnFailure.IsBound())
		{
			OnFailure.Broadcast(EActionFailureCodes::TimeOut);
		}

		TimerManager.ClearTimer(TimerState.Handle);
		DestroyAction();
	}
}
