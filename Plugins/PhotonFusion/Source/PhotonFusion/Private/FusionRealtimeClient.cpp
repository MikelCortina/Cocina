// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "FusionRealtimeClient.h"
#include "FusionUtils.h"
#include "FusionOnlineSubsystemSettings.h"

UFusionRealtimeClient* UFusionRealtimeClient::CreateRealtimeClient(FFusionConnectOptions Options)
{
	const UFusionOnlineSubsystemSettings* Settings = UFusionOnlineSubsystemSettings::GetPhotonOnlineSettings();

	PhotonMatchmaking::ClientConstructOptions ConstructOptions = Options.ToClientConstructOptions(Settings->AppId, Settings->AppVersion);

	UFusionRealtimeClient* Wrapper = NewObject<UFusionRealtimeClient>();
	Wrapper->Client = new PhotonMatchmaking::RealtimeClient(ConstructOptions);
	Wrapper->bOwnsClient = true;
	return Wrapper;
}

UFusionRealtimeClient* UFusionRealtimeClient::FromExisting(PhotonMatchmaking::RealtimeClient* InClient)
{
	if (!InClient)
		return nullptr;

	UFusionRealtimeClient* Wrapper = NewObject<UFusionRealtimeClient>();
	Wrapper->Client = InClient;
	Wrapper->bOwnsClient = false;
	return Wrapper;
}

PhotonMatchmaking::RealtimeClient* UFusionRealtimeClient::GetClient() const
{
	return Client;
}

bool UFusionRealtimeClient::IsValid() const
{
	return Client != nullptr;
}

bool UFusionRealtimeClient::OwnsClient() const
{
	return bOwnsClient;
}

bool UFusionRealtimeClient::IsConnected() const
{
	return Client && Client->IsConnected();
}

bool UFusionRealtimeClient::IsInRoom() const
{
	return Client && Client->IsInRoom();
}

EFusionStatus UFusionRealtimeClient::GetStatus() const
{
	if (!Client)
		return EFusionStatus::None;

	switch (Client->GetState())
	{
	case PhotonMatchmaking::ConnectionState::Disconnected:
		return EFusionStatus::Disconnected;
	case PhotonMatchmaking::ConnectionState::Connecting:
		return EFusionStatus::Connecting;
	case PhotonMatchmaking::ConnectionState::Connected:
		return EFusionStatus::Connected;
	case PhotonMatchmaking::ConnectionState::JoiningRoom:
		return EFusionStatus::JoiningRoom;
	case PhotonMatchmaking::ConnectionState::InRoom:
		return EFusionStatus::InRoom;
	case PhotonMatchmaking::ConnectionState::LeavingRoom:
		return EFusionStatus::LeavingRoom;
	case PhotonMatchmaking::ConnectionState::Disconnecting:
		return EFusionStatus::Disconnected;
	default:
		return EFusionStatus::None;
	}
}

int32 UFusionRealtimeClient::GetLocalPlayerId() const
{
	if (Client)
		return Client->GetLocalPlayer().number;

	return 0;
}

bool UFusionRealtimeClient::IsMasterClient() const
{
	if (Client && Client->IsInRoom())
	{
		if (auto Room = Client->GetCurrentRoom())
		{
			return Room->IsMasterClient();
		}
	}

	return false;
}

int32 UFusionRealtimeClient::PlayerCount() const
{
	if (Client && Client->IsInRoom())
	{
		if (auto Room = Client->GetCurrentRoom())
		{
			return Room->GetPlayerCount();
		}
	}

	return 0;
}

bool UFusionRealtimeClient::GetRoomInfo(FString& Name, int32& Players) const
{
	if (IsInRoom())
	{
		if (auto Room = Client->GetCurrentRoom())
		{
			const auto& RoomName = Room->GetName();
			Name = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(RoomName.c_str())));
			Players = Room->GetPlayerCount();
		}
		return true;
	}

	return false;
}

void UFusionRealtimeClient::Service()
{
	if (Client)
		Client->Service();
}

void UFusionRealtimeClient::CleanupCompletedTasks()
{
	std::erase_if(PendingVoidTasks, [](const auto& t) { return t.IsReady(); });
	std::erase_if(PendingRoomTasks, [](const auto& t) { return t.IsReady(); });
}

void UFusionRealtimeClient::BeginDestroy()
{
	PendingVoidTasks.clear();
	PendingRoomTasks.clear();

	if (bOwnsClient && Client)
	{
		delete Client;
	}
	Client = nullptr;

	Super::BeginDestroy();
}
