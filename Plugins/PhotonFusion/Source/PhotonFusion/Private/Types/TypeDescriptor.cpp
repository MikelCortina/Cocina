// Copyright 2026 Exit Games GmbH. All Rights Reserved.
// ReSharper disable CppUnusedIncludeDirective

#include "Types/TypeDescriptor.h"

#include "FusionClient.h"
#include "FusionUtils.h"
#include "FusionHelpers.h"
#include "CoreMinimal.h"
#include "CustomTypeDescriptorBuilder.h"
#include "Misc/AssertionMacros.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Engine/ContentEncryptionConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/MovementComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "UObject/UnrealType.h"
#include "Misc/EngineVersionComparison.h"
#include "PhotonOnlineSubsystemSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
// ReSharper restore CppUnusedIncludeDirective

TMap<FString, FFusionArrayHooks> UTypeDescriptorLibrary::HooksMap;

int32 EFusionDataTypeByteSize(const EFusionDataTypes Type)
{
	switch (Type)
	{
	case EFusionDataTypes::Bool:
	case EFusionDataTypes::Byte:
		return 1;

	case EFusionDataTypes::Int16:
	case EFusionDataTypes::UInt16:
		return 2;
	
	case EFusionDataTypes::Int:
	case EFusionDataTypes::UInt:
	case EFusionDataTypes::Float:
		return 4;
	
	case EFusionDataTypes::Double:
	case EFusionDataTypes::Int64:
	case EFusionDataTypes::UInt64:
		return 8;

	case EFusionDataTypes::Quat:
		return sizeof(FQuat);

	case EFusionDataTypes::Vector:
		return sizeof(FVector);

	case EFusionDataTypes::Rotator:
		return sizeof(FRotator);

	default:
		return 0;
	}
}

int32 EFusionDataTypeWordCount(const EFusionDataTypes Type)
{
	switch (Type)
	{
	case EFusionDataTypes::Byte:
	case EFusionDataTypes::Int:
	case EFusionDataTypes::UInt:
	case EFusionDataTypes::Int16:
	case EFusionDataTypes::UInt16:
	case EFusionDataTypes::Float:
	case EFusionDataTypes::Bool:
		return 1;

	case EFusionDataTypes::Double:
	case EFusionDataTypes::Int64:
	case EFusionDataTypes::UInt64:
		return 2;
		
	case EFusionDataTypes::ObjectId:
	case EFusionDataTypes::ActorId:
		return (sizeof(SharedMode::StringHandle) / 4) + 1; //Allows us to encode the type of object on the first word.

	case EFusionDataTypes::Quat:
		return sizeof(FQuat) / 4;

	case EFusionDataTypes::Vector:
		return sizeof(FVector) / 4;

	case EFusionDataTypes::Rotator:
		return sizeof(FRotator) / 4;

	case EFusionDataTypes::Name:
		return 2; //Holds string lookup address.

	case EFusionDataTypes::String:
		return 2; //Holds string lookup address.
		
	case EFusionDataTypes::ClassId:
		return 2; //Holds string lookup address.

	default:
		return 0;
	}
}

EFusionDataTypes EFusionDataTypeParseCpp(const FString& Name)
{
	if (Name == "int" || Name == "int32")
	{
		return EFusionDataTypes::Int;
	}

	if (Name == "uint" || Name == "uint32")
	{
		return EFusionDataTypes::UInt;
	}

	if (Name == "int16")
	{
		return EFusionDataTypes::Int16;
	}

	if (Name == "uint16")
	{
		return EFusionDataTypes::UInt16;
	}

	if (Name == "float")
	{
		return EFusionDataTypes::Float;
	}

	if (Name == "byte" || Name == "uint8" || Name.StartsWith("TEnumAsByte<"))
	{
		return EFusionDataTypes::Byte;
	}

	if (Name == "bool")
	{
		return EFusionDataTypes::Bool;
	}

	if (Name == "FVector")
	{
		return EFusionDataTypes::Vector;
	}

	if (Name == "FQuat")
	{
		return EFusionDataTypes::Quat;
	}

	if (Name == "FRotator")
	{
		return EFusionDataTypes::Rotator;
	}

	if (Name == "int64")
	{
		return EFusionDataTypes::Int64;
	}

	if (Name == "uint64")
	{
		return EFusionDataTypes::UInt64;
	}

	if (Name == "double")
	{
		return EFusionDataTypes::Double;
	}

	if (Name == "FName")
	{
		return EFusionDataTypes::Name;
	}

	if (Name == "FString")
	{
		return EFusionDataTypes::String;
	}

	return EFusionDataTypes::Unknown;
}

