// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/JoinRoomAsync.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h" 

UJoinRoomAsync* UJoinRoomAsync::JoinRoom(const FString RoomName, UObject* WorldContextObject)
{
	UJoinRoomAsync* Action = NewObject<UJoinRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomName = RoomName;
	Action->bRandomRoom = false;
	
	return Action;
}

UJoinRoomAsync* UJoinRoomAsync::JoinRandomRoom(UObject* WorldContextObject)
{
	UJoinRoomAsync* Action = NewObject<UJoinRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->bRandomRoom = true;
	
	return Action;
}

void UJoinRoomAsync::Activate()
{
	UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(WorldContextObjectBase)->
		GetSubsystem<UFusionOnlineSubsystem>();

	if (!OnlineSubsystem->IsConnected())
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
		return;
	}

	if (OnlineSubsystem->RealtimeClient->IsInRoom())
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::JoiningRoom);
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
