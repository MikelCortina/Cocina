// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "FusionHelpers.h"

#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionShared.h"
#include "Types/TypeDescriptor.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "CoreMinimal.h"
#include "EngineGlobals.h"  
#include "FusionActorComponent.h"
#include "CoreMinimal.h"

#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#if WITH_EDITOR
#include "Misc/CommandLine.h"
#endif

FGuid UFusionHelpers::InstanceId;

void UFusionHelpers::FusionClientTravel(const UObject* WorldContextObject, FName LevelName, bool bAbsolute, FString Options)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (World == nullptr)
	{
		return;
	}

	const ETravelType TravelType = (bAbsolute ? TRAVEL_Absolute : TRAVEL_Relative);
	FWorldContext &WorldContext = GEngine->GetWorldContextFromWorldChecked(World);
	FString Cmd = LevelName.ToString();
	if (Options.Len() > 0)
	{
		Cmd += FString(TEXT("?")) + Options;
	}
	FURL TestURL(&WorldContext.LastURL, *Cmd, TravelType);
	if (TestURL.IsLocalInternal())
	{
		// make sure the file exists if we are opening a local file
		if (!GEngine->MakeSureMapNameIsValid(TestURL.Map))
		{
			UE_LOG(LogLevel, Warning, TEXT("WARNING: The map '%s' does not exist."), *TestURL.Map);
		}
	}

	if (const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContext.World()))
	{
		if (UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>())
		{
			OnlineSubsystem->ClientTravel(TestURL.Map, const_cast<UObject*>(WorldContextObject));
		}
	}
}

void UFusionHelpers::InvokeCustomRPC(UObject* Source, const FString EventName, const int32 RPCId, const EFusionRPCTarget Target, const TArray<uint8>& Buffer)
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(Source);
	if (!GameInstance)
	{
		return;
	}
	UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem)
	{
		return;
	}
	OnlineSubsystem->SendCustomRPC(Source, EventName, RPCId, Target, Buffer, ERPCMode::FusionRPC);
}

UFunctionDescriptor* UFusionHelpers::GetFunctionDescriptor(UObject* Source, const FString EventName)
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(Source);
	if (!GameInstance)
	{
		return nullptr;
	}
	const UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
	if (!OnlineSubsystem || !OnlineSubsystem->GFusionClient)
	{
		return nullptr;
	}

	if (UTypeLookup* Lookup = OnlineSubsystem->GFusionClient->Lookup.Get())
	{
		UClass* Cls = Source->GetClass();
		UTypeDescriptor* Descriptor = Lookup->FindClassDescriptor(Cls);

		//Ensure that the backing event is mapped on a descriptor.
		if (Descriptor && Descriptor->EventFunctions.Contains(EventName))
		{
			return Descriptor->EventFunctions[EventName];
		}
	}

	return nullptr;
}

void UFusionHelpers::AddParamToBuffer([[maybe_unused]] const int32& Value, [[maybe_unused]] UObject* Source, [[maybe_unused]] UFunctionDescriptor* Descriptor, [[maybe_unused]] int32 PropertyIndex, [[maybe_unused]] TArray<uint8>& Buffer)
{
	// this method is empty on purpose, do not change that.
}

DEFINE_FUNCTION(UFusionHelpers::execAddParamToBuffer)
{
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr); 
	
	void* InputAddress = Stack.MostRecentPropertyAddress;

	P_GET_OBJECT(UObject, Source)

	P_GET_OBJECT(UFunctionDescriptor, Descriptor)

	P_GET_PROPERTY(FIntProperty, PropertyIndex);
	
	P_GET_TARRAY_REF(uint8, Buffer);
	
	P_FINISH;
	
	Descriptor->SerializeParams(InputAddress, PropertyIndex, Buffer, Source);
}