void UTypeBase::AddProperty(FFloatProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	Property* Item = new Property{};
	Item->DataType = EFusionDataTypeParseCpp(Prop->GetCPPType());
	Item->Alignment = std::max(1, Prop->GetMinAlignment() / 4);
	AddProperty(Item, Prop, BuildOptions);
}

void UTypeBase::AddProperty(FDoubleProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	Property* Item = new Property{};
	Item->DataType = EFusionDataTypeParseCpp(Prop->GetCPPType());
	Item->Alignment = std::max(2, Prop->GetMinAlignment() / 4);
	AddProperty(Item, Prop, BuildOptions);
}

void UTypeBase::AddProperty(FEnumProperty* Prop, EFusionDataTypes DataType, FPropertyBuildOptions& BuildOptions)
{
	Property* Item = new Property{};
	Item->DataType = DataType;
	Item->Alignment = std::max(2, Prop->GetMinAlignment() / 4);
	AddProperty(Item, Prop, BuildOptions);
}







void UTypeBase::AddProperty(FClassProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	Property* Item = new Property{};
	Item->Alignment = 2;
	Item->DataType = EFusionDataTypes::ClassId;
	AddProperty(Item, Prop, BuildOptions);
}

void UTypeBase::AddProperty(FNameProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	Property* Item = new Property{};
	Item->DataType = EFusionDataTypeParseCpp(Prop->GetCPPType());
	Item->Alignment = std::max(2, Prop->GetMinAlignment() / 4);
	AddProperty(Item, Prop, BuildOptions);
}

void UTypeBase::AddProperty(FStrProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	Property* Item = new Property{};
	Item->DataType = EFusionDataTypeParseCpp(Prop->GetCPPType());
	Item->Alignment = std::max(2, Prop->GetMinAlignment() / 4);
	AddProperty(Item, Prop, BuildOptions);
}


void UTypeBase::AddProperty(FNumericProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	Property* Item = new Property{};
	Item->DataType = EFusionDataTypeParseCpp(Prop->GetCPPType());
	Item->Alignment = std::max(1, Prop->GetMinAlignment() / 4);
	AddProperty(Item, Prop, BuildOptions);
}

void UTypeBase::AddProperty(FStructProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	Property* Item = new Property{};
	Item->DataType = EFusionDataTypeParseCpp(Prop->Struct->GetStructCPPName());
	Item->Alignment = std::max(1, Prop->Struct.Get()->GetMinAlignment() / 4);
	AddProperty(Item, Prop, BuildOptions);
}

void UTypeBase::AddProperty(UTypeLookup* Lookup, FObjectProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	HandleObjectProperty(Lookup, Prop, EObjectPointerType::ObjectPointer, BuildOptions);
}

void UTypeBase::AddProperty(UTypeLookup* Lookup, FWeakObjectProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	HandleObjectProperty(Lookup, Prop, EObjectPointerType::WeakObjectPointer, BuildOptions);
}

void UTypeBase::AddProperty(UTypeLookup* Lookup, FSoftObjectProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	HandleObjectProperty(Lookup, Prop, EObjectPointerType::SoftObjectPointer, BuildOptions);
}

void UTypeBase::HandleObjectProperty(UTypeLookup* Lookup, FObjectPropertyBase* Prop, EObjectPointerType PointerType, FPropertyBuildOptions& BuildOptions)
{
	ObjectProperty* Item = new ObjectProperty{};
	Item->DataType = EFusionDataTypes::ObjectId;
	Item->Alignment = 2;
	Item->WordCount = EFusionDataTypeWordCount(Item->DataType);
	Item->PointerType = PointerType;

	AddProperty(Item, Prop, BuildOptions);
}

void UTypeBase::AddFusionNetworkArrayProperty(UTypeLookup* Lookup, FArrayProperty* ArrayProp, FProperty* ParentContainerProperty, int MaxItems, FPropertyBuildOptions& BuildOptions)
{
	FusionArrayProperty* Item = new FusionArrayProperty(MaxItems, ParentContainerProperty);
	Item->DataType = EFusionDataTypes::FusionArray;
	Item->Alignment = std::max(1, ArrayProp->GetMinAlignment() / 4);
	Item->WordCount = 0;
	
	if (BuildArrayItem(Lookup, ArrayProp, Item, MaxItems))
	{
		Item->WordCount += 1; //Extra word to hold array size at index 0
		AddProperty(Item, ArrayProp, BuildOptions);
	
		Item->Interpolated = false;
		FUSION_LOG("Add Fusion Array Property: %s  WordCount: %d", *ArrayProp->GetName(), Item->WordCount);
	}
	else
	{
		FUSION_LOG_ERROR("Failed To Add Fusion Array Property: %s", *ArrayProp->GetName());
		delete Item;
	}
}

