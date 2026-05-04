// Fill out your copyright notice in the Description page of Project Settings.


#include "Types/TypeLookup.h"

#include "CustomTypeDescriptorBuilder.h"
#include "FusionHelpers.h"
#include "PhotonOnlineSubsystemSettings.h"
#include "Kismet/GameplayStatics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UTypeDescriptor* UTypeLookup::CreateTypeDescriptor(UClass* Type, FPropertyBuildOptions& BuildOptions, bool CreateForComponents /* = false */)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::TypeLookup::CreateTypeDescriptor);
	if (!Type)
	{
		return nullptr;
	}

	if (auto Found = ClassDescriptors.FindRef(Type))
	{
		return Found.Get();
	}

	FString ClassId = Type->GetName();
	uint64 TypeHash = CityHash64(TCHAR_TO_ANSI(*ClassId), ClassId.Len());

	for (TPair<TWeakObjectPtr<UStruct>, TObjectPtr<UClass>> Pair : CustomTypeBuilders)
	{
		if (UStruct* TypeKey = Pair.Key.Get(); Type->IsChildOf(TypeKey))
		{
			if (!TypeBuilderInstances.Contains(TypeKey))
			{
				auto BuilderCls = Pair.Value;
				TypeBuilderInstances.Add(TypeKey, TStrongObjectPtr(NewObject<UCustomTypeDescriptorBuilder>(this, BuilderCls)));
			}

			if (TStrongObjectPtr<UCustomTypeDescriptorBuilder> BuilderInstance = TypeBuilderInstances[TypeKey]; BuilderInstance.IsValid())
			{
				TStrongObjectPtr<UTypeDescriptor> CustomDescriptor = BuilderInstance->CreateDescriptor(this, Type, nullptr, BuildOptions);
				HashToDescriptor.Add(CustomDescriptor->TypeHash, CustomDescriptor);
				ClassDescriptors.Add(Type, CustomDescriptor);
				
				return CustomDescriptor.Get();
			}
		}
	}

	TStrongObjectPtr<UTypeDescriptor> Descriptor = TStrongObjectPtr(NewObject<UTypeDescriptor>(this));
	Descriptor->Type = TStrongObjectPtr<UStruct>(Type);
	ClassDescriptors.Add(Type, Descriptor);
	
	FUSION_LOG("Adding Class Type Descriptor: %s", *Type->GetName());
	
	const FString RPC_Prefix = TEXT("__FUSIONRPCEVENT_");
	for (TFieldIterator<FProperty> It(Type); It; ++It) {
		FProperty* Prop = *It;

		//These hidden properties are just to help us find the RPC events/functions.
		if (FString PropertyName = Prop->GetName(); PropertyName.StartsWith(RPC_Prefix))
		{
			AddFunctionLookup(Descriptor, Type, PropertyName.RightChop(RPC_Prefix.Len()));
		}
	}

	for (TFieldIterator<UFunction> It(Type); It; ++It) {
		if (UFunction* Function = *It; Function->FunctionFlags & FUNC_NetMulticast)
		{
			AddFunctionLookup(Descriptor, Type, Function->GetName());
		}
		else if (Function->FunctionFlags & FUNC_NetClient)
		{
			AddFunctionLookup(Descriptor, Type, Function->GetName());
		}
		else if (Function->FunctionFlags & FUNC_NetServer)
		{
			AddFunctionLookup(Descriptor, Type, Function->GetName());
		}
	}
	
	for (TFieldIterator<FProperty> It(Type); It; ++It) {
		FProperty* Prop = *It;

		if (FString PropertyName = Prop->GetName(); !PropertyName.StartsWith(RPC_Prefix))
		{
			AddPropertyToTypeDescriptor(Descriptor.Get(), Prop, BuildOptions);
		}
	}

	if (CreateForComponents)
	{
		TArray<FDefaultComponentInfo> ComponentTypes = UFusionHelpers::GetDefaultOwnerComponents(Type);
	
		for (FDefaultComponentInfo Component : ComponentTypes)
		{
			CreateTypeDescriptor(Component.ComponentClass, BuildOptions);
		}
	}

	//Temp hash generation stuff, we will see what works.
	Descriptor->TypeHash = TypeHash;
	
	HashToDescriptor.Add(Descriptor->TypeHash, Descriptor);

	FUSION_LOG("Added TypeDescriptor for Type: %s (hash: %llu, words: %i)", *ClassId, Descriptor->TypeHash, Descriptor->WordCount);
	
	return Descriptor.Get();
}


