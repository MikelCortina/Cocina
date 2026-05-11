// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "FusionNetDriver.h"

#include "Misc/AssertionMacros.h"
#include "FusionClient.h"
#include "FusionNetConnection.h"
#include "FusionOnlineSubsystem.h"
#include "Fusion/Buffers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Types/FusionTypeDescriptor.h"

#include "UObject/Package.h"

bool UFusionNetDriver::InitConnect(FNetworkNotify* InNotify, const FURL& ConnectURL, FString& Error)
{
	check(ServerConnection == nullptr);
	ServerConnection = NewObject<UNetConnection>(this, UFusionNetConnection::StaticClass());
	ServerConnection->InitConnection(this, USOCK_Open, ConnectURL, 1000000);
	
	bMaySendProperties = true;

	return true;
}

bool UFusionNetDriver::IsServer() const
{
	return bIsActingAsAServer;
}

void UFusionNetDriver::Cleanup()
{
	if (ServerConnection)
	{
		ServerConnection->CleanUp();
	}
	ServerConnection = nullptr;
}

void UFusionNetDriver::CleanPackageMaps()
{

}

void UFusionNetDriver::SetIsServer(bool bToggle)
{
	bIsActingAsAServer = bToggle;
}

void UFusionNetDriver::ProcessRemoteFunction(AActor* Actor, UFunction* Function, void* Parameters,
                                             FOutParmRec* OutParms, FFrame* Stack, UObject* SubObject)
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(Actor);
	if (!GameInstance)
	{
		return;
	}
	UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem || !OnlineSubsystem->GFusionClient || !OnlineSubsystem->GFusionClient->Lookup.IsValid())
	{
		return;
	}
	const FString EventName = Function->GetName();
	const uint32 HashCRC = FCrc::StrCrc32(*EventName);
	const int32 HashInt32 = static_cast<int32>(HashCRC & 0x7FFFFFFF);

	UObject* TargetType = SubObject != nullptr ? SubObject : Actor;

	//Ensure that the backing event is mapped on a descriptor.
	if (UFusionTypeDescriptor* Descriptor = OnlineSubsystem->GFusionClient->Lookup->FindClassDescriptor(TargetType->GetClass()); Descriptor && Descriptor->EventFunctions.Contains(EventName))
	{
		SharedMode::WriteBuffer WriteBuffer;
		TObjectPtr<UFusionFunctionDescriptor> EvtFunction = Descriptor->EventFunctions[EventName];

		for (FFusionFunctionProperty FunctionProperty : EvtFunction->FunctionProperties)
		{
			uint8* Ptr = static_cast<uint8*>(Parameters) + FunctionProperty.WordOffset;

			for (int32 i = FunctionProperty.StartRange; i < FunctionProperty.EndRange; i++)
			{
				Property* Property = EvtFunction->Properties[i];
				check(Property);
				Property->Serialize(OnlineSubsystem->GFusionClient, Ptr, FunctionProperty, WriteBuffer, true);
			}
		}

		TArray<uint8> Buffer;
		FusionCore::Data Data = WriteBuffer.Take();
		if (Data.Valid())
		{
			Buffer.Append(Data.Ptr, Data.Length);
			Data.Free();
		}

		if (Function->FunctionFlags & FUNC_NetMulticast)
		{
			OnlineSubsystem->SendCustomRPC(TargetType, EventName, HashInt32, EFusionRPCTarget::SendToAllClients, Buffer, ERPCMode::UnrealRPC);
		}
		else if (Function->FunctionFlags & FUNC_NetClient)
		{
			OnlineSubsystem->SendCustomRPC(TargetType, EventName, HashInt32, EFusionRPCTarget::SendToObjectOwner, Buffer, ERPCMode::UnrealRPC);
		}
		else if (Function->FunctionFlags & FUNC_NetServer)
		{
			OnlineSubsystem->SendCustomRPC(TargetType, EventName, HashInt32, EFusionRPCTarget::SendToObjectOwner, Buffer, ERPCMode::UnrealRPC);
		}
	}
}

bool UFusionNetDriver::ShouldReplicateFunction(AActor* Actor, UFunction* Function) const
{
	return true;
}
