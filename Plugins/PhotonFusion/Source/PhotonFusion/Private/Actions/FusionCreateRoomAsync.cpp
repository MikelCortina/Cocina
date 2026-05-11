// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/FusionCreateRoomAsync.h"

#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Fusion/FusionRoomProperties.h"
#include "Matchmaking/FusionMatchmakingHelpers.h"

UFusionCreateRoomAsync* UFusionCreateRoomAsync::CreateRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject)
{
	UFusionCreateRoomAsync* Action = NewObject<UFusionCreateRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomOptions = RoomOptions;
	
	return Action;
}

void UFusionCreateRoomAsync::Activate()
{
	UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(WorldContextObjectBase)->GetSubsystem<
		UFusionOnlineSubsystem>();

	if (!OnlineSubsystem->IsConnected())
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	if (OnlineSubsystem->RealtimeClient->IsInRoom())
	{
		FUSION_LOG_WARN("Already in a room when trying to create room");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::InRoom);
		DestroyAction();
		return;
	}

	const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> RoomName = StringCast<UTF8CHAR>(*RoomOptions.RoomName);
	OnlineSubsystem->RealtimeClient->PendingRoomTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->CreateRoom(
		reinterpret_cast<const PhotonCommon::CharType*>(RoomName.Get()),
		FusionMatchmakingHelpers::CreatePhotonRoomOptions(RoomOptions)));
	WaitForRoomJoined();
}