TArray<FDefaultComponentInfo> UFusionHelpers::GetDefaultOwnerComponents(UClass* Class)
{
	TArray<FDefaultComponentInfo> UniqueOut;

	UClass* ActorClass = Class;
	
	if (ActorClass)
	{
		TArray<FDefaultComponentInfo> Out;

		if (AActor* CDO = ActorClass->GetDefaultObject<AActor>())
		{
			for (TInlineComponentArray<UActorComponent*> CDOComps(CDO); UActorComponent* Comp : CDOComps)
			{
				if (!Comp) continue;
				FDefaultComponentInfo Info;
				Info.VariableName = Comp->GetFName();
				Info.TemplateName = Comp->GetName();
				Info.ComponentClass = Comp->GetClass();
				Info.ComponentTemplate = Comp;
				Out.Add(Info);
			}
		}

		UClass* CurrentClass = ActorClass;
		
		while (CurrentClass)
		{
			if (UBlueprintGeneratedClass* BpgClass = Cast<UBlueprintGeneratedClass>(CurrentClass))
			{
				for (UActorComponent* Template : BpgClass->ComponentTemplates)
				{
					if (!Template) continue;
					FDefaultComponentInfo Info;
					Info.VariableName = Template->GetFName();
					Info.TemplateName = Template->GetName();
					Info.ComponentClass = Template->GetClass();
					Info.ComponentTemplate = Template;
					Out.Add(Info);
				}
				
				if (USimpleConstructionScript* SCS = BpgClass->SimpleConstructionScript)
				{
					for (const TArray<USCS_Node*>& Nodes = SCS->GetAllNodes(); USCS_Node* Node : Nodes)
					{
						if (!Node) continue;


						UObject* NodeTemplateObj = Node->ComponentTemplate;
						if (UActorComponent* NodeTemplateComp = Cast<UActorComponent>(NodeTemplateObj))
						{
							FDefaultComponentInfo Info;
							Info.VariableName = Node->GetVariableName();
							Info.TemplateName = NodeTemplateComp->GetName();
							Info.ComponentClass = NodeTemplateComp->GetClass();
							Info.ComponentTemplate = NodeTemplateComp;
							Out.Add(Info);
						}
					}
				}
			}

			CurrentClass = CurrentClass->GetSuperClass();
		}

		for (const FDefaultComponentInfo& I : Out)
		{
			bool bFound = false;
			for (const FDefaultComponentInfo& U : UniqueOut)
			{
				if (U.VariableName == I.VariableName && U.ComponentClass == I.ComponentClass)
				{
					bFound = true;
					break;
				}
			}
			if (!bFound) UniqueOut.Add(I);
		}
	}
	
	return UniqueOut;
}

FString UFusionHelpers::GetTypesHeader(const UFusionClient* Client, TArray<FTypeData>& TypesData)
{
	const TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();

	// Create a JSON array to hold all the class paths
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	
	for (const FTypeData TypeData : TypesData)
	{
		if (const TStrongObjectPtr<UTypeDescriptor> Descriptor = Client->Lookup->HashToDescriptor.FindRef(
			TypeData.TypeRef.Hash))
		{
			FString ClassPath = Descriptor->Type.Get()->GetPathName();
			FUSION_LOG("Build Json for : %s", *ClassPath);

			TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
			JsonObj->SetStringField(TEXT("C"), ClassPath);
			JsonObj->SetStringField(TEXT("N"), TypeData.Object->GetFName().ToString());
			
			JsonArray.Add(MakeShared<FJsonValueObject>(JsonObj));
		}
	}

	RootObject->SetArrayField(TEXT("Types"), JsonArray);

	FString OutputString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject, Writer);
	
	return OutputString;
}


int32 UFusionHelpers::PieConsoleGameIndex() {
#if WITH_EDITOR
	const FString Cmd(FCommandLine::Get());
	
	if (Cmd.Contains("PIEGameUserSettings1")) return 1;
	if (Cmd.Contains("PIEGameUserSettings2")) return 2;
	if (Cmd.Contains("PIEGameUserSettings3")) return 3;
	if (Cmd.Contains("PIEGameUserSettings4")) return 4;
#endif
	return -1;
}

bool UFusionHelpers::IsPieConsoleGame()
{
#if WITH_EDITOR
	return FParse::Param(FCommandLine::Get(), TEXT("PIEVIACONSOLE"));
#else
	return false;
#endif
}

FGuid UFusionHelpers::GetPIEInstanceId()
{
	return InstanceId;
}