UTypeDescriptor* UTypeLookup::CreateTypeStructDescriptor(UStruct* Type, FProperty* ParentProperty, FPropertyBuildOptions& BuildOptions)
{
	if (const TStrongObjectPtr<UTypeDescriptor> Found = StructDescriptors.FindRef(Type); Found.IsValid())
	{
		return Found.Get();
	}

	const FString StructName = Type->GetName();
	const uint64 TypeHash = CityHash64(TCHAR_TO_ANSI(*StructName), StructName.Len());

	for (TPair<TWeakObjectPtr<UStruct>, TObjectPtr<UClass>> Pair : CustomTypeBuilders)
	{
		if (UStruct* TypeKey = Pair.Key.Get(); Type->IsChildOf(TypeKey))
		{
			if (!TypeBuilderInstances.Contains(TypeKey))
			{
				TObjectPtr<UClass> BuilderCls = Pair.Value;
				TypeBuilderInstances.Add(TypeKey, TStrongObjectPtr(NewObject<UCustomTypeDescriptorBuilder>(this, BuilderCls)));
			}

			if (TStrongObjectPtr<UCustomTypeDescriptorBuilder> BuilderInstance = TypeBuilderInstances[TypeKey]; BuilderInstance.IsValid())
			{
				const TStrongObjectPtr<UTypeDescriptor> CustomDescriptor = BuilderInstance->CreateDescriptor(this, Type, ParentProperty, BuildOptions);
				StructDescriptors.Add(Type, CustomDescriptor);
				HashToDescriptor.Add(CustomDescriptor->TypeHash, CustomDescriptor);
			
				return CustomDescriptor.Get();
			}
		}
	}

	const TStrongObjectPtr<UTypeDescriptor> Descriptor = TStrongObjectPtr(NewObject<UTypeDescriptor>(this));
	
	Descriptor->Type = TStrongObjectPtr(Type);
	Descriptor->TypeHash = TypeHash;
	
	StructDescriptors.Add(Type, Descriptor);
	HashToDescriptor.Add(Descriptor->TypeHash, Descriptor);
	
	for (TFieldIterator<FProperty> It(Type); It; ++It)
	{
		//Force build properties when dealing with structs.
		AddPropertyToTypeDescriptor(Descriptor.Get(), *It, BuildOptions);
	}
	
	return Descriptor.Get();
}


bool UTypeLookup::ShouldAddProperty([[maybe_unused]] const UTypeBase* Descriptor, const FProperty* Prop, const FPropertyBuildOptions& BuildOptions)
{
	if (!Prop)
		return false;
	
	if (Prop->GetOwnerClass() == AActor::StaticClass() && Prop->GetName() == "Role")
	{
		return false;
	}

	if (EnumHasAnyFlags(BuildOptions.OptionsFlags, EBuildStructOptions::SkipNotReplicated))
	{
		if ((Prop->GetPropertyFlags() & CPF_RepSkip) != 0)
		{
			return false;
		}
	}

	if (Prop->IsA(FStrProperty::StaticClass()))
	{
		if (!EnumHasAnyFlags(BuildOptions.OptionsFlags, EBuildStructOptions::AddStringProperties))
		{
			return false;
		}
	}

	if (Prop->IsA(FNameProperty::StaticClass()))
	{
		if (!EnumHasAnyFlags(BuildOptions.OptionsFlags, EBuildStructOptions::AddNameProperties))
		{
			return false;
		}
	}
	
	if (EnumHasAnyFlags(BuildOptions.OptionsFlags, EBuildStructOptions::AddDefaultProperties))
	{
		return true;
	}
	
	//Check if replication is enabled on the Property.
	return (Prop->GetPropertyFlags() & CPF_Net) == CPF_Net || (Prop->GetPropertyFlags() & CPF_RepNotify) == CPF_RepNotify;
}

void UTypeLookup::AddPropertyIfExists(const TStrongObjectPtr<UTypeDescriptor>& Descriptor, const UStruct* Type, const UStruct* PropType, const FName PropName)
{
	if (Type->IsChildOf(PropType))
	{
		if (FProperty* Prop = Type->FindPropertyByName(PropName))
		{
			FPropertyBuildOptions BuildOptions {EBuildStructOptions::SkipNotReplicated | EBuildStructOptions::AddDefaultProperties };
			AddPropertyToTypeDescriptor(Descriptor.Get(), Prop, BuildOptions);
		}
	}
}

