// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/CreateRoomAsync.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h" 

UCreateRoomAsync* UCreateRoomAsync::CreateRoom(const FFusionRoomOptions RoomOptions, UObject* WorldContextObject)
{
	UCreateRoomAsync* Action = NewObject<UCreateRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomOptions = RoomOptions;
	
	return Action;
}

void UCreateRoomAsync::Activate()
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
		FUSION_LOG_WARN("Already in a room when trying to create room");
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::InRoom);
		return;
	}

	const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> RoomName = StringCast<UTF8CHAR>(*RoomOptions.RoomName);
	OnlineSubsystem->RealtimeClient->PendingRoomTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->CreateRoom(
		reinterpret_cast<const PhotonCommon::CharType*>(RoomName.Get()),
		RoomOptions.ToCreateRoomOptions()));
	WaitForRoomJoined();
}
