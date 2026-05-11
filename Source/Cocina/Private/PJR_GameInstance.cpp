#include "PJR_GameInstance.h"

void UPJR_GameInstance::SetSessionData(const FString& NewPlayerName, const FString& NewRoomName)
{
	PlayerName = NewPlayerName;
	RoomName = NewRoomName;
}

FString UPJR_GameInstance::GetPlayerName() const
{
	return PlayerName;
}

FString UPJR_GameInstance::GetRoomName() const
{
	return RoomName;
}