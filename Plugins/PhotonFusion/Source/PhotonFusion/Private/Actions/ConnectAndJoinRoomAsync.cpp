// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/ConnectAndJoinRoomAsync.h"
#include "Actions/CreateRoomAsync.h"
#include "Actions/ConnectToPhotonAsync.h"
#include "Actions/JoinOrCreateRoomAsync.h"
#include "FusionClient.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"

UConnectAndJoinRoomAsync* UConnectAndJoinRoomAsync::ConnectAndJoinRoom(const FFusionConnectOptions ConnectOptions, const FFusionRoomOptions RoomOptions, UObject* WorldContextObject)
{
	UConnectAndJoinRoomAsync* Action = NewObject<UConnectAndJoinRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomOptions = RoomOptions;
	Action->ConnectOptions = ConnectOptions;

	return Action;
}

void UConnectAndJoinRoomAsync::Activate()
{
	if (const UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(WorldContextObjectBase)->
		GetSubsystem<UFusionOnlineSubsystem>(); OnlineSubsystem->IsConnected())
	{
		FUSION_LOG_WARN("Already connected when trying to connect");

		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Connecting);
		return;
	}

	if (UConnectToPhotonAsync* Action = UConnectToPhotonAsync::ConnectToPhoton(ConnectOptions, WorldContextObjectBase))
	{
		ConnectPhotonAsync = Action;
		ConnectPhotonAsync->OnSuccess.AddUniqueDynamic(this,
			&UConnectAndJoinRoomAsync::OnConnectToPhotonSuccess);
		ConnectPhotonAsync->OnFailure.AddUniqueDynamic(this,
			&UConnectAndJoinRoomAsync::OnAsyncFailure);

		ConnectPhotonAsync->Activate();
	}
	else
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
	}

}

void UConnectAndJoinRoomAsync::OnConnectToPhotonSuccess()
{
	if (UJoinOrCreateRoomAsync* Action = UJoinOrCreateRoomAsync::JoinOrCreateRoom(RoomOptions, WorldContextObjectBase))
	{
		JoinOrCreateRoomAsync = Action;
		JoinOrCreateRoomAsync->OnSuccess.AddUniqueDynamic(this,
			&UConnectAndJoinRoomAsync::OnJoinOrCreateRoomSuccess);
		JoinOrCreateRoomAsync->OnFailure.AddUniqueDynamic(this,
			&UConnectAndJoinRoomAsync::OnAsyncFailure);

		JoinOrCreateRoomAsync->Activate();
	}
	else
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EActionFailureCodes::Disconnected);
	}
}

void UConnectAndJoinRoomAsync::OnAsyncFailure(const EActionFailureCodes FailureCode)
{
	if (OnFailure.IsBound())
		OnFailure.Broadcast(FailureCode);
}

void UConnectAndJoinRoomAsync::OnJoinOrCreateRoomSuccess()
{
	if (OnSuccess.IsBound())
		OnSuccess.Broadcast();
}
