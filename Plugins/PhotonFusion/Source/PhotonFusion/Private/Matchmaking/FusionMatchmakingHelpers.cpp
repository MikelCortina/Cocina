// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Matchmaking/FusionMatchmakingHelpers.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionRealtimeClient.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Fusion/FusionRoomProperties.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

bool FusionMatchmakingHelpers::StartFusion(TObjectPtr<UObject> WorldContextObjectBase, TObjectPtr<UFusionRealtimeClient> RealtimeClient)
{
	if (!WorldContextObjectBase.Get())
		return false;

	if (!RealtimeClient.Get())
		return false;
	
	UFusionOnlineSubsystem* OnlineSubsystem = WorldContextObjectBase->GetWorld()->GetGameInstance()->GetSubsystem<UFusionOnlineSubsystem>();
	
	if (OnlineSubsystem->GetFusionClient())
	{
		return false;
	}
	
	const UFusionOnlineSubsystemSettings* Settings = UFusionOnlineSubsystemSettings::GetPhotonOnlineSettings();
	UFusionClient* FusionClient = NewObject<UFusionClient>();
	FusionClient->Startup(WorldContextObjectBase->GetWorld(), OnlineSubsystem->GetTypeLookup(), Settings, RealtimeClient);
	OnlineSubsystem->SetFusionClient(FusionClient);

	return true;
}

PhotonMatchmaking::CreateRoomOptions FusionMatchmakingHelpers::CreatePhotonRoomOptions(const FFusionRoomOptions& InputRoomOptions)
{
	PhotonMatchmaking::CreateRoomOptions Options = InputRoomOptions.ToCreateRoomOptions();

	if (!InputRoomOptions.InitialWorld.IsNull())
	{
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("MapName"), InputRoomOptions.InitialWorld.ToSoftObjectPath().GetLongPackageName());
		Payload->SetBoolField(TEXT("Attached"), false);
		
		FString PayloadString;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadString);
		FJsonSerializer::Serialize(Payload, Writer);

		const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> PayloadUTF8 = StringCast<UTF8CHAR>(*PayloadString);
		const uint8_t* PayloadBytes = reinterpret_cast<const uint8_t*>(PayloadUTF8.Get());
		std::vector<uint8_t> PayloadVector(PayloadBytes, PayloadBytes + PayloadUTF8.Length());

		std::map<FusionCore::Map, std::vector<uint8_t>> maps;
		maps[FusionCore::Map{1}] = std::move(PayloadVector);

		const std::string MapDataBase64 = FusionCore::FusionMapStateBuilder::BuildBase64(maps, 0);
		Options.customProperties[PhotonCommon::StringType{FusionCore::FusionRoomProperties::MapData}] =
			PhotonCommon::StringType{
				reinterpret_cast<const char8_t*>(MapDataBase64.data()),
				MapDataBase64.size()
			};
	}
	
	return Options;
}