EFusionInstanceType UFusionHelpers::GetInstanceType()
{
#if WITH_EDITOR
	if (IsPieConsoleGame()) {
		switch (PieConsoleGameIndex()) {
		case 1: return EFusionInstanceType::PieConsole1;
		case 2: return EFusionInstanceType::PieConsole2;
		case 3: return EFusionInstanceType::PieConsole3;
		case 4: return EFusionInstanceType::PieConsole4;
		default:
			return EFusionInstanceType::Unknown;
		}
		
	}
	
	return EFusionInstanceType::Editor;
#else
	return EFusionInstanceType::Game;
#endif
}

UWorld* UFusionHelpers::GetWorldByName(const FString& WorldName)
{
	if (!GEngine)
	{
		return nullptr;
	}

	auto contexts = GEngine->GetWorldContexts();
	for (const FWorldContext& Context : contexts)
	{
		if (UWorld* World = Context.World())
		{
			if (World->GetName() == WorldName)
			{
				return World;
			}
		}
	}

	return nullptr;
}

bool UFusionHelpers::IsAllowedWorldInstance(UFusionClient* Client, UWorld* World)
{
	if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World))
	{
		return IsAllowedWorldContext(Client, *WorldContext);
	}

	return false;
}

bool UFusionHelpers::IsAllowedWorldContext(UFusionClient* Client, const FWorldContext& WorldContext)
{
	const EFusionInstanceType ContextType = WorldContextType(WorldContext);
	if (ContextType == EFusionInstanceType::Unknown)
		return false;

	if (ContextType != Client->ClientInstanceType)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFusionOnlineSubsystem::OnPostMapLoad: Wrong instance type"));
		return false;
	}
	
	return true;
}

EFusionInstanceType UFusionHelpers::WorldContextType(const FWorldContext& WorldContext)
{ 
	switch (WorldContext.PIEInstance)
	{
	case 0:
		return EFusionInstanceType::Editor;
	case 1:
		return EFusionInstanceType::PieConsole1;
	case 2:
		return EFusionInstanceType::PieConsole2;
	case 3:
		return EFusionInstanceType::PieConsole3;
	case 4:
		return EFusionInstanceType::PieConsole4;
	default:
		return EFusionInstanceType::Unknown;
	}
}

uint32 UFusionHelpers::SafeObjectNameHash(const UObject* Object)
{
	if (!Object)
		return 0;
	
	const FString ObjectName = Object->GetFName().ToString();
	return UFusionHelpers::SafeObjectNameHash(TCHAR_TO_ANSI(*ObjectName), ObjectName.Len());
}

uint32 UFusionHelpers::SafeObjectNameHash(ANSICHAR* String, uint32 Length)
{
	uint32 hash = CityHash32(String, Length);
	return hash >= 0xFFFFFFFD ? hash - 3 : hash;
}

UActorComponent* UFusionHelpers::AddActorComponent(AActor* Actor, TSubclassOf<UActorComponent> ComponentClass)
{
	if (!Actor || !ComponentClass)
		return nullptr;

	if (UWorld* World = Actor->GetWorld())
	{
		if (const UGameInstance* Instance = World->GetGameInstance())
		{
			if (const UFusionOnlineSubsystem* SubSystem = Instance->GetSubsystem<UFusionOnlineSubsystem>())
			{
				if (SubSystem->IsConnected() && !UFusionOnlineSubsystem::IsOwner(Actor))
					return nullptr;
			}
		}
	}

	UActorComponent* NewComponent = NewObject<UActorComponent>(Actor, *ComponentClass);
	if (NewComponent)
	{
		Actor->AddInstanceComponent(NewComponent);
		NewComponent->RegisterComponent();
	}
	return NewComponent;
}

bool UFusionHelpers::DestroyActorComponent(UActorComponent* Component)
{
	if (!Component)
		return false;

	AActor* Owner = Component->GetOwner();
	if (!Owner)
		return false;

	if (UWorld* World = Owner->GetWorld())
	{
		if (const UGameInstance* Instance = World->GetGameInstance())
		{
			if (const UFusionOnlineSubsystem* SubSystem = Instance->GetSubsystem<UFusionOnlineSubsystem>())
			{
				if (SubSystem->IsConnected() && !UFusionOnlineSubsystem::IsOwner(Owner))
					return false;
			}
		}
	}

	Owner->RemoveInstanceComponent(Component);
	Component->DestroyComponent();
	return true;
}