// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/FusionConnectToPhotonAsync.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "FusionShared.h"
#include "FusionUtils.h"
#include "FusionOnlineSubsystemSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h" 

UFusionConnectToPhotonAsync* UFusionConnectToPhotonAsync::ConnectToPhoton(const FFusionConnectOptions ConnectOptions, UObject* WorldContextObject)
{
	UFusionConnectToPhotonAsync* action = NewObject<UFusionConnectToPhotonAsync>();
	action->WorldContextObjectBase = WorldContextObject;
	action->ConnectOptions = ConnectOptions;
	
	if (ConnectOptions.Region != "eu" && ConnectOptions.Region != "us" && ConnectOptions.Region != "asia" && ConnectOptions.RegionSelectionMode == EFusionRegionSelectionMode::Select)
	{
		action->bFailInstantly = true;
	}
	
	if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(WorldContextObject->GetWorld()))
	{
		const EFusionInstanceType WorldContextType = UFusionHelpers::WorldContextType(*WorldContext);
		UE_LOG(LogFusion, Warning, TEXT("Try connecting from instance type: %s"), *UEnum::GetValueAsString(WorldContextType))
	}
	
	return action;
}

void UFusionConnectToPhotonAsync::Activate()
{
	if (bFailInstantly)
	{
		FUSION_LOG_ERROR("Invalid region, currently valid region codes are eu, us and asia");
		
		if (OnFailure.IsBound()) {
			OnFailure.Broadcast(EFusionActionFailureCodes::InvalidRegion);
		}

		DestroyAction();
	}
	else
	{
		
		if (const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase))
		{
			if (const UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>(); OnlineSubsystem && OnlineSubsystem->IsConnected())
			{
				FUSION_LOG_WARN("Already connected when trying to connect");
				if (OnFailure.IsBound())
					OnFailure.Broadcast(EFusionActionFailureCodes::Connecting);
				DestroyAction();
				return;
			}
		}

		TryConnect();
	}
}

void UFusionConnectToPhotonAsync::TryConnect()
{
	const UFusionOnlineSubsystemSettings* Settings = UFusionOnlineSubsystemSettings::GetPhotonOnlineSettings();
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase);
	if (!GameInstance)
	{
		FUSION_LOG_ERROR("Invalid GameInstance during connect");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::TimeOut);
		DestroyAction();
		return;
	}
	UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem)
	{
		FUSION_LOG_ERROR("FusionOnlineSubsystem not available during connect");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::TimeOut);
		DestroyAction();
		return;
	}

	if (OnlineSubsystem->GFusionClient)
	{
		OnlineSubsystem->StopFusionSession();
	}
	
	OnlineSubsystem->RealtimeClient = nullptr;
	OnlineSubsystem->RealtimeClient = UFusionRealtimeClient::CreateRealtimeClient(ConnectOptions);

	PhotonMatchmaking::ConnectOptions RealtimeConnectOptions{};
	RealtimeConnectOptions.auth.userId = reinterpret_cast<const PhotonCommon::CharType*>(StringCast<UTF8CHAR>(*FGuid::NewGuid().ToString()).Get());

	if (Settings->UseLocalServer)
	{
		const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> LocalServerUrl = StringCast<UTF8CHAR>(*Settings->LocalServerUrl);
		RealtimeConnectOptions.serverType = PhotonMatchmaking::ServerType::MasterServer;
		RealtimeConnectOptions.serverAddress = reinterpret_cast<const PhotonCommon::CharType*>(LocalServerUrl.Get());
		FUSION_LOG("Try Connecting to local server with URL: %s", *Settings->LocalServerUrl);
	}
	else
	{
		if (Settings->CloudServerUrl.Len() > 0)
		{
			const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> CloudServerUrl = StringCast<UTF8CHAR>(*Settings->CloudServerUrl);
			RealtimeConnectOptions.serverAddress = reinterpret_cast<const PhotonCommon::CharType*>(CloudServerUrl.Get());
		}
	}

	if (ConnectOptions.RegionSelectionMode == EFusionRegionSelectionMode::Select)
	{
		PendingRegionsTask.emplace(OnlineSubsystem->RealtimeClient->GetClient()->AvailableRegions());
		ConnectPhase = EConnectPhase::WaitingForRegions;
	}
	else
	{
		ConnectPhase = EConnectPhase::WaitingForConnect;
	}

	OnlineSubsystem->RealtimeClient->PendingVoidTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->Connect(RealtimeConnectOptions));

	constexpr float CheckInterval = 0.1f;

	TimerState.ChecksDone = 0;

	FTimerManager& TimerManager = WorldContextObjectBase->GetWorld()->GetTimerManager();
	TimerManager.SetTimer(TimerState.Handle, this, &UFusionConnectToPhotonAsync::WaitConnect, CheckInterval, true);
}

