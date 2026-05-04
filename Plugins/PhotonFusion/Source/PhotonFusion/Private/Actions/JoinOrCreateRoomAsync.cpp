// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/JoinOrCreateRoomAsync.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"

UJoinOrCreateRoomAsync* UJoinOrCreateRoomAsync::JoinOrCreateRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject)
{
	UJoinOrCreateRoomAsync* Action = NewObject<UJoinOrCreateRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomOptions = RoomOptions;
	Action->bRandom = false;
	
	return Action;
}

UJoinOrCreateRoomAsync* UJoinOrCreateRoomAsync::JoinOrCreateRandomRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject)
{
	UJoinOrCreateRoomAsync* Action = NewObject<UJoinOrCreateRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomOptions = RoomOptions;
	Action->bRandom = true;
	
	return Action;
}

void UJoinOrCreateRoomAsync::Activate()
{
	UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(WorldContextObjectBase)->GetSubsystem<
		UFusionOnlineSubsystem>();
	if (!OnlineSubsystem->IsConnected())
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
		return;
	}

	if (OnlineSubsystem->RealtimeClient->IsInRoom())
	{
		FUSION_LOG_WARN("Already in a room when trying to join or create room");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::InRoom);
		return;
	}

	const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> RoomName = StringCast<UTF8CHAR>(*RoomOptions.RoomName);
	if (bRandom)
	{
		OnlineSubsystem->RealtimeClient->PendingRoomTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->JoinRandomOrCreateRoom(RoomOptions.ToCreateRoomOptions()));
	}
	else
	{
		OnlineSubsystem->RealtimeClient->PendingRoomTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->JoinOrCreateRoom(
			reinterpret_cast<const PhotonCommon::CharType*>(RoomName.Get()),
			RoomOptions.ToCreateRoomOptions()));
	}
	WaitForRoomJoined();
}
