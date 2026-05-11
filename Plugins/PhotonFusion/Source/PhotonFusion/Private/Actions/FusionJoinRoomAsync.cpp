// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/FusionJoinRoomAsync.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h" 

UFusionJoinRoomAsync* UFusionJoinRoomAsync::JoinRoom(const FString RoomName, UObject* WorldContextObject)
{
	UFusionJoinRoomAsync* Action = NewObject<UFusionJoinRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomName = RoomName;
	Action->bRandomRoom = false;
	
	return Action;
}

UFusionJoinRoomAsync* UFusionJoinRoomAsync::JoinRandomRoom(UObject* WorldContextObject)
{
	UFusionJoinRoomAsync* Action = NewObject<UFusionJoinRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->bRandomRoom = true;
	
	return Action;
}

void UFusionJoinRoomAsync::Activate()
{
	UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(WorldContextObjectBase)->
		GetSubsystem<UFusionOnlineSubsystem>();

	if (!OnlineSubsystem->IsConnected())
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::Disconnected);
		DestroyAction();
		return;
	}

	if (OnlineSubsystem->RealtimeClient->IsInRoom())
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::JoiningRoom);
		DestroyAction();
		return;
	}

	if (bRandomRoom)
	{
		OnlineSubsystem->RealtimeClient->PendingRoomTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->JoinRandomRoom());
		WaitForRoomJoined();
	}
	else
	{
		const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> RoomNameUTF = StringCast<UTF8CHAR>(*RoomName);
		OnlineSubsystem->RealtimeClient->PendingRoomTasks.push_back(OnlineSubsystem->RealtimeClient->GetClient()->JoinRoom(reinterpret_cast<const PhotonCommon::CharType*>(RoomNameUTF.Get())));
		WaitForRoomJoined();
	}
}
