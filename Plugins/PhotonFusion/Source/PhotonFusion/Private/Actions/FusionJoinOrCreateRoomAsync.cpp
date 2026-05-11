// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/FusionJoinOrCreateRoomAsync.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Fusion/FusionRoomProperties.h"
#include "Matchmaking/FusionMatchmakingHelpers.h"


UFusionJoinOrCreateRoomAsync* UFusionJoinOrCreateRoomAsync::JoinOrCreateRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject)
{
	UFusionJoinOrCreateRoomAsync* Action = NewObject<UFusionJoinOrCreateRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomOptions = RoomOptions;
	Action->bRandom = false;
	
	return Action;
}

UFusionJoinOrCreateRoomAsync* UFusionJoinOrCreateRoomAsync::JoinOrCreateRandomRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject)
{
	UFusionJoinOrCreateRoomAsync* Action = NewObject<UFusionJoinOrCreateRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomOptions = RoomOptions;
	Action->bRandom = true;
	
	return Action;
}

void UFusionJoinOrCreateRoomAsync::Activate()
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
		FUSION_LOG_WARN("Already in a room when trying to join or create room");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::InRoom);
		DestroyAction();
		return;
	}
	
	const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> RoomName = StringCast<UTF8CHAR>(*RoomOptions.RoomName);
	if (bRandom)
	{
		OnlineSubsystem->RealtimeClient->PendingRoomTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->JoinRandomOrCreateRoom(FusionMatchmakingHelpers::CreatePhotonRoomOptions(RoomOptions)));
	}
	else
	{
		OnlineSubsystem->RealtimeClient->PendingRoomTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->JoinOrCreateRoom(
			reinterpret_cast<const PhotonCommon::CharType*>(RoomName.Get()),
			FusionMatchmakingHelpers::CreatePhotonRoomOptions(RoomOptions)));
	}
	WaitForRoomJoined();
}