void UFusionConnectToPhotonAsync::WaitConnect()
{
	FTimerManager& TimerManager = WorldContextObjectBase->GetWorld()->GetTimerManager();
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObjectBase);
	if (!GameInstance)
	{
		FUSION_LOG_ERROR("Invalid GameInstance during WaitConnect");
		TimerManager.ClearTimer(TimerState.Handle);
		PendingRegionsTask.reset();
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::TimeOut);
		DestroyAction();
		return;
	}

	UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem)
	{
		FUSION_LOG_ERROR("FusionOnlineSubsystem not available during WaitConnect");
		TimerManager.ClearTimer(TimerState.Handle);
		PendingRegionsTask.reset();
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::TimeOut);
		DestroyAction();
		return;
	}

	if (ConnectPhase == EConnectPhase::WaitingForRegions && PendingRegionsTask.has_value())
	{
		if (PendingRegionsTask->IsReady())
		{
			auto RegionsResult = PendingRegionsTask->Get();
			PendingRegionsTask.reset();

			if (!RegionsResult.IsOk())
			{
				FUSION_LOG_ERROR("AvailableRegions request failed");
				TimerManager.ClearTimer(TimerState.Handle);
				if (OnFailure.IsBound())
					OnFailure.Broadcast(EFusionActionFailureCodes::Error);
				DestroyAction();
				return;
			}

			const auto& RegionList = RegionsResult.GetValue();
			const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> RegionUTF8 = StringCast<UTF8CHAR>(*ConnectOptions.Region);
			const PhotonCommon::CharType* RegionCode = reinterpret_cast<const PhotonCommon::CharType*>(RegionUTF8.Get());

			bool bRegionFound = false;
			for (const auto& R : RegionList)
			{
				if (R.code == RegionCode)
				{
					bRegionFound = true;
					break;
				}
			}

			if (!bRegionFound)
			{
				FUSION_LOG_ERROR("Requested region '%s' not found in available regions", *ConnectOptions.Region);
				TimerManager.ClearTimer(TimerState.Handle);
				if (OnFailure.IsBound())
					OnFailure.Broadcast(EFusionActionFailureCodes::InvalidRegion);
				DestroyAction();
				return;
			}

			OnlineSubsystem->RealtimeClient->PendingVoidTasks.push_back(
				OnlineSubsystem->RealtimeClient->GetClient()->SelectRegion(RegionCode));

			ConnectPhase = EConnectPhase::WaitingForConnect;
		}
	}

	if (ConnectPhase == EConnectPhase::WaitingForConnect)
	{
		if (OnlineSubsystem->IsConnected())
		{
			OnSuccess.Broadcast();
			TimerManager.ClearTimer(TimerState.Handle);
			DestroyAction();
			return;
		}
	}

	TimerState.ChecksDone++;

	if (constexpr int32 MaxChecks = 300; TimerState.ChecksDone >= MaxChecks)
	{
		FUSION_LOG("Connecting time out");
		PendingRegionsTask.reset();

		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::TimeOut);

		TimerManager.ClearTimer(TimerState.Handle);
		DestroyAction();
	}
}