void UTypeLookup::AddPropertyToTypeDescriptor(UTypeBase* Descriptor, FProperty* Property, FPropertyBuildOptions& BuildOptions)
{
	if (!ShouldAddProperty(Descriptor, Property, BuildOptions))
	{
		return;
	}

	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		//Since we have our own version to replicate component hierarchies we don't want to add this to our replicated properties.
		if (ArrayProp->GetName() != "AttachChildren")
		{
			BuildOptions.ArrayPreAllocSize = GetPreAllocationSize(ArrayProp, BuildOptions.ArrayPreAllocSize);
			
			Descriptor->AddProperty(this, ArrayProp, BuildOptions);
		}
	}
	else if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		Descriptor->AddProperty(ClassProperty, BuildOptions);
	}
	else if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		const EFusionDataTypes DataType = EFusionDataTypeParseCpp(NameProperty->GetCPPType());

		if (const int32 WordCount = EFusionDataTypeWordCount(DataType); WordCount > 0)
		{
			Descriptor->AddProperty(NameProperty, BuildOptions);
		}
	}
	else if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
	{
		const EFusionDataTypes DataType = EFusionDataTypeParseCpp(StrProperty->GetCPPType());

		if (const int32 WordCount = EFusionDataTypeWordCount(DataType); WordCount > 0)
		{
			Descriptor->AddProperty(StrProperty, BuildOptions);
		}
	}
	else if (FNumericProperty* PropNumeric = CastField<FNumericProperty>(Property))
	{
		const EFusionDataTypes DataType = EFusionDataTypeParseCpp(PropNumeric->GetCPPType());
	
		if (const int32 WordCount = EFusionDataTypeWordCount(DataType); WordCount > 0)
		{
			Descriptor->AddProperty(PropNumeric, BuildOptions);
		}
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		EFusionDataTypes DataType = EFusionDataTypes::Unknown;
		if (const FProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty())
		{
			if (UnderlyingProp->IsA(FByteProperty::StaticClass()))
			{
				DataType = EFusionDataTypes::Byte;
			}
			else if (UnderlyingProp->IsA(FIntProperty::StaticClass()))
			{
				DataType = EFusionDataTypes::Int;
			}
			else if (UnderlyingProp->IsA(FUInt32Property::StaticClass()))
			{
				DataType = EFusionDataTypes::UInt;
			}
			else if (UnderlyingProp->IsA(FInt16Property::StaticClass()))
			{
				DataType = EFusionDataTypes::Int16;
			}
			else if (UnderlyingProp->IsA(FUInt16Property::StaticClass()))
			{
				DataType = EFusionDataTypes::UInt16;
			}
			else
			{
				FUSION_LOG("Enum has unknown underlying type: %s", *UnderlyingProp->GetClass()->GetName());
			}
		}
	
		if (const int32 WordCount = EFusionDataTypeWordCount(DataType); WordCount > 0)
		{
			Descriptor->AddProperty(EnumProp, DataType, BuildOptions);
		}
	}
	else if (FStructProperty* PropStruct = CastField<FStructProperty>(Property))
	{
		const EFusionDataTypes DataType = EFusionDataTypeParseCpp(PropStruct->Struct->GetStructCPPName());

		// static const UEnum* EnumPtr = StaticEnum<EFusionDataTypes>();
		// FString test = EnumPtr->GetNameStringByValue((int64)DataType);
		// FUSION_LOG("Replicate Struct Property: %s  with data type: %s", *Property->GetName(), *test)

		if (const int32 WordCount = EFusionDataTypeWordCount(DataType); WordCount > 0)
		{
			Descriptor->AddProperty(PropStruct, BuildOptions);
		}
		else
		{
			//Build struct more verbosely including default none replicated fields.
			FPropertyBuildOptions StructBuildOptions {EBuildStructOptions::SkipNotReplicated | EBuildStructOptions::AddDefaultProperties};
			if (EnumHasAnyFlags(BuildOptions.OptionsFlags, EBuildStructOptions::AddStringProperties))
			{
				StructBuildOptions.OptionsFlags |= EBuildStructOptions::AddStringProperties;
			}
			if (EnumHasAnyFlags(BuildOptions.OptionsFlags, EBuildStructOptions::AddNameProperties))
			{
				StructBuildOptions.OptionsFlags |= EBuildStructOptions::AddNameProperties;
			}
			StructBuildOptions.ArrayPreAllocSize = BuildOptions.ArrayPreAllocSize;
			StructBuildOptions.IsRootTransform = BuildOptions.IsRootTransform;
			
			if (UTypeDescriptor* Sub = CreateTypeStructDescriptor(PropStruct->Struct, PropStruct, StructBuildOptions); Sub->Properties.Num() > 0)
			{
				StructProperty* StructPropertyInstance = new StructProperty(PropStruct);
				
				//Create a struct property with the child properties inside, this ensures toplevel onrep functions will function properly
				for (const ::Property* Prop : Sub->Properties)
				{
					StructPropertyInstance->AddSubProperty(Prop->Clone());
				}

				StructPropertyInstance->DataType = EFusionDataTypes::Struct;
				StructPropertyInstance->Alignment = 2;

				if (StructPropertyInstance->SubProperties.Num() > 0)
				{
					StructPropertyInstance->WordCount = StructPropertyInstance->SubProperties.Last()->WordOffset + StructPropertyInstance->SubProperties.Last()->WordCount;
				}
				else
				{
					StructPropertyInstance->WordCount = 0;
				}

				Descriptor->AddProperty(StructPropertyInstance, PropStruct, BuildOptions);
			}
		}
	}
	else if (FObjectProperty* PropObj = CastField<FObjectProperty>(Property))
	{
		Descriptor->AddProperty(this, PropObj, BuildOptions);
	}
	else if (FWeakObjectProperty* WeakPropObj = CastField<FWeakObjectProperty>(Property))
	{
		Descriptor->AddProperty(this, WeakPropObj, BuildOptions);
	}
	else if (FSoftObjectProperty* SoftPropObj = CastField<FSoftObjectProperty>(Property))
	{
		Descriptor->AddProperty(this, SoftPropObj, BuildOptions);
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		Descriptor->AddProperty(FloatProp, BuildOptions);
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		Descriptor->AddProperty(DoubleProp, BuildOptions);
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		Descriptor->AddProperty(BoolProp, BuildOptions);
	}
	else
	{
		FUSION_LOG_WARN("Unsupported property");
	}
}

void UTypeLookup::AddFunctionLookup(const TStrongObjectPtr<UTypeDescriptor>& Descriptor, const UClass* Class, const FString& EventName)
{
	if (const UFunction* FoundEventFunction = Class->FindFunctionByName(*EventName))
	{
		//Ensure function descriptor gets GCed when the parent descriptor does.
		UFunctionDescriptor* FuncDescriptor = NewObject<UFunctionDescriptor>(Descriptor.Get());

		if (Descriptor->EventFunctions.Find(EventName))
		{
			FUSION_LOG_ERROR("Function: %s already added to descriptor", *EventName);
			return;
		}

		//Store runtime function, since we need this when calling.
		FuncDescriptor->Function = Class->FindFunctionByName(*EventName);
		
		//Build layout of the event function so we know how to invoke the event later on.
		for (TFieldIterator<FProperty> It(FoundEventFunction); It; ++It)
		{
			// Only parameters (exclude locals, return values)
			if (FProperty* Prop = *It; Prop->HasAnyPropertyFlags(CPF_Parm))
			{
				FPropertyBuildOptions StructBuildOptions {
					EBuildStructOptions::SkipNotReplicated |
					EBuildStructOptions::AddDefaultProperties | //Since we are iterating parameters of a function we have to look at everything
					EBuildStructOptions::AddStringProperties | 
					EBuildStructOptions::AddNameProperties
				};

				const int StartRange = FuncDescriptor->Properties.Num();
				AddPropertyToTypeDescriptor(FuncDescriptor, Prop, StructBuildOptions);
				const int EndRange = FuncDescriptor->Properties.Num();

				FFunctionProperty Item{};
				Item.EngineProperty = Prop;
				Item.WordOffset = Prop->GetOffset_ForUFunction();
				Item.WordCount = Prop->GetSize();
				Item.StartRange = StartRange;
				Item.EndRange = EndRange;

				FuncDescriptor->FunctionProperties.Add(Item);
				FuncDescriptor->ParametersSize += Item.WordOffset + Item.WordCount;
				
				FUSION_LOG("Param: %s | Type: %s | Offset: %d | Size: %d",
					*Prop->GetName(),
					*Prop->GetClass()->GetName(),
					Item.WordOffset,
					Item.WordCount
				);
			}
		}

		const FString ClassId = Class->GetName();
		const FString EventString = ClassId + TEXT(":") + EventName;
		const uint64 EventHash = CityHash64(TCHAR_TO_ANSI(*EventString), EventString.Len());
		Descriptor->EventHashToName.Add(EventHash, EventName);
		Descriptor->EventNameToHash.Add(EventName, EventHash);
		Descriptor->EventFunctions.Add(EventName, FuncDescriptor);
		
		FUSION_LOG("Added Event Function: %s", *FoundEventFunction->GetName());
	}
}

int32 UTypeLookup::GetReplicatedActorTypeLayout(UClass* Type, TArray<SharedMode::TypeRef>& Types)
{
	uint32 Offset = 0;

	FPropertyBuildOptions BuildOptions = GetDefaultBuildOptions();

	const UTypeDescriptor* ActorType = CreateTypeDescriptor(Type, BuildOptions);
	Types.Add(SharedMode::TypeRef{ActorType->TypeHash, Offset});
	Offset += ActorType->WordCount;

	for (TArray<FDefaultComponentInfo> ComponentTypes = UFusionHelpers::GetDefaultOwnerComponents(Type);
	     FDefaultComponentInfo Component : ComponentTypes)
	{
		const UTypeDescriptor* ComponentType = CreateTypeDescriptor(Component.ComponentClass, BuildOptions);
		Types.Add(SharedMode::TypeRef{ComponentType->TypeHash, Offset});
		Offset += ComponentType->WordCount;
	}

	return Offset;
}

int32 UTypeLookup::EnsureDefaultActorTypeDescriptor(UClass* Type)
{
	FPropertyBuildOptions BuildOptions = GetDefaultBuildOptions();

	CreateTypeDescriptor(Type, BuildOptions, true);
	TArray<SharedMode::TypeRef> ActorTypes{};
	return GetReplicatedActorTypeLayout(Type, ActorTypes);
}


UTypeDescriptor* UTypeLookup::FindClassDescriptor(const UStruct* Type)
{
	if (const TStrongObjectPtr<UTypeDescriptor> Found = ClassDescriptors.FindRef(Type); Found.IsValid())
	{
		return Found.Get();
	}

	return nullptr;
}

UTypeDescriptor* UTypeLookup::FindClassDescriptor(const uint64 Type)
{
	if (const TStrongObjectPtr<UTypeDescriptor> Found = HashToDescriptor.FindRef(Type))
	{
		return Found.Get();
	}

	return nullptr;
}

UTypeDescriptor* UTypeLookup::FindStructDescriptor(const UStruct* Type)
{
	if (const TStrongObjectPtr<UTypeDescriptor> Found = StructDescriptors.FindRef(Type); Found.IsValid())
	{
		return Found.Get();
	}

	return nullptr;
}


void UTypeLookup::RegisterTypeBuilder(UStruct* Target, UClass* Builder)
{
	if (!Target)
		return;

	if (!Builder)
		return;
	
	if (!CustomTypeBuilders.Contains(Target))
	{
		CustomTypeBuilders.Add(Target, Builder);
	}
}

void UTypeLookup::UnRegisterTypeBuilder(UStruct* Target)
{
	if (!Target)
		return;
	
	if (!CustomTypeBuilders.Contains(Target))
	{
		CustomTypeBuilders.Remove(Target);
	}
}


void UTypeLookup::Destroy()
{
	for (const TPair<const UStruct*, TStrongObjectPtr<UTypeDescriptor>>& Pair : ClassDescriptors)
	{
		TStrongObjectPtr<UTypeDescriptor> Descriptor = Pair.Value;
		Descriptor.Reset();
	}

	for (const TPair<const UStruct*, TStrongObjectPtr<UTypeDescriptor>>& Pair : StructDescriptors)
	{
		TStrongObjectPtr<UTypeDescriptor> Descriptor = Pair.Value;
		Descriptor.Reset();
	}
	
	for (const TPair<TWeakObjectPtr<UStruct>, TStrongObjectPtr<UCustomTypeDescriptorBuilder>>& Pair : TypeBuilderInstances)
	{
		TStrongObjectPtr<UCustomTypeDescriptorBuilder> Descriptor = Pair.Value;
		Descriptor.Reset();
	}
	
	HashToDescriptor.Empty();
	StructDescriptors.Empty();
	ClassDescriptors.Empty();
	TypeBuilderInstances.Empty();
}

FPropertyBuildOptions UTypeLookup::GetDefaultBuildOptions()
{
	const UPhotonOnlineSubsystemSettings* Settings = UPhotonOnlineSubsystemSettings::GetPhotonOnlineSettings();
	int PreAllocSize = Settings ? Settings->DefaultArraySize : 8;
	
	return FPropertyBuildOptions {
		EBuildStructOptions::SkipNotReplicated |
		EBuildStructOptions::AddStringProperties | 
		EBuildStructOptions::AddNameProperties,
		PreAllocSize
	};
}
