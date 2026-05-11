// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Actions/FusionConnectAndJoinRoomAsync.h"
#include "Actions/FusionCreateRoomAsync.h"
#include "Actions/FusionConnectToPhotonAsync.h"
#include "Actions/FusionJoinOrCreateRoomAsync.h"
#include "FusionClient.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"

UFusionConnectAndJoinRoomAsync* UFusionConnectAndJoinRoomAsync::ConnectAndJoinRoom(const FFusionConnectOptions ConnectOptions, const FFusionRoomOptions RoomOptions, UObject* WorldContextObject)
{
	UFusionConnectAndJoinRoomAsync* Action = NewObject<UFusionConnectAndJoinRoomAsync>();
	Action->WorldContextObjectBase = WorldContextObject;
	Action->RoomOptions = RoomOptions;
	Action->ConnectOptions = ConnectOptions;

	return Action;
}

void UFusionConnectAndJoinRoomAsync::Activate()
{
	if (const UFusionOnlineSubsystem* OnlineSubsystem = UGameplayStatics::GetGameInstance(WorldContextObjectBase)->
		GetSubsystem<UFusionOnlineSubsystem>(); OnlineSubsystem->IsConnected())
	{
		FUSION_LOG_WARN("Already connected when trying to connect");

		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::Connecting);
		DestroyAction();
		return;
	}

	if (UFusionConnectToPhotonAsync* Action = UFusionConnectToPhotonAsync::ConnectToPhoton(ConnectOptions, WorldContextObjectBase))
	{
		ConnectPhotonAsync = Action;
		ConnectPhotonAsync->OnSuccess.AddUniqueDynamic(this,
			&UFusionConnectAndJoinRoomAsync::OnConnectToPhotonSuccess);
		ConnectPhotonAsync->OnFailure.AddUniqueDynamic(this,
			&UFusionConnectAndJoinRoomAsync::OnAsyncFailure);

		ConnectPhotonAsync->Activate();
	}
	else
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::Disconnected);
		DestroyAction();
	}

}

void UFusionConnectAndJoinRoomAsync::OnConnectToPhotonSuccess()
{
	if (UFusionJoinOrCreateRoomAsync* Action = UFusionJoinOrCreateRoomAsync::JoinOrCreateRoom(RoomOptions, WorldContextObjectBase))
	{
		JoinOrCreateRoomAsync = Action;
		JoinOrCreateRoomAsync->OnSuccess.AddUniqueDynamic(this,
			&UFusionConnectAndJoinRoomAsync::OnJoinOrCreateRoomSuccess);
		JoinOrCreateRoomAsync->OnFailure.AddUniqueDynamic(this,
			&UFusionConnectAndJoinRoomAsync::OnAsyncFailure);

		JoinOrCreateRoomAsync->Activate();
	}
	else
	{
		if (OnFailure.IsBound())
			OnFailure.Broadcast(EFusionActionFailureCodes::Disconnected);
	}
}

void UFusionConnectAndJoinRoomAsync::OnAsyncFailure(const EFusionActionFailureCodes FailureCode)
{
	if (OnFailure.IsBound())
		OnFailure.Broadcast(FailureCode);
}

void UFusionConnectAndJoinRoomAsync::OnJoinOrCreateRoomSuccess()
{
	if (OnSuccess.IsBound())
		OnSuccess.Broadcast();
}