void UTypeBase::AddProperty(UTypeLookup* Lookup, FArrayProperty* ArrayProp, FPropertyBuildOptions& BuildOptions)
{
	ArrayProperty* Item = new ArrayProperty(BuildOptions.ArrayPreAllocSize);
	Item->DataType = EFusionDataTypes::Array;
	Item->Alignment = std::max(1, ArrayProp->GetMinAlignment() / 4);
	Item->WordCount = 0;
	
	if (BuildArrayItem(Lookup, ArrayProp, Item, BuildOptions.ArrayPreAllocSize))
	{
		Item->WordCount += 1; //Extra word to hold array size at index 0
		AddProperty(Item, ArrayProp, BuildOptions);
	
		Item->Interpolated = false;
		FUSION_LOG("Add Fusion Array Property: %s  WordCount: %d", *ArrayProp->GetName(), Item->WordCount);
	}
	else
	{
		FUSION_LOG_ERROR("Failed To Add Fusion Array Property: %s", *ArrayProp->GetName());
		delete Item;
	}
}

void UTypeBase::AddProperty(FBoolProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	Property* Item = new Property{};

	Item->DataType = EFusionDataTypes::Bool;
	Item->Alignment = 1;

	AddProperty(Item, Prop, BuildOptions);
}

void UTypeBase::AddProperty(Property* Item, FProperty* Prop, FPropertyBuildOptions& BuildOptions)
{
	Item->EngineProperty = Prop;

	if (Item->WordCount == 0)
	{
		Item->WordCount = EFusionDataTypeWordCount(Item->DataType);
	}

	if (Item->Alignment > 1)
	{
		// align container type word count
		if (const uint32 Diff = WordCount % Item->Alignment; Diff != 0)
		{
			WordCount += Item->Alignment - Diff;
		}


		// align items word count
		if (const int32 Diff = Item->WordCount % Item->Alignment; Diff != 0)
		{
			Item->WordCount += Item->Alignment - Diff;
		}
	}
	
	// set offset to current container word count
	Item->WordOffset = WordCount;
	if ((Prop->GetPropertyFlags() & CPF_RepNotify) == CPF_RepNotify)
	{
		if (Prop->RepNotifyFunc.ToString().Contains("OnRep_Transform"))
		{
			Item->IsTransformProperty = true;
		}
		
		for (TFieldIterator<UFunction> It(Type.Get(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			UFunction* Function = *It;

			if (Function->GetName() == Prop->RepNotifyFunc)
			{
				Item->RepNotify = Function;
			}
		}
	}

	Item->Interpolated = true;
	Item->ItemId = FGuid::NewGuid();
	
	// 
	Properties.Add(Item);

	// 
	WordCount += Item->WordCount;
}

bool UTypeBase::BuildArrayItem(UTypeLookup* Lookup, const FArrayProperty* ArrayProp, ArrayProperty* ArrayItem, const int MaxItems)
{
	//Build mapping for the array entries properties.
	FProperty* ElementProp = ArrayProp->Inner;
	
	if (FStructProperty* StructProp = CastField<FStructProperty>(ElementProp))
	{
		FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
		BuildOptions.OptionsFlags |= EBuildStructOptions::AddDefaultProperties;
		
		UStruct* ElementStructType = StructProp->Struct;
		if (UTypeDescriptor* ArrayElementDescriptor = Lookup->CreateTypeStructDescriptor(ElementStructType, StructProp, BuildOptions))
		{
			StructProperty* StructPropertyInstance = new StructProperty(StructProp);
				
			//Create a struct property with the child properties inside, this ensures toplevel onrep functions will function properly
			for (const Property* Prop : ArrayElementDescriptor->Properties)
			{
				StructPropertyInstance->AddSubProperty(Prop->Clone());
			}

			StructPropertyInstance->DataType = EFusionDataTypes::Struct;
			StructPropertyInstance->Alignment = 1;
			if (StructPropertyInstance->SubProperties.Num() > 0)
			{
				StructPropertyInstance->WordCount = StructPropertyInstance->SubProperties.Last()->WordOffset + StructPropertyInstance->SubProperties.Last()->WordCount;
			}
			else
			{
				StructPropertyInstance->WordCount = 0;
			}

			StructPropertyInstance->EngineProperty = StructProp;
			
			ArrayItem->WordCount = StructPropertyInstance->WordCount * MaxItems;
			
			ArrayItem->AddSubProperty(StructPropertyInstance);
		}

		return true;
	}
	
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(ElementProp))
	{
		ArrayItem->SkipResolve = true;
		
		Property* SubItem;
		int ItemWordSize;
		
		if (ObjProp->PropertyClass->IsChildOf(AActor::StaticClass()))
		{
			ObjectProperty* Item = new ObjectProperty{};
			Item->DataType = EFusionDataTypes::ActorId;
			Item->Alignment = 1;
			Item->WordCount = EFusionDataTypeWordCount(Item->DataType); //Add for path size.
			Item->PointerType = EObjectPointerType::ObjectPointer;
			
			ItemWordSize = Item->WordCount;
			SubItem = Item;
		}
		else
		{
			ObjectProperty* Item = new ObjectProperty{};
			Item->DataType = EFusionDataTypes::ObjectId;
			Item->Alignment = 1;
			Item->WordCount = EFusionDataTypeWordCount(Item->DataType); //Add for path size.

			ItemWordSize = Item->WordCount;
			SubItem = Item;
		}
		
		// 
		SubItem->EngineProperty = ObjProp;
		SubItem->SkipResolve = true;

		ArrayItem->WordCount = ItemWordSize * MaxItems;
		ArrayItem->AddSubProperty(SubItem);

		return true;
	}

	if (const EFusionDataTypes DataType = EFusionDataTypeParseCpp(ElementProp->GetCPPType()); DataType != EFusionDataTypes::Unknown)
	{
		Property* SubItem  = new Property{};
		SubItem->DataType = DataType;
		SubItem->Alignment = 1;
		SubItem->WordCount = EFusionDataTypeWordCount(SubItem->DataType);
		SubItem->EngineProperty = ElementProp;
		SubItem->SkipResolve = true;

		ArrayItem->WordCount = SubItem->WordCount * MaxItems;
		ArrayItem->AddSubProperty(SubItem);

		return true;
	}

	return false;
}


UTypeDescriptor::~UTypeDescriptor()
{
	while (Properties.Num() > 0)
	{
		const Property* Property = Properties[0];
		Properties.RemoveAt(0);
		delete Property;
	}
	
	Properties.Empty();
}

void UFunctionDescriptor::SerializeParams(void* Container, const int PropertyIndex, TArray<uint8>& Buffer, const UObject* Source)
{
	const FString RPCName = Function ? Function->GetName() : "";
	checkf(PropertyIndex < Properties.Num(), TEXT("RPC: %s  Trying to access index: %d when serializing rpc parameters, max property index is: %d "), *RPCName, PropertyIndex, Properties.Num()-1);

	const FFunctionProperty FunctionProperty = FunctionProperties[PropertyIndex];
	checkf(FunctionProperty.EngineProperty != nullptr, TEXT("Property was null when trying to serialize rpc parameters."));

	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(Source);
	const UFusionOnlineSubsystem* OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();

	for (int i = FunctionProperty.StartRange; i < FunctionProperty.EndRange; i++)
	{
		Property* Property = Properties[i];
		check(Property);
		Property->Serialize(OnlineSubsystem->GFusionClient, Container, FunctionProperty, Buffer, true);
	}
}

void UFunctionDescriptor::DeserializeParams(UFusionClient* Client, const SharedMode::Rpc& Rpc, void* Params)
{
	uint8* Container = static_cast<uint8*>(Params);
	int CurrentOffset = 0;

	for (FFunctionProperty& FunctionProperty : FunctionProperties)
	{
		//Always zero out struct properties so they dont have garbage/uninitialized values.
		if (FunctionProperty.EngineProperty[0].IsA(FStructProperty::StaticClass()))
		{
			const int32 StructSize = FunctionProperty.EngineProperty[0].GetSize();
			const int32 Offset = FunctionProperty.EngineProperty[0].GetOffset_ForUFunction();
			void* StructPtr = Container + Offset;
			FMemory::Memzero(StructPtr, StructSize);
		}
		
		for (int i = FunctionProperty.StartRange; i < FunctionProperty.EndRange; i++)
		{
			Property* Property = Properties[i];
			check(Property);
			
			Property->Deserialize(Client, Rpc.Bytes.Ptr, Container, FunctionProperty, CurrentOffset);
		}
	}
}

