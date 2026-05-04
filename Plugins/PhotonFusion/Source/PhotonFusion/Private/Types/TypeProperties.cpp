// Copyright 2026 Exit Games GmbH. All Rights Reserved.
// ReSharper disable CppUnusedIncludeDirective

#include "Types/TypeProperties.h"

#include <Fusion/LogUtils.h>

#include "Misc/AssertionMacros.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "FusionClient.h"
#include "FusionShared.h"
#include "FusionUtils.h"
#include "UObject/UObjectBaseUtility.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Types/TypeDescriptor.h"
#include "Misc/EngineVersionComparison.h"
#include "Types/PropertyHelpers.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/GameEngine.h"
// ReSharper restore CppUnusedIncludeDirective

static int32 CompressFloat(float F)
{
	return *reinterpret_cast<uint32*>(&F);
}

static float DecompressFloat(int32 F)
{
	return *reinterpret_cast<float*>(&F);
}

static FString ReadString(uint8_t* Buffer, int32& CurrentOffset)
{
	int32 StringSize = 0;
	FMemory::Memcpy(&StringSize, Buffer + CurrentOffset, sizeof(int32));

	//Place offset at start of the string.
	CurrentOffset += sizeof(int32);

	const TCHAR* StringData = reinterpret_cast<const TCHAR*>(Buffer + CurrentOffset);
	const FString Path(StringSize, StringData);
	
	//Set offset at the next property after the string data.
	const int32 StringByteSize = StringSize * sizeof(TCHAR);
	CurrentOffset += StringByteSize;

	return Path;
}

static void AddString(TArray<uint8>& Buffer, FString StrValue)
{
	const int32 CharCount = StrValue.Len();

	const int32 StringSize = CharCount * sizeof(TCHAR);
	const int32 BytesToAdd = sizeof(int32) + StringSize; //Size of string integer + size of the bytes containing the string
	int32 Offset = Buffer.AddUninitialized(BytesToAdd);

	//Copy in the string size when we read this on the other side.
	FMemory::Memcpy(Buffer.GetData() + Offset, &CharCount, sizeof(int32));

	if (StringSize > 0)
	{
		Offset += sizeof(int32);
		FMemory::Memcpy(Buffer.GetData() + Offset, StrValue.GetCharArray().GetData(), StringSize);
	}
}

void AddOneInt(TArray<uint8>& Buffer, const uint32 Value)
{
	constexpr int32 BytesToAdd = sizeof(int32);
	int32 Offset = Buffer.AddUninitialized(BytesToAdd);

	FMemory::Memcpy(Buffer.GetData() + Offset, &Value, sizeof(int32));
}

void AddTwoInts(TArray<uint8>& Buffer, const uint32 IntOne, const uint32 IntTwo)
{
	constexpr int32 BytesToAdd = sizeof(int32) + sizeof(int32);
	int32 Offset = Buffer.AddUninitialized(BytesToAdd);

	FMemory::Memcpy(Buffer.GetData() + Offset, &IntOne, sizeof(int32));

	Offset += sizeof(int32);

	FMemory::Memcpy(Buffer.GetData() + Offset, &IntTwo, sizeof(int32));
}

void AddObjectType(TArray<uint8>& Buffer, const EEncodedObjectType Type)
{
	int32 Offset = Buffer.AddUninitialized( sizeof(int32));

	FMemory::Memcpy(Buffer.GetData() + Offset, &Type, sizeof(int32));
}

FString GetSerializedLevelPath(const UObject* Owner, const UActorComponent* ActorComponent)
{
	const unsigned int ObjectHash = UFusionHelpers::SafeObjectNameHash(Owner);
	//const unsigned int Hash = UFusionHelpers::GetObjectHash(Owner);
	const FString ComponentName = ActorComponent ? ActorComponent->GetName() : "";

	FString SerializedLevelPath = FString::Printf(TEXT("%u:%s"), ObjectHash, *ComponentName);
	return SerializedLevelPath;
}

void Property::CopyTo(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words) const
{
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	ResolveContainerAndProperty(Container, ResolvedContainer, ResolvedProperty);

	if (!ResolvedProperty || !ResolvedContainer)
	{
		return;
	}

	// ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
	// ReSharper disable once CppIncompleteSwitchStatement
	switch (DataType)
	{
	case EFusionDataTypes::Bool:
		Words[0] = CastField<FBoolProperty>(ResolvedProperty)->GetPropertyValue(ResolvedContainer) ? 1 : 0;
		break;

	case EFusionDataTypes::Byte:
		*reinterpret_cast<uint8*>(Words) = *static_cast<uint8*>(ResolvedContainer);
		break;

	case EFusionDataTypes::Double:
	case EFusionDataTypes::UInt64:
	case EFusionDataTypes::Int64:
		*reinterpret_cast<uint64*>(Words) = *static_cast<uint64*>(ResolvedContainer);
		break;

	case EFusionDataTypes::Int:
	case EFusionDataTypes::UInt:
	case EFusionDataTypes::Float:
		{
			Words[0] = *static_cast<int32*>(ResolvedContainer);
			break;
		}

	case EFusionDataTypes::Int16:
		{
			*reinterpret_cast<int16*>(Words) = *static_cast<int16*>(ResolvedContainer);
			break;
		}
	case EFusionDataTypes::UInt16:
		{
			*reinterpret_cast<uint16*>(Words) = *static_cast<uint16*>(ResolvedContainer);
			break;
		}

	case EFusionDataTypes::ObjectId:
		checkf(false, TEXT("Object Properties should never hit this, they override CopyTo"));
		break;

	case EFusionDataTypes::Vector:
		{
			const FVector* v = static_cast<FVector*>(ResolvedContainer);
			Words[0] = CompressFloat(v->X);
			Words[1] = CompressFloat(v->Y);
			Words[2] = CompressFloat(v->Z);
		}
		break;

	case EFusionDataTypes::Rotator:
		{
			const FRotator* r = static_cast<FRotator*>(ResolvedContainer);
			Words[0] = CompressFloat(r->Pitch);
			Words[1] = CompressFloat(r->Yaw);
			Words[2] = CompressFloat(r->Roll);
		}
		break;

	case EFusionDataTypes::Quat:
		{
			const FQuat* q = static_cast<FQuat*>(ResolvedContainer);
			float* WordsF = reinterpret_cast<float*>(Words);

			WordsF[0] = static_cast<float>(q->X);
			WordsF[1] = static_cast<float>(q->Y);
			WordsF[2] = static_cast<float>(q->Z);
			WordsF[3] = static_cast<float>(q->W);
		}
		break;

	case EFusionDataTypes::ClassId:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::CopyTo::Property::ClassId);
			if (const FClassProperty* ClassProp = static_cast<FClassProperty*>(ResolvedProperty))
			{
				const SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(Words);
				FString Path = "";

				if (const TObjectPtr<UObject> ClassValue = ClassProp->GetPropertyValue(ResolvedContainer))
				{
					//FUSION_LOG("ClassProp: %s", *ClassValue->GetName());
					if (UClass* StoredClass = Cast<UClass>(ClassValue))
					{
						Path = StoredClass->GetPathName();

						if (!Client->Lookup->FindClassDescriptor(StoredClass))
						{
							FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();

							if (const UTypeDescriptor* NewDescriptor = Client->Lookup->CreateTypeDescriptor(
								StoredClass, BuildOptions))
							{
								FUSION_LOG("Create new class descriptor: %s", *NewDescriptor->Type->GetName());
							}
							//FUSION_LOG("Found class descriptor: %s", *StoredClass->GetName());
						}
					}
				}

				const SharedMode::StringHandle Handle = UPropertyHelpers::EncodeString(Context.Pair.Object, Path, *ExistingHandle);
				std::memcpy(Words, &Handle, sizeof(SharedMode::StringHandle));
			}
		}
		break;
	case EFusionDataTypes::Name:
	case EFusionDataTypes::String:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::CopyTo::Property::String);
			FString StoredString;

			if (ResolvedProperty->IsA(FNameProperty::StaticClass()))
			{
				const FNameProperty* NameProp = static_cast<FNameProperty*>(ResolvedProperty);
				const FName StoredName = NameProp->GetPropertyValue(ResolvedContainer);
				StoredString = StoredName.ToString();
			}
			else if (ResolvedProperty->IsA(FStrProperty::StaticClass()))
			{
				const FStrProperty* StrProp = static_cast<FStrProperty*>(ResolvedProperty);
				StoredString = StrProp->GetPropertyValue(ResolvedContainer);
			}

			const SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(Words);
			const SharedMode::StringHandle Handle = UPropertyHelpers::EncodeString(Context.Pair.Object, StoredString, *ExistingHandle);

			Words[0] = Handle.id;
			Words[1] = Handle.generation;
		}
		break;

	case EFusionDataTypes::Array:
	case EFusionDataTypes::FusionArray:
		checkf(false, TEXT("Array Properties should never hit this, they override CopyTo"));
		break;
	}
}

void Property::CleanupPreviousState(UFusionClient* Client, const FCopyContext& Context, void* Container, int* Words)
{
	// ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
	// ReSharper disable once CppIncompleteSwitchStatement
	switch (DataType)
	{
	case EFusionDataTypes::ClassId:
	case EFusionDataTypes::Name:
	case EFusionDataTypes::String:
		{
			const SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(Words);
			const SharedMode::StringHandle Handle = Context.Pair.Object->FreeString(*ExistingHandle);
			std::memcpy(Words, &Handle, sizeof(SharedMode::StringHandle)); //Make sure to wipe the state handle.
		}
		break;
	}
}

bool Property::CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow, bool Interpolating) const
{
	bool Changed = false;
	
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	ResolveContainerAndProperty(Container, ResolvedContainer, ResolvedProperty);
	if (!ResolvedContainer || !ResolvedProperty)
	{
		return false;
	}

	// ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
	// ReSharper disable once CppIncompleteSwitchStatement
	switch (DataType)
	{
	case EFusionDataTypes::Bool:
		{
			bool NewVal = *reinterpret_cast<bool*>(Words);
			FBoolProperty* BoolProp = CastField<FBoolProperty>(ResolvedProperty);
			bool Current = BoolProp->GetPropertyValue(ResolvedContainer);
			if (NewVal != Current)
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, &Current);
				}
				
				BoolProp->SetPropertyValue(ResolvedContainer, NewVal);
				Changed = true;
			}
		}
		break;
		
	case EFusionDataTypes::Byte:
		{
			if (uint8* Ptr = static_cast<uint8*>(ResolvedContainer); *Ptr != *reinterpret_cast<uint8*>(Words))
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, ResolvedContainer);
				}
				
				*Ptr = *reinterpret_cast<uint8*>(Words);
				Changed = true;
			}
		}
		break;

	case EFusionDataTypes::UInt64:
	case EFusionDataTypes::Int64:
		{
			if (uint64* PreviousValue = static_cast<uint64*>(ResolvedContainer); *PreviousValue != *reinterpret_cast<uint64*>(Words))
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, ResolvedContainer);
				}
				
				*PreviousValue = *reinterpret_cast<uint64*>(Words);
				Changed = true;
			}
		}
		break;

	case EFusionDataTypes::Int:
	case EFusionDataTypes::UInt:
		{
			if (int32* PreviousValue = static_cast<int32*>(ResolvedContainer); *static_cast<int32*>(ResolvedContainer) != Words[0])
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, ResolvedContainer);
				}
				
				*PreviousValue = Words[0];
				Changed = true;
			}
		}
		break;
		
	case EFusionDataTypes::Int16:
		{
			if (int16* PreviousValue = static_cast<int16*>(ResolvedContainer); *PreviousValue != *reinterpret_cast<int16*>(Words))
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, ResolvedContainer);
				}
				
				*PreviousValue = *reinterpret_cast<int16*>(Words);
				Changed = true;
			}
		}
		break;

	case EFusionDataTypes::UInt16:
		{
			if (uint16* PreviousValue = static_cast<uint16*>(ResolvedContainer); *PreviousValue != *reinterpret_cast<uint16*>(Words))
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, ResolvedContainer);
				}
				
				*PreviousValue = *reinterpret_cast<uint16*>(Words);
				Changed = true;
			}
		}
		break;
		
	case EFusionDataTypes::Float:
		{
			if (int32* Ptr = static_cast<int32*>(ResolvedContainer); *Ptr != Words[0])
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, ResolvedContainer);
				}

				if (!Interpolating)
				{
					*Ptr = Words[0];
					Changed = true;
				}
			}
		}

		break;
		
	case EFusionDataTypes::Double:
		{
			if (uint64* Ptr = static_cast<uint64*>(ResolvedContainer); *Ptr != *reinterpret_cast<uint64*>(Words))
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, ResolvedContainer);
				}

				if (!Interpolating)
				{
					*Ptr = *reinterpret_cast<uint64*>(Words);
					Changed = true;
				}
			}
		}
		break;

	case EFusionDataTypes::ClassId:
		{
			SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(Words);

			SharedMode::StringMessage OutStringStatus{};
			const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
			EncodedStringStatus Status = UPropertyHelpers::GetEncodedStringStatus(OutStringStatus);
			
			if (Status != EncodedStringStatus::Valid)
			{
				FString StatusString = StaticEnum<EncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
				FUSION_LOG_ERROR("Encoded string has error: %s", *StatusString);
				return false;
			}

			if (OutStringStatus ==  SharedMode::StringMessage::EmptyHeap)
			{
				return false;
			}

			bool EmptyString = ExistingHandle->id == UINT32_MAX;
			const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> PtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(StringPtr));

			if (StringPtr || EmptyString)
			{
				FClassProperty* ClassProp = static_cast<FClassProperty*>(ResolvedProperty);
				checkf(ClassProp != nullptr, TEXT("Indexed Class Property in Descriptor of the incorrect type"));

				UClass* NewValue = nullptr;
				if (!EmptyString)
				{
					FString ClassPath = FString(PtrAsTchar.Get());
					NewValue = LoadObject<UClass>(nullptr, *ClassPath);
				}

				TObjectPtr<UObject> CurrentValue = ClassProp->GetPropertyValue(ResolvedContainer);
	
				if (NewValue)
				{
					if (!Client->Lookup->FindClassDescriptor(NewValue))
					{
						FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
						if ([[maybe_unused]] UTypeDescriptor* NewDescriptor = Client->Lookup->CreateTypeDescriptor(
							NewValue, BuildOptions))
						{
							FUSION_LOG("Create new descriptor for received class: %s", *NewValue->GetName());
						}
					}
				}
				
				if (CurrentValue != NewValue)
				{
					if (RepNotify)
					{
						Context.AddRepFunctionPointer(RepNotify, ResolvedProperty, CurrentValue);
					}
					
					ClassProp->SetPropertyValue(ResolvedContainer, NewValue);
					Changed = true;
				}
			}
			
		}
		break;

	case EFusionDataTypes::ObjectId:
		checkf(false, TEXT("Object Properties should never hit this, they override CopyFrom"));
		break;
	
	case EFusionDataTypes::Vector:
		{
			FVector* Ptr = static_cast<FVector*>(ResolvedContainer);
			FVector NewValue;
			
			NewValue.X = DecompressFloat(Words[0]);
			NewValue.Y = DecompressFloat(Words[1]);
			NewValue.Z = DecompressFloat(Words[2]);

			if (NewValue != *Ptr)
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, Ptr);
				}

				if (!Interpolating)
				{
					*Ptr = NewValue;
					Changed = true;
				}
			}
		}
		break;

	case EFusionDataTypes::Rotator:
		{
			FRotator* Ptr = static_cast<FRotator*>(ResolvedContainer);
			FRotator NewValue;

			NewValue.Pitch = DecompressFloat(Words[0]);
			NewValue.Yaw = DecompressFloat(Words[1]);
			NewValue.Roll = DecompressFloat(Words[2]);

			if (NewValue != *Ptr)
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, Ptr);
				}
				
				if (!Interpolating)
				{
					*Ptr = NewValue;
					Changed = true;
				}
			}
		}
		break;

	case EFusionDataTypes::Quat:
		{
			FQuat* Ptr = static_cast<FQuat*>(ResolvedContainer);
			const float* WordsF = reinterpret_cast<float*>(Words);
			
			FQuat NewValue;

			NewValue.X = WordsF[0];
			NewValue.Y = WordsF[1];
			NewValue.Z = WordsF[2];
			NewValue.W = WordsF[3];
			
			if (*Ptr != NewValue)
			{
				if (RepNotify)
				{
					Context.AddRepFunction(RepNotify, ResolvedProperty, Ptr);
				}
				
				if (!Interpolating)
				{
					*Ptr = NewValue;
					Changed = true;
				}
			}
		}
		break;
	case EFusionDataTypes::Name:
	case EFusionDataTypes::String:
		{
			SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(Words);
			
			SharedMode::StringMessage OutStringStatus{};
			const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
			const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> StringPtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(StringPtr));
			EncodedStringStatus Status = UPropertyHelpers::GetEncodedStringStatus(OutStringStatus);
			
			if (Status != EncodedStringStatus::Valid)
			{
				FString StatusString = StaticEnum<EncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
				FUSION_LOG_ERROR("Encoded string has error: %s", *StatusString);
				return false;
			}

			if (OutStringStatus ==  SharedMode::StringMessage::EmptyHeap)
			{
				return false;
			}

			bool EmptyString = ExistingHandle->id == UINT32_MAX;

			if (StringPtr || EmptyString)
			{
				// FUSION_LOG("String Property: %s  Length: %d", *ResolvedProperty->GetName(), StringLen);
				
				checkf(ResolvedProperty != nullptr, TEXT("Unable to resolve Name Property"));
				
				if (ResolvedProperty->IsA(FNameProperty::StaticClass()))
				{
					FNameProperty* NameProp = static_cast<FNameProperty*>(ResolvedProperty);
					FName OldValue = NameProp->GetPropertyValue(ResolvedContainer);
					FString StringValue = EmptyString ? "" : FString(StringPtrAsTchar.Get());

					if (FName NameValue(*StringValue); OldValue != NameValue)
					{
						if (RepNotify)
						{
							Context.AddRepFunction(RepNotify, ResolvedProperty, NameProp->GetPropertyValuePtr(ResolvedContainer));
						}
						
						NameProp->SetPropertyValue(ResolvedContainer, NameValue);
						Changed = true;
					}
				}
				else if (ResolvedProperty->IsA(FStrProperty::StaticClass()))
				{
					FStrProperty* StrProp = static_cast<FStrProperty*>(ResolvedProperty);
					FString OldValue = StrProp->GetPropertyValue(ResolvedContainer);

					if (FString StringValue = EmptyString ? "" : FString(StringPtrAsTchar.Get()); OldValue != StringValue)
					{
						if (RepNotify)
						{
							Context.AddRepFunction(RepNotify, ResolvedProperty, StrProp->GetPropertyValuePtr(ResolvedContainer));
						}
						
						StrProp->SetPropertyValue(ResolvedContainer, StringValue);
						Changed = true;
					}
				}
			}
		}
		break;
		
	case EFusionDataTypes::Array:
	case EFusionDataTypes::FusionArray:
		checkf(false, TEXT("Array Properties should never hit this, they override CopyFrom"));
		break;
	}

	return Changed;
}


inline void Property::ResolveContainerAndProperty(void* Container, void*& ResolvedContainer, FProperty*& ResolvedProperty) const
{
	if (SkipResolve)
	{
		ResolvedContainer = Container;
		ResolvedProperty = EngineProperty;
		
		return;
	}
	
	if (!EngineProperty)
	{
		FUSION_LOG_WARN("Cannot resolve property where initial property is null");

		ResolvedContainer = nullptr;
		ResolvedProperty = nullptr;
		return;
	}
	
	void* InnerContainer = EngineProperty->ContainerPtrToValuePtr<uint8>(Container);
	
	ResolvedProperty = EngineProperty;
	ResolvedContainer = InnerContainer;
}

void Property::AddSubProperty(Property* Property)
{
	SubProperties.Add(Property);
}

bool Property::ResolveObjectProperty(void* Container, void*& ResolvedContainer, FProperty*& ResolvedProperty) const
{
	if (!EngineProperty)
	{
		FUSION_LOG_WARN("Cannot resolve property where initial property is null");

		ResolvedContainer = nullptr;
		ResolvedProperty = nullptr;
		return false;
	}
	
	void* InnerContainer = EngineProperty->ContainerPtrToValuePtr<uint8>(Container);
	
	ResolvedProperty = EngineProperty;
	ResolvedContainer = InnerContainer;
	return true;
}

void StructProperty::CopyTo(UFusionClient* Client, FCopyContext& CopyRoot, void* Container, int* Words) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::CopyTo::Struct);
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	if (!ResolveObjectProperty(Container, ResolvedContainer, ResolvedProperty))
		return;

	for (int j = 0; j < SubProperties.Num(); j++)
	{
		const Property* ItemProperty = SubProperties[j];
		int* TargetAddress = Words + ItemProperty->WordOffset;

		ItemProperty->CopyTo(Client, CopyRoot, ResolvedContainer, TargetAddress);
	}
}

bool StructProperty::CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow, const bool Interpolate) const
{
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	if (!ResolveObjectProperty(Container, ResolvedContainer, ResolvedProperty))
		return false;

	if (RepNotify)
	{
		if (ValueBuffer && RepNotify->NumParms > 0)
		{
			//Store in temp buffer, valid in this particular stack frame, continuously overriden.
			ResolvedProperty->CopyCompleteValue(ValueBuffer, ResolvedContainer);
		}
	}
	
	bool Changed = false;
	for (int j = 0; j < SubProperties.Num(); j++)	
	{
		const Property* ItemProperty = SubProperties[j];
		int* TargetAddress = Words + ItemProperty->WordOffset;
		int* ShadowAddress = Shadow + ItemProperty->WordOffset;
		
		Changed |= ItemProperty->CopyFrom(Client, Context, ResolvedContainer, TargetAddress, ShadowAddress, Interpolate);
	}

	if (Changed && RepNotify)
	{
		Context.AddRepFunction(RepNotify, ResolvedProperty, ValueBuffer);
	}

	return Changed;
}

void StructProperty::CleanupPreviousState(UFusionClient* Client, const FCopyContext& Context, void* Container, int* Words)
{
	check(EngineProperty);
	
	void* ResolvedContainer = Container ? EngineProperty->ContainerPtrToValuePtr<uint8>(Container) : nullptr;
	
	for (int j = 0; j < SubProperties.Num(); j++)	
	{
		Property* ItemProperty = SubProperties[j];
		int* TargetAddress = Words + ItemProperty->WordOffset;
		
		ItemProperty->CleanupPreviousState(Client, Context, ResolvedContainer, TargetAddress);
	}
}

void StructProperty::Serialize(UFusionClient* Client, void* Container, const FFunctionProperty& FunctionProperty, TArray<uint8>& Buffer, bool RootArgument)
{
	check(EngineProperty);
	
	for (int j = 0; j < SubProperties.Num(); j++) {
		Property* ItemProperty = SubProperties[j];
		const int32 Offset = ItemProperty->EngineProperty ? ItemProperty->EngineProperty->GetOffset_ForInternal() : 0;
		uint8* SubPropertyContainer = static_cast<uint8*>(Container) + Offset;
		ItemProperty->Serialize(Client, SubPropertyContainer, FunctionProperty, Buffer, false);
	}
}

void StructProperty::Deserialize(UFusionClient* Client, uint8_t* Buffer, void* Container,
	const FFunctionProperty& FunctionProperty, int& CurrentOffset)
{
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	ResolveContainerAndProperty(Container, ResolvedContainer, ResolvedProperty);

	for (int j = 0; j < SubProperties.Num(); j++)	
	{
		Property* ItemProperty = SubProperties[j];
		ItemProperty->Deserialize(Client, Buffer, ResolvedContainer, FunctionProperty, CurrentOffset);
	}
}

ObjectProperty::~ObjectProperty()
{
	
}

bool IsObjectTransient(const TObjectPtr<UObject>& ObjectPtr)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::CopyTo::Object::IsObjectTransient);
	bool IsTransient = false;
	if (ObjectPtr->GetWorld())
	{
		IsTransient = true;
	}
	else if (ObjectPtr->HasAnyFlags(RF_Transient))
	{
		IsTransient = true;
	}

	if (const UPackage* Package = ObjectPtr->GetOutermost())
	{
		if (Package->HasAnyPackageFlags(PKG_InMemoryOnly))
		{
			IsTransient = true;
		}
		else if (Package == GetTransientPackage())
		{
			IsTransient = true;
		}
	}

	if (Cast<UClass>(ObjectPtr.Get()))
	{
		//Direct class pointer and not to object, then obviously not transient
		IsTransient = false;
	}

	return IsTransient;
}

Property* ObjectProperty::Clone() const
{
	return new ObjectProperty(*this);
}

void ObjectProperty::CheckReleaseString(FCopyContext& Context, int* Words) const
{
	const EEncodedObjectType PreviousType = static_cast<EEncodedObjectType>(Words[0]);

	if (PreviousType == EEncodedObjectType::MapObjectComponentStringPath ||
						PreviousType == EEncodedObjectType::NetworkedObjectComponentStringPath ||
						PreviousType == EEncodedObjectType::ObjectPath ||
						PreviousType == EEncodedObjectType::ClassPath ||
						PreviousType == EEncodedObjectType::BlueprintCDOClass ||
						PreviousType == EEncodedObjectType::SoftObjectPath)
	{
		int* StringTargetAddress = Words + 1;
		const SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(StringTargetAddress);
		SharedMode::StringHandle resultHandle = Context.Pair.Object->FreeString(*ExistingHandle);
	}
}

void ObjectProperty::CopyTo(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::CopyTo::Object);
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	if (!ResolveObjectProperty(Container, ResolvedContainer, ResolvedProperty))
		return;
	
	UObject* TargetObject {nullptr};
	switch (PointerType)
	{
		case EObjectPointerType::ObjectPointer:
			{
				const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(ResolvedProperty);
				const TObjectPtr<UObject> ObjectPtr = ObjectProperty->GetPropertyValue(ResolvedContainer);
				TargetObject = ObjectPtr.Get();
			}
		break;
	case EObjectPointerType::WeakObjectPointer:
		{
			const FWeakObjectProperty* ObjectProperty = CastField<FWeakObjectProperty>(ResolvedProperty);
			const FWeakObjectPtr ObjectPtr = ObjectProperty->GetPropertyValue(ResolvedContainer);
			TargetObject = ObjectPtr.Get();
		}
		break;
	case EObjectPointerType::SoftObjectPointer:
		{
			const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(ResolvedProperty);
			const FSoftObjectPtr SoftPtr = SoftProperty->GetPropertyValue(ResolvedContainer);
			const FSoftObjectPath& SoftPath = SoftPtr.GetUniqueID();

			if (SoftPath.IsValid())
			{
				Words[0] = static_cast<int32>(EEncodedObjectType::SoftObjectPath);
				
				int* StringTargetAddress = Words + 1;
				const SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(StringTargetAddress);

				// If previous encoding was non-string (e.g. NetworkedObject), words contain ObjectId data, not a valid string handle.
				if (!Context.Pair.Object->IsValidStringHandle(*ExistingHandle))
				{
					Words[1] = 0;
					Words[2] = 0;
				}

				const FString Path = SoftPath.ToString();
				const SharedMode::StringHandle Handle = UPropertyHelpers::EncodeString(Context.Pair.Object, Path, *ExistingHandle);
				std::memcpy(StringTargetAddress, &Handle, sizeof(SharedMode::StringHandle));
			}
			else
			{
				CheckReleaseString(Context, Words);

				Words[0] = static_cast<int32>(EEncodedObjectType::None);
				Words[1] = 0;
				Words[2] = 0;
			}
			return;
		}
	}
	
	if (TargetObject)
	{
		UClass* TypeClass = TargetObject->GetClass();
		bool IsClassPointer = false;
		
		if (UClass* ClassPtr = Cast<UClass>(TargetObject)) {
			//In some rare cases the object field is not pointer to actual object but rather to a defined class.
			TypeClass = ClassPtr;
			IsClassPointer = true;
		}

		FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
		const UTypeDescriptor* Descriptor = Client->Lookup->CreateTypeDescriptor(TypeClass, BuildOptions);
		
		if (const SharedMode::Object* FoundObject = Client->FindObject(TargetObject))
		{
			//In case previous encoding was pointing to string heap, release first.
			CheckReleaseString(Context, Words);
			
			Words[0] = static_cast<int32>(EEncodedObjectType::NetworkedObject);
			Words[1] = FoundObject->Id.Origin;
			Words[2] = static_cast<int32>(FoundObject->Id.Counter);
			
			return;
		}
	
		if (const UActorComponent* ActorComponent = Cast<UActorComponent>(TargetObject))
		{
			if (ActorComponent->GetOwner()->GetLevel())
			{
				FString ComponentPath;

				if (const SharedMode::ObjectId OwnerId = Client->FindObjectId(ActorComponent->GetOwner()); OwnerId.IsSome())
				{
					Words[0] = static_cast<int32>(EEncodedObjectType::NetworkedObjectComponentStringPath);
					ComponentPath = FString::Printf(TEXT("%u:%u:%s"), OwnerId.Origin, OwnerId.Counter, *ActorComponent->GetName());
				}
				else
				{
					Words[0] = static_cast<int32>(EEncodedObjectType::MapObjectComponentStringPath);
					ComponentPath = GetSerializedLevelPath(ActorComponent->GetOwner(), ActorComponent);
				}

				int* TargetAddress = Words + 1;
				const SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(TargetAddress);

				// If previous encoding was non-string (e.g. NetworkedObject), words contain ObjectId data, not a valid string handle.
				if (!Context.Pair.Object->IsValidStringHandle(*ExistingHandle))
				{
					Words[1] = 0;
					Words[2] = 0;
				}

				const SharedMode::StringHandle Handle = UPropertyHelpers::EncodeString(Context.Pair.Object, ComponentPath, *ExistingHandle);
				std::memcpy(TargetAddress, &Handle, sizeof(SharedMode::StringHandle));
			}
			else
			{
				CheckReleaseString(Context, Words);
				
				Words[0] = static_cast<int32>(EEncodedObjectType::None);
				Words[1] = 0;
				Words[2] = 0;
			}
			return;
		}

		if (AActor* Actor = Cast<AActor>(TargetObject))
		{
			CheckReleaseString(Context, Words);
			
			const unsigned int Hash = UFusionHelpers::SafeObjectNameHash(Actor);
			if (Client->MapActors.Contains(Hash))
			{
				Words[0] = static_cast<int32>(EEncodedObjectType::MapObjectHash);
				Words[1] = Hash;
				Words[2] = 0;
			}
			else
			{
				Words[0] = static_cast<int32>(EEncodedObjectType::None);
				Words[1] = 0;
				Words[2] = 0;
			}
			return;
		}

		//Exhaustive check to determine if object is a runtime created instance or if it's loaded from an asset (don't want objects for those)
		if (!IsClassPointer && IsObjectTransient(TargetObject))
		{
			CheckReleaseString(Context, Words);
			
			if (SharedMode::Object* CustomObject = Client->CreateCustomObject(Context, TargetObject, Descriptor, Client->CurrentMapInstance.Sequence))
			{
				//Ensure local client has this set immediately.
				CustomObject->Engine = TargetObject;

				Words[0] = static_cast<int32>(EEncodedObjectType::NetworkedObject);
				Words[1] = CustomObject->Id.Origin;
				Words[2] = static_cast<int32>(CustomObject->Id.Counter);
			}
			else
			{
				Words[0] = static_cast<int32>(EEncodedObjectType::None);
				Words[1] = 0;
				Words[2] = 0;
			}
			return;
		}

		int* StringTargetAddress = Words + 1;
		const SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(StringTargetAddress);

		// If previous encoding was non-string (e.g. NetworkedObject), words contain ObjectId data, not a valid string handle.
		if (!Context.Pair.Object->IsValidStringHandle(*ExistingHandle))
		{
			Words[1] = 0;
			Words[2] = 0;
		}

		if (IsClassPointer)
		{
			Words[0] = static_cast<int32>(EEncodedObjectType::ClassPath);

			const FString Path = TypeClass->GetPathName();
			const SharedMode::StringHandle Handle = UPropertyHelpers::EncodeString(Context.Pair.Object, Path, *ExistingHandle);
			std::memcpy(StringTargetAddress, &Handle, sizeof(SharedMode::StringHandle));
		}
		else if (const UPackage* Package = TargetObject->GetOutermost())
		{
			if (!Package->HasAnyPackageFlags(PKG_InMemoryOnly))
			{
				if ([[maybe_unused]] bool bIsBlueprintGenerated = TypeClass->IsChildOf(UBlueprintGeneratedClass::StaticClass())) {
					Words[0] = static_cast<int32>(EEncodedObjectType::BlueprintCDOClass);

					const FString Path = TypeClass->GetPathName();
					const SharedMode::StringHandle Handle = UPropertyHelpers::EncodeString(Context.Pair.Object, Path, *ExistingHandle);
					std::memcpy(StringTargetAddress, &Handle, sizeof(SharedMode::StringHandle));
				}
				else {
					Words[0] = static_cast<int32>(EEncodedObjectType::ObjectPath);

					const FString Path = TargetObject->GetPathName();
					const SharedMode::StringHandle Handle = UPropertyHelpers::EncodeString(Context.Pair.Object, Path, *ExistingHandle);
					std::memcpy(StringTargetAddress, &Handle, sizeof(SharedMode::StringHandle));
				}
			}
		}
		else
		{
			CheckReleaseString(Context, Words);
			
			Words[0] = static_cast<int32>(EEncodedObjectType::None);
			Words[1] = 0;
			Words[2] = 0;
		}
		return;
	}


	CheckReleaseString(Context, Words);
	
	Words[0] = static_cast<int32>(EEncodedObjectType::None);
	Words[1] = 0;
	Words[2] = 0;
}

void ObjectProperty::Serialize(UFusionClient* Client, void* Container, const FFunctionProperty& FunctionProperty, TArray<uint8>& Buffer, bool RootArgument)
{
	if (EngineProperty && Container)
	{
		UObject* TargetObject{nullptr};
		switch (PointerType)
		{
			case EObjectPointerType::ObjectPointer:
				{
					const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(EngineProperty);
					const TObjectPtr<UObject> ObjectPtr = ObjectProperty->GetPropertyValue(Container);
					TargetObject = ObjectPtr.Get();
				}
				break;
			case EObjectPointerType::WeakObjectPointer:
				{
					const FWeakObjectProperty* ObjectProperty = CastField<FWeakObjectProperty>(EngineProperty);
					const FWeakObjectPtr ObjectPtr = ObjectProperty->GetPropertyValue(Container);
					TargetObject = ObjectPtr.Get();
				}
				break;
			case EObjectPointerType::SoftObjectPointer:
				{
					const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(EngineProperty);
					const FSoftObjectPtr SoftPtr = SoftProperty->GetPropertyValue(Container);
					const FSoftObjectPath& SoftPath = SoftPtr.GetUniqueID();

					if (SoftPath.IsValid())
					{
						AddObjectType(Buffer, EEncodedObjectType::SoftObjectPath);
						AddString(Buffer, SoftPath.ToString());
					}
					else
					{
						AddObjectType(Buffer, EEncodedObjectType::None);
					}
					return;
				}
		}

		if (TargetObject)
		{
			if ([[maybe_unused]] UClass* ClassPtr = Cast<UClass>(TargetObject))
			{
				FUSION_LOG_ERROR("Unsupported UClass target object");
			}

			if (AActor* Actor = Cast<AActor>(TargetObject))
			{
				if (const SharedMode::Object* FoundSubObject = Client->FindObject(Actor))
				{
					AddObjectType(Buffer, EEncodedObjectType::NetworkedObject);

					AddTwoInts(Buffer,
						FoundSubObject->Id.Origin,
						FoundSubObject->Id.Counter);
				}
				else
				{
					AddObjectType(Buffer, EEncodedObjectType::MapObjectHash);
					
					const unsigned int ObjectHash = UFusionHelpers::SafeObjectNameHash(Actor);
					AddOneInt(Buffer, ObjectHash);
				}

				return;
			}
			if (UActorComponent* Component = Cast<UActorComponent>(TargetObject))
			{
				if (const SharedMode::Object* FoundSubObject = Client->FindObject(Component))
				{
					AddObjectType(Buffer, EEncodedObjectType::NetworkedObject);
					
					AddTwoInts(Buffer,
						FoundSubObject->Id.Origin,
						FoundSubObject->Id.Counter);
				}
				else
				{
					if (Component->GetOwner()->GetLevel())
					{
						AddObjectType(Buffer, EEncodedObjectType::MapObjectComponentStringPath);
						
						const FString SerializedLevelPath = GetSerializedLevelPath(Component->GetOwner(), Component);
						AddString(Buffer, SerializedLevelPath);
					}
					else
					{
						AddObjectType(Buffer, EEncodedObjectType::None);
					}
				}

				return;
			}

			if (const SharedMode::ObjectId ObjectId = Client->FindObjectId(TargetObject); ObjectId.IsSome())
			{
				AddObjectType(Buffer, EEncodedObjectType::NetworkedObject);
				AddTwoInts(Buffer, ObjectId.Origin, ObjectId.Counter);
				return;
			}
			
			const unsigned int ObjectHash = UFusionHelpers::SafeObjectNameHash(TargetObject);
			if ([[maybe_unused]] TObjectPtr<AActor>* FoundActor = Client->MapActors.Find(ObjectHash))
			{
				AddObjectType(Buffer, EEncodedObjectType::MapObjectHash);
				AddOneInt(Buffer, ObjectHash);
				return;
			}
			
			if (const UPackage* Package = TargetObject->GetOutermost())
			{
				if (!Package->HasAnyPackageFlags(PKG_InMemoryOnly))
				{
					const UClass* TypeClass = TargetObject->GetClass();
					if ([[maybe_unused]] bool bIsBlueprintGenerated = TypeClass->IsChildOf(UBlueprintGeneratedClass::StaticClass()))
					{
						AddObjectType(Buffer, EEncodedObjectType::BlueprintCDOClass);
						AddString(Buffer, TypeClass->GetPathName());
					}
					else
					{
						AddObjectType(Buffer, EEncodedObjectType::ObjectPath);
						AddString(Buffer, TargetObject->GetPathName());
					}
					return;
				}
			}
		}

		AddObjectType(Buffer, EEncodedObjectType::None);
	}
}

bool ObjectProperty::CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow, bool Interpolate) const
{
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	if (!ResolveObjectProperty(Container, ResolvedContainer, ResolvedProperty))
		return false;

	const EEncodedObjectType ObjectLoadType = static_cast<EEncodedObjectType>(Words[0]);
	bool Changed = false;
	UObject* NewValue{nullptr};
	
	if (ObjectLoadType == EEncodedObjectType::MapObjectHash) //Reserved for map actors
	{
		uint32 ObjectHash = static_cast<uint32>(Words[1]);

		if (TObjectPtr<AActor>* FoundActor = Client->MapActors.Find(ObjectHash))
		{
			NewValue = FoundActor->Get();
		}
	}
	else if (ObjectLoadType == EEncodedObjectType::NetworkedObject)
	{
		uint32 origin = static_cast<uint32>(Words[1]);
		uint32 counter = static_cast<uint32>(Words[2]);
		SharedMode::ObjectId Id = SharedMode::ObjectId(origin, counter);
		
		if (Id.IsSome()) {
			if (FObjectActorPair FoundPair = Client->FindObjectPair(Id); FoundPair.IsValid())
			{
				NewValue = FoundPair.EngineObject;
			}
			else
			{
				//Have property wait for dependency to become available
				Client->AddDependencyCheck(Id, Context, []()
				{
					return true;
				});
			}
		}
	}
	else if (ObjectLoadType == EEncodedObjectType::MapObjectComponentStringPath || ObjectLoadType == EEncodedObjectType::NetworkedObjectComponentStringPath)
	{
		int* TargetAddress = Words + 1;
		SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(TargetAddress);
			
		SharedMode::StringMessage OutStringStatus{};
		const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
		const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> StringPtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(StringPtr));
		EncodedStringStatus Status = UPropertyHelpers::GetEncodedStringStatus(OutStringStatus);
			
		if (Status != EncodedStringStatus::Valid)
		{
			FString StatusString = StaticEnum<EncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
			FUSION_LOG_ERROR("Encoded string has error: %s   Object: %s", *StatusString, *Context.Pair.EngineObject->GetName());
			return false;
		}

		if (OutStringStatus ==  SharedMode::StringMessage::EmptyHeap)
		{
			return false;
		}

		bool EmptyString = ExistingHandle->id == UINT32_MAX;
		
		if (!EmptyString && StringPtr)
		{
			FString Path = FString(StringPtrAsTchar.Get());

			if (ObjectLoadType == EEncodedObjectType::MapObjectComponentStringPath)
			{
				FString HashPart, CompPart;
				if (Path.Split(TEXT(":"), &HashPart, &CompPart))
				{
					uint32 ActorHash = FCString::Strtoui64(*HashPart, nullptr, 10);

					if (TObjectPtr<AActor>* FoundActor = Client->MapActors.Find(ActorHash)) {
						FName ComponentName(*CompPart);

						for (UActorComponent* Comp : FoundActor->Get()->GetComponents()) {
							if (Comp && Comp->GetFName() == ComponentName)
							{
								NewValue = Comp;
								break;
							}
						}
					}
				}
			}
			else if (ObjectLoadType == EEncodedObjectType::NetworkedObjectComponentStringPath)
			{
				const int32 FirstColon = Path.Find(TEXT(":"));
				const int32 SecondColon = (FirstColon != INDEX_NONE) ? Path.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstColon + 1) : INDEX_NONE;

				if (FirstColon != INDEX_NONE && SecondColon != INDEX_NONE)
				{
					uint32 ActorOrigin = FCString::Strtoui64(*Path, nullptr, 10);
					uint32 ActorCounter = FCString::Strtoui64(*Path + FirstColon + 1, nullptr, 10);

					if (SharedMode::ObjectId Id = SharedMode::ObjectId(ActorOrigin, ActorCounter); Id.IsSome())
					{
						if (AActor* FoundActor = Cast<AActor>(Client->FindObject(Id)))
						{
							FName ComponentName(*Path + SecondColon + 1);

							for (UActorComponent* Comp : FoundActor->GetComponents())
							{
								if (Comp && Comp->GetFName() == ComponentName)
								{
									NewValue = Comp;
									break;
								}
							}
						}
					}
				}
			}
		}
	}
	else if (ObjectLoadType == EEncodedObjectType::ObjectPath ||
			ObjectLoadType == EEncodedObjectType::ClassPath ||
			ObjectLoadType == EEncodedObjectType::BlueprintCDOClass)
	{
		int* TargetAddress = Words + 1;
		SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(TargetAddress);
			
		SharedMode::StringMessage OutStringStatus{};
		const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
		const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> StringPtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(StringPtr));
		EncodedStringStatus Status = UPropertyHelpers::GetEncodedStringStatus(OutStringStatus);
			
		if (Status != EncodedStringStatus::Valid)
		{
			FString StatusString = StaticEnum<EncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
			FUSION_LOG_ERROR("Encoded string has error: %s   Object: %s", *StatusString, *Context.Pair.EngineObject->GetName());
			return false;
		}

		if (OutStringStatus ==  SharedMode::StringMessage::EmptyHeap)
		{
			return false;
		}

		bool EmptyString = ExistingHandle->id == UINT32_MAX;
		
		if (!EmptyString && StringPtr)
		{
			FString Path = FString(StringPtrAsTchar.Get());

			if (ObjectLoadType == EEncodedObjectType::ObjectPath)
			{
				NewValue = LoadObject<UObject>(nullptr, *Path);
			}
			else if (ObjectLoadType == EEncodedObjectType::ClassPath)
			{
				NewValue = LoadObject<UClass>(nullptr, *Path);
			}
			else if (ObjectLoadType == EEncodedObjectType::BlueprintCDOClass)
			{
				
			}
		}
	}
	else if (ObjectLoadType == EEncodedObjectType::SoftObjectPath)
	{
		if (FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(ResolvedProperty))
		{
			int* TargetAddress = Words + 1;
			SharedMode::StringHandle* ExistingHandle = reinterpret_cast<SharedMode::StringHandle*>(TargetAddress);

			SharedMode::StringMessage OutStringStatus{};
			const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
			EncodedStringStatus Status = UPropertyHelpers::GetEncodedStringStatus(OutStringStatus);

			if (Status != EncodedStringStatus::Valid)
			{
				FString StatusString = StaticEnum<EncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
				FUSION_LOG_ERROR("Encoded string has error: %s   Object: %s", *StatusString, *Context.Pair.EngineObject->GetName());
				return false;
			}

			if (OutStringStatus ==  SharedMode::StringMessage::EmptyHeap)
			{
				return false;
			}

			FSoftObjectPath NewPath;
			bool EmptyString = ExistingHandle->id == UINT32_MAX;
			if (!EmptyString && StringPtr)
			{
				const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> StringPtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(StringPtr));
				NewPath.SetPath(FString(StringPtrAsTchar.Get()));
			}

			const FSoftObjectPtr CurrentValue = SoftProperty->GetPropertyValue(ResolvedContainer);
			if (CurrentValue.GetUniqueID() != NewPath)
			{
				if (RepNotify)
				{
					FProperty* Property = const_cast<FProperty*>(static_cast<const FProperty*>(SoftProperty));
					Context.AddRepFunctionPointer(RepNotify, Property, nullptr);
				}
				SoftProperty->SetPropertyValue(ResolvedContainer, FSoftObjectPtr(NewPath));
				Changed = true;

				FUSION_LOG("Setting Soft Object Property: %s   Value: %s", *ResolvedProperty->GetName(), *NewPath.ToString());
			}
		}

		return Changed;
	}

	switch (PointerType)
	{
		case EObjectPointerType::ObjectPointer:
			{
				FObjectProperty* ObjectProperty = CastField<FObjectProperty>(ResolvedProperty);
				Changed |= SetObjectPropertyValueIfDifferent<FObjectProperty>(Context, ObjectProperty, ResolvedContainer, NewValue);
			}
			break;
		case EObjectPointerType::WeakObjectPointer:
			{
				FWeakObjectProperty* WeakProperty = CastField<FWeakObjectProperty>(ResolvedProperty);
				Changed |= SetObjectPropertyValueIfDifferent<FWeakObjectProperty>(Context, WeakProperty, ResolvedContainer, NewValue);
			}
			break;
	}

	if (Changed)
	{
		FString ValueName = NewValue ? NewValue->GetName() : TEXT("nullptr");
		FUSION_LOG_TRACE("Setting Object Property: %s   Value: %s", *ResolvedProperty->GetName(), *ValueName);
	}

	return Changed;
}

void ObjectProperty::Deserialize(UFusionClient* Client, uint8_t* Buffer, void* Container,
	const FFunctionProperty& FunctionProperty, int& CurrentOffset)
{
	void* ResolvedContainer{ nullptr };
	FProperty* ResolvedProperty{ nullptr };
	
	if (!ResolveObjectProperty(Container, ResolvedContainer, ResolvedProperty))
		return;

	[[maybe_unused]] void* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<void>(ResolvedContainer);

	const int32* DataPtr = reinterpret_cast<const int32*>(Buffer + CurrentOffset);
	const EEncodedObjectType ObjectType = static_cast<EEncodedObjectType>(DataPtr[0]);
	CurrentOffset += 1 * sizeof(int32);
	
	UObject* NewValue{ nullptr };

	if (ObjectType == EEncodedObjectType::None)
	{
		//No nothing for now
	}
	else if (ObjectType == EEncodedObjectType::MapObjectHash)
	{
		const uint32 ObjectHash = DataPtr[1];
		if (const TObjectPtr<AActor>* FoundActor = Client->MapActors.Find(ObjectHash))
		{
			NewValue = FoundActor->Get();
		}

		CurrentOffset += 1 * sizeof(int32);
	}
	else if (ObjectType == EEncodedObjectType::NetworkedObject)
	{
		uint32 origin = static_cast<uint32>(DataPtr[1]);
		uint32 counter = static_cast<uint32>(DataPtr[2]);
		
		if (const SharedMode::ObjectId Id = SharedMode::ObjectId(origin, counter); Id.IsSome()) {
			if (const FObjectActorPair FoundPair = Client->FindObjectPair(Id); FoundPair.IsValid())
			{
				NewValue = FoundPair.EngineObject;
			}
		}

		CurrentOffset += 2 * sizeof(int32);
	}
	else if (ObjectType == EEncodedObjectType::MapObjectComponentStringPath)
	{
		FString Path = ReadString(Buffer, CurrentOffset);

		if (!Path.IsEmpty())
		{
			FString HashPart, CompPart;
			if (Path.Split(TEXT(":"), &HashPart, &CompPart)) {
				const uint32 ObjectHash = FCString::Strtoui64(*HashPart, nullptr, 10);

				if (const TObjectPtr<AActor>* FoundActor = Client->MapActors.Find(ObjectHash)) {
					const FName ComponentName(*CompPart);

					for (UActorComponent* Comp : FoundActor->Get()->GetComponents()) {
						if (Comp && Comp->GetFName() == ComponentName)
						{
							NewValue = Comp;
							break;
						}
					}
				}
			}
		}
	}
	else if (ObjectType == EEncodedObjectType::ObjectPath)
	{
		FString Path = ReadString(Buffer, CurrentOffset);
		
		NewValue = LoadObject<UObject>(nullptr, *Path);
	}
	else if (ObjectType == EEncodedObjectType::ClassPath)
	{
		FString Path = ReadString(Buffer, CurrentOffset);
		
		NewValue = LoadObject<UClass>(nullptr, *Path);
	}
	else if (ObjectType == EEncodedObjectType::BlueprintCDOClass)
	{
		FString Path = ReadString(Buffer, CurrentOffset);

		if (const UClass* Class = LoadObject<UClass>(nullptr, *Path))
		{
			//Loading class path, probably blueprint.
			UObject* CDO = Class->GetDefaultObject<UObject>();
			NewValue = CDO;
		}
	}
	else if (ObjectType == EEncodedObjectType::SoftObjectPath)
	{
		FString Path = ReadString(Buffer, CurrentOffset);

		if (PointerType == EObjectPointerType::SoftObjectPointer)
		{
			FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(ResolvedProperty);
			if (SoftProperty)
			{
				FSoftObjectPath SoftPath(Path);
				SoftProperty->SetPropertyValue(ResolvedContainer, FSoftObjectPtr(SoftPath));
			}
		}
		return;
	}

	switch (PointerType)
	{
		case EObjectPointerType::ObjectPointer:
		{
			const FObjectProperty* ObjProp = static_cast<FObjectProperty*>(ResolvedProperty);
			ObjProp->SetObjectPropertyValue(ResolvedContainer, NewValue);
		}
		break;
		case EObjectPointerType::WeakObjectPointer:
		{
			const FWeakObjectProperty* WeakObjProp = static_cast<FWeakObjectProperty*>(ResolvedProperty);
			WeakObjProp->SetObjectPropertyValue(ResolvedContainer, NewValue);
		}
		break;
	}
}

ArrayProperty::~ArrayProperty()
{
}

void ArrayProperty::CopyTo(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words) const
{
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	ResolveContainerAndProperty(Container, ResolvedContainer, ResolvedProperty);
	
	if (ResolvedContainer && ResolvedProperty)
	{
		const FArrayProperty* ArrayProp = static_cast<FArrayProperty*>(ResolvedProperty);
		const void* ArrayPtr = ArrayProp->ContainerPtrToValuePtr<void>(Container);
		FScriptArrayHelper ArrayHelper(ArrayProp, ArrayPtr);
		const int Size = ArrayHelper.Num();
		const int PreviousSize = Words[0];
		
		//Write array size.
		Words[0] = Size;

		int CurrentStride = 1; //Offset from array size
		const int MaxIteration = FMath::Min(Size, MaxItems);

		int StrideOffset = 0;
		if (SubProperties.Num() > 0) {
			const Property* LastProp = SubProperties[SubProperties.Num() - 1];
			StrideOffset = LastProp->WordOffset + LastProp->WordCount;
		}

		//This is currently in use by string heap, since we need a place to free string handles in case our array has changed.
		if (PreviousSize != Size && PreviousSize > 0) {
			for (int i = 0; i < PreviousSize; i++) {
				void* ElementAddr = i < Size ? ArrayHelper.GetRawPtr(i) : nullptr; //Only get valid indices.
				
				if (CurrentStride >= WordCount) {
					FUSION_LOG_ERROR("Out of bounds");
				}

				for (int j = 0; j < SubProperties.Num(); j++) {
					Property* ItemProperty = SubProperties[j];
					int* TargetAddress = Words + ItemProperty->WordOffset + CurrentStride;
					
					ItemProperty->CleanupPreviousState(Client, Context, ElementAddr, TargetAddress);
				}

				CurrentStride += StrideOffset;
			}
		}

		//Reset
		CurrentStride = 1;
	
		for (int i = 0; i < MaxIteration; i++) {
			void* ElementAddr = ArrayHelper.GetRawPtr(i);

			if (CurrentStride >= WordCount) {
				FUSION_LOG_ERROR("Out of bounds");
			}
		
			for (int j = 0; j < SubProperties.Num(); j++) {
				const Property* ItemProperty = SubProperties[j];
				int* TargetAddress = Words + ItemProperty->WordOffset + CurrentStride;
			
				ItemProperty->CopyTo(Client, Context, ElementAddr, TargetAddress);
			}

			CurrentStride += StrideOffset;
		}

		//FUSION_LOG("ArrayName: %s Size: %d ", *ArrayProp->GetName(), ArrayHelper.Num());
	}
}

bool ArrayProperty::CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow, bool Interpolate) const
{
	bool Changed = false;
	
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	ResolveContainerAndProperty(Container, ResolvedContainer, ResolvedProperty);

	if (ResolvedContainer && ResolvedProperty)
	{
		const FArrayProperty* ArrayProp = static_cast<FArrayProperty*>(ResolvedProperty);
		const void* ArrayPtr = ArrayProp->ContainerPtrToValuePtr<void>(Container);
		FScriptArrayHelper ArrayHelper(ArrayProp, ArrayPtr);
		const int ArraySize = Words[0];

		//just trying to catch if we encoded some crazy values or reading from incorrect part of word buffer.
		if (ArraySize < 0 || ArraySize > 500) {
			FUSION_LOG_ERROR("ArrayProperty::CopyFrom: Got Invalid Array Size: %d   For Property: %s", ArraySize, *ResolvedProperty->GetName());
			return false;
		}

		if (const int CurrentSize = ArrayHelper.Num(); CurrentSize != ArraySize) {
			ArrayHelper.Resize(ArraySize);
			Changed = true;
		}

		if (ArrayHelper.Num() > MaxItems) {
			FUSION_LOG_TRACE("Array: %s Larger than reallocated size of: %d", *ResolvedProperty->GetName(), MaxItems);
		}

		int StrideOffset = 0;
		if (SubProperties.Num() > 0) {
			const Property* LastProp = SubProperties[SubProperties.Num() - 1];
			StrideOffset = LastProp->WordOffset + LastProp->WordCount;
		}

		int CurrentStride = 1; //Offset from array size
		const int MaxIteration = FMath::Min(ArrayHelper.Num(), MaxItems); 		//Ensure we don't go past the preallocated size.
		
		for (int i = 0; i < MaxIteration; i++) {
			void* ElementAddr = ArrayHelper.GetRawPtr(i);

			if (CurrentStride >= WordCount) {
				FUSION_LOG_ERROR("Out of bounds");
			}

			for (int j = 0; j < SubProperties.Num(); j++) {
				const Property* ItemProperty = SubProperties[j];
				int* TargetAddress = Words + ItemProperty->WordOffset + CurrentStride;
				int* ShadowAddress = Shadow + ItemProperty->WordOffset + CurrentStride;
				
				Changed |= ItemProperty->CopyFrom(Client, Context, ElementAddr, TargetAddress, ShadowAddress, false);
			}
			
			//Puts the word pointer at the next element in our array.
			CurrentStride += StrideOffset;
		}
		
		//FUSION_LOG("Local ArraySize: %d  ReceivedSize: %d ArrayName: %s", ArrayHelper.Num(), ArraySize, *ArrayProp->GetName());
	}

	if (Changed && RepNotify)
	{
		Context.AddRepFunctionPointer(RepNotify, ResolvedProperty, nullptr);
	}

	return Changed;
}

void ArrayProperty::CleanupPreviousState(UFusionClient* Client, const FCopyContext& Context, void* Container, int* Words)
{
	check(EngineProperty);
	
	void* ResolvedContainer = Container ? EngineProperty->ContainerPtrToValuePtr<uint8>(Container) : nullptr;
	
	const FArrayProperty* ArrayProp = static_cast<FArrayProperty*>(EngineProperty);
	
	FScriptArrayHelper ArrayHelper(ArrayProp, ResolvedContainer);
	const int Size = ResolvedContainer ? ArrayHelper.Num() : 0;

	int CurrentStride = 1; //Offset from array size
	const int MaxIteration = FMath::Min(Size, MaxItems);
	int StrideOffset = 0;
	if (SubProperties.Num() > 0) {
		const Property* LastProp = SubProperties[SubProperties.Num() - 1];
		StrideOffset = LastProp->WordOffset + LastProp->WordCount;
	}
	
	for (int i = 0; i < MaxIteration; i++)
	{
		void* ElementAddr = nullptr;
		if (i < Size)
		{
			ElementAddr = ResolvedContainer ? ArrayHelper.GetRawPtr(i) : nullptr;
		}

		if (CurrentStride >= WordCount) {
			FUSION_LOG_ERROR("Out of bounds");
		}

		for (int j = 0; j < SubProperties.Num(); j++)
		{
			Property* ItemProperty = SubProperties[j];
			int* TargetAddress = Words + ItemProperty->WordOffset + CurrentStride;
			
			ItemProperty->CleanupPreviousState(Client, Context, ElementAddr, TargetAddress);
		}

		CurrentStride += StrideOffset;
	}
}

void ArrayProperty::Serialize(UFusionClient* Client, void* Container, const FFunctionProperty& FunctionProperty, TArray<uint8>& Buffer,
                              bool RootArgument)
{
	const FArrayProperty* ArrayProp = static_cast<FArrayProperty*>(EngineProperty);

	FScriptArrayHelper ArrayHelper(ArrayProp, Container);
	const int Size = Container ? ArrayHelper.Num() : 0;
	const int MaxIteration = FMath::Min(Size, MaxItems);
	
	//Encode array size into buffer
	const int32 BytesToAdd = sizeof(int32);
	const int32 Offset = Buffer.AddUninitialized(BytesToAdd);
	FMemory::Memcpy(Buffer.GetData() + Offset, &MaxIteration, sizeof(int32));
	
	for (int i = 0; i < MaxIteration; i++)
	{
		void* ElementAddr = Container ? ArrayHelper.GetRawPtr(i) : nullptr;
		
		for (int j = 0; j < SubProperties.Num(); j++)
		{
			Property* ItemProperty = SubProperties[j];
			const int32 ItemOffset = ItemProperty->EngineProperty ? ItemProperty->EngineProperty->GetOffset_ForInternal() : 0;
			uint8* SubPropertyContainer = static_cast<uint8*>(ElementAddr) + ItemOffset;
			
			ItemProperty->Serialize(Client, SubPropertyContainer, FunctionProperty, Buffer, false);
		}
	}
}

void ArrayProperty::Deserialize(UFusionClient* Client, uint8_t* Buffer, void* Container, const FFunctionProperty& FunctionProperty, int& CurrentOffset)
{
	uint8* ResolvedContainer = static_cast<uint8*>(Container);
	FProperty* ResolvedProperty = EngineProperty;
	
	if ([[maybe_unused]] FArrayProperty* ArrayProperty = static_cast<FArrayProperty*>(ResolvedProperty))
	{
		const int StructSize = ResolvedProperty->GetSize();
		const int Offset = ResolvedProperty->GetOffset_ForUFunction();
		void* ArrayPtr = ResolvedContainer + Offset;
		FMemory::Memzero(ArrayPtr, StructSize);
	}
	
	int32 ArraySize = 0;
	FMemory::Memcpy(&ArraySize, Buffer + CurrentOffset, sizeof(int32));

	//Place offset at start of the string.
	CurrentOffset += sizeof(int32);
	
	[[maybe_unused]] void* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<void>(ResolvedContainer);
	
	const FArrayProperty* ArrayProp = static_cast<FArrayProperty*>(EngineProperty);
	FScriptArrayHelper ArrayHelper(ArrayProp, DestAddr);

	ArrayHelper.AddUninitializedValues(ArraySize);

	for (int i = 0; i < ArraySize; i++)
	{
		void* ElementAddr = ArrayHelper.GetRawPtr(i);
		
		for (int j = 0; j < SubProperties.Num(); j++)
		{
			Property* ItemProperty = SubProperties[j];
			const int32 ItemOffset = ItemProperty->EngineProperty ? ItemProperty->EngineProperty->GetOffset_ForInternal() : 0;
			uint8* SubPropertyContainer = static_cast<uint8*>(ElementAddr) + ItemOffset;
			
			ItemProperty->Deserialize(Client, Buffer, SubPropertyContainer, FunctionProperty, CurrentOffset);
		}
	}
}

void ArrayProperty::ResolveItemProperty(const Property* SourceProperty, void* ItemAddress, void*& ResolvedContainer, FProperty*& ResolvedProperty) const
{
	void* InnerContainer = SourceProperty->EngineProperty->ContainerPtrToValuePtr<uint8>(ItemAddress);

	ResolvedProperty = SourceProperty->EngineProperty;
	ResolvedContainer = InnerContainer;
}

FusionArrayProperty::~FusionArrayProperty()
{
}

bool FusionArrayProperty::CopyFrom(UFusionClient* Client, FCopyContext& CopyRoot, void* Container, int* Words, int* Shadow, bool Interpolate) const
{
	bool Changed = false;
	
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	ResolveContainerAndProperty(Container, ResolvedContainer, ResolvedProperty);

	if (ResolvedContainer && ResolvedProperty)
	{
		const FArrayProperty* ArrayProp = static_cast<FArrayProperty*>(ResolvedProperty);
		FScriptArrayHelper ArrayHelper(ArrayProp, ResolvedContainer);

		const int ReceivedSize = Words[0];

		if (ReceivedSize < 0)
		{
			FUSION_LOG("Got Invalid Array Size: %d   For Property: %s", ReceivedSize, *ResolvedProperty->GetName());
			return false;
		}

		const int CurrentSize = ArrayHelper.Num();
		
		TArray<int32> ModifiedIndices;
		TArray<int32> AddedIndices;
		TArray<int32> RemovedIndices;
		
		if (ReceivedSize >= CurrentSize)
		{
			int CurrentStride = 1;
			const int MaxIteration = FMath::Min(ReceivedSize, MaxItems); 			//Ensure we don't go past the preallocated size.
			for (int i = 0; i < MaxIteration; i++)
			{
				if (i >= CurrentSize)
				{
					ArrayHelper.AddValue();
					AddedIndices.Add(i);

					Changed = true;
				}
				
				void* ElementAddr = ArrayHelper.GetRawPtr(i);
				bool ElementChanged = false;
			
				for (int j = 0; j < SubProperties.Num(); j++)
				{
					const Property* ItemProperty = SubProperties[j];
					int* TargetAddress = Words + ItemProperty->WordOffset + CurrentStride;
					int* ShadowAddress = Shadow + ItemProperty->WordOffset + CurrentStride;
					
					ElementChanged |= ItemProperty->CopyFrom(Client, CopyRoot, ElementAddr, TargetAddress, ShadowAddress, false);
				}

				if (ElementChanged)
				{
					ModifiedIndices.Add(i);
					Changed = true;
				}

				if (SubProperties.Num() > 0)
				{
					const Property* LastProp = SubProperties[SubProperties.Num() - 1];
					//Puts the pointer at the next element in our array.
					CurrentStride += LastProp->WordOffset + LastProp->WordCount;
				}
			}
		}
		else
		{
			for (int i = ReceivedSize; i < CurrentSize; i++)
			{
				RemovedIndices.Add(i);
			}
		}
		
		if (const FStructProperty* StructProp = static_cast<FStructProperty*>(ParentContainerProperty))
		{
			if (RemovedIndices.Num() > 0)
			{
				if (const FFusionArrayHooks* Hooks = UTypeDescriptorLibrary::HooksMap.Find(
					StructProp->Struct->GetName()))
				{
					//Container here will be the parent struct that is inheriting FFusionNetworkedArray, where the callbacks should also be.
					FFusionNetworkedArray* FastArray = static_cast<FFusionNetworkedArray*>(Container);
					Hooks->PreRemove(FastArray, RemovedIndices, ReceivedSize);
					FUSION_LOG("Hooks->PreRemove ArraySize: %d  ReceivedSize: %d ArrayName: %s", ArrayHelper.Num(), ReceivedSize, *ArrayProp->GetName());
				}

				//Remove after the callback has triggered so it can still operate on the underlying array.
				for (int32 i = RemovedIndices.Num() - 1; i >= 0; --i)
				{
					const int32 IndexToRemove = RemovedIndices[i];
					ArrayHelper.RemoveValues(IndexToRemove, 1);
				}
			}
			if (AddedIndices.Num() > 0)
			{
				if (const FFusionArrayHooks* Hooks = UTypeDescriptorLibrary::HooksMap.Find(
					StructProp->Struct->GetName()))
				{
					//Container here will be the parent struct that is inheriting FFusionNetworkedArray, where the callbacks should also be.
					FFusionNetworkedArray* FastArray = static_cast<FFusionNetworkedArray*>(Container);
					Hooks->PostAdd(FastArray, AddedIndices, ReceivedSize);
					FUSION_LOG("Hooks->PostAdd ArraySize: %d  ReceivedSize: %d ArrayName: %s", ArrayHelper.Num(), ReceivedSize, *ArrayProp->GetName());
				}
			}
			if (ModifiedIndices.Num() > 0)
			{
				if (const FFusionArrayHooks* Hooks = UTypeDescriptorLibrary::HooksMap.Find(
					StructProp->Struct->GetName()))
				{
					//Container here will be the parent struct that is inheriting FFusionNetworkedArray, where the callbacks should also be.
					FFusionNetworkedArray* FastArray = static_cast<FFusionNetworkedArray*>(Container);
					Hooks->PostChange(FastArray, ModifiedIndices, ReceivedSize);
					FUSION_LOG("Hooks->PostChange ArraySize: %d  ReceivedSize: %d ArrayName: %s", ArrayHelper.Num(), ReceivedSize, *ArrayProp->GetName());
				}
			}
		}

		//Ensure we destroy after the change callbacks, so we don't destroy something that the callback code might need to operate on first.
		if (const int ItemsToRemove = ArrayHelper.Num() - ReceivedSize; ItemsToRemove > 0)
		{
			ArrayHelper.RemoveValues(ReceivedSize, ItemsToRemove);
		}
		
		return Changed;
	}

	return Changed;
}

void Property::Serialize(UFusionClient* Client, void* Container, const FFunctionProperty& FunctionProperty, TArray<uint8>& Buffer, bool RootArgument)
{
	if (EngineProperty->IsA(FStrProperty::StaticClass()) || EngineProperty->IsA(FNameProperty::StaticClass()) )
	{
		FString StrValue;
		if (const FStrProperty* StrProp = CastField<FStrProperty>(EngineProperty))
		{
			const FString* StrPtr = StrProp->GetPropertyValuePtr(Container);
			StrValue = *StrPtr;
		}
		else if (const FNameProperty* NameProp = CastField<FNameProperty>(EngineProperty))
		{
			const FName* ValuePtr = NameProp->GetPropertyValuePtr(Container);
			StrValue = ValuePtr->ToString();
		}

		const int32 CharCount = StrValue.Len();

		const int32 StringSize = CharCount * sizeof(TCHAR);
		const int32 BytesToAdd = sizeof(int32) + StringSize; //Size of string integer + size of the bytes containing the string
		int32 Offset = Buffer.AddUninitialized(BytesToAdd);
		
		//FUSION_LOG("Writing string at offset: %d  StringSize: %d  ByteToAdd: %d", Offset, StringSize, BytesToAdd);

		//Copy in the string size when we read this on the other side.
		FMemory::Memcpy(Buffer.GetData() + Offset, &CharCount, sizeof(int32));
		
		if (StringSize > 0)
		{
			Offset += sizeof(int32);
			FMemory::Memcpy(Buffer.GetData() + Offset, StrValue.GetCharArray().GetData(), StringSize);
		}
	}
	else if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(EngineProperty))
	{
		//For some reason we have to
		const int32 BoolValue = BoolProp->GetPropertyValue(Container) ? 1 : 0;
		const int32 BytesToAdd = WordCount * sizeof(int32);
		const int32 Offset = Buffer.AddUninitialized(BytesToAdd);
		
		FMemory::Memcpy(Buffer.GetData() + Offset, &BoolValue, BytesToAdd);

		//FUSION_LOG("Writing BoolProp: %s At offset: %d  With Value: %d", *ResolvedProperty->GetName(), Offset, BoolValue);
	}
	else if (DataType == EFusionDataTypes::Vector)
	{
		const int32 BytesToAdd = WordCount * sizeof(int32);
		const int32 Offset = Buffer.AddUninitialized(BytesToAdd);

		const FVector* v = static_cast<FVector*>(Container);
		uint8* DataPtr = Buffer.GetData() + Offset;
		int32* WordsF = reinterpret_cast<int32*>(DataPtr);

		WordsF[0] = CompressFloat(v->X);
		WordsF[2] = CompressFloat(v->Y);
		WordsF[4] = CompressFloat(v->Z);
	}
	else if (DataType == EFusionDataTypes::Quat)
	{
		const int32 BytesToAdd = WordCount * sizeof(int32);
		const int32 Offset = Buffer.AddUninitialized(BytesToAdd);

		const FQuat* q = static_cast<FQuat*>(Container);
		uint8* DataPtr = Buffer.GetData() + Offset;
		float* WordsF = reinterpret_cast<float*>(DataPtr);

		WordsF[0] = static_cast<float>(q->X);
		WordsF[2] = static_cast<float>(q->Y);
		WordsF[4] = static_cast<float>(q->Z);
		WordsF[6] = static_cast<float>(q->W);
	}
	else if (DataType == EFusionDataTypes::Rotator)
	{
		const int32 BytesToAdd = WordCount * sizeof(int32);
		const int32 Offset = Buffer.AddUninitialized(BytesToAdd);

		const FRotator* r = static_cast<FRotator*>(Container);
		uint8* DataPtr = Buffer.GetData() + Offset;
		int32* WordsF = reinterpret_cast<int32*>(DataPtr);

		WordsF[0] = CompressFloat(r->Pitch);
		WordsF[2] = CompressFloat(r->Yaw);
		WordsF[4] = CompressFloat(r->Roll);
	}
	else if (DataType == EFusionDataTypes::ClassId)
	{
		if (const FClassProperty* ClassProp = static_cast<FClassProperty*>(EngineProperty))
		{
			if (const TObjectPtr<UObject> ClassValue = ClassProp->GetPropertyValue(Container))
			{
				if (UClass* StoredClass = Cast<UClass>(ClassValue))
				{
					FString StrValue = StoredClass->GetPathName();
					const int32 CharCount = StrValue.Len();

					const int32 StringSize = CharCount * sizeof(TCHAR);
					const int32 BytesToAdd = sizeof(int32) + StringSize; //Size of string integer + size of the bytes containing the string
					int32 Offset = Buffer.AddUninitialized(BytesToAdd);

					//Copy in the string size when we read this on the other side.
					FMemory::Memcpy(Buffer.GetData() + Offset, &CharCount, sizeof(int32));

					if (StringSize > 0)
					{
						Offset += sizeof(int32);
						FMemory::Memcpy(Buffer.GetData() + Offset, StrValue.GetCharArray().GetData(), StringSize);
					}

					/////

					if (!Client->Lookup->FindClassDescriptor(StoredClass))
					{
						FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
						
						if (const UTypeDescriptor* NewDescriptor = Client->Lookup->CreateTypeDescriptor(StoredClass, BuildOptions))
						{
							FUSION_LOG("Create new class descriptor: %s", *NewDescriptor->Type->GetName());
						}
						//FUSION_LOG("Found class descriptor: %s", *StoredClass->GetName());
					}
					//
				}
			}
			else
			{
				// Make it set to none
				const int32 Offset = Buffer.AddUninitialized(sizeof(int32));
				constexpr int32 CharCount = 0;
				FMemory::Memcpy(Buffer.GetData() + Offset, &CharCount, sizeof(int32));
			}

		}
	}
	else if (DataType == EFusionDataTypes::Int)
	{
		if (const FIntProperty* IntProp = static_cast<FIntProperty*>(EngineProperty))
		{
			auto Value = IntProp->GetPropertyValue(Container);
		}

		const int32 BytesToAdd = WordCount * sizeof(int32);
		const int32 Offset = Buffer.AddUninitialized(BytesToAdd);
		
		FMemory::Memcpy(Buffer.GetData() + Offset, Container, BytesToAdd);
	}
	else
	{
		const int32 BytesToAdd = WordCount * sizeof(int32);
		const int32 Offset = Buffer.AddUninitialized(BytesToAdd);
		
		FMemory::Memcpy(Buffer.GetData() + Offset, Container, BytesToAdd);
		
		//FUSION_LOG("Add Bytes for Property %s Of Type: %s With StartOffset: %d  And Size: %d  AddedTotalBytes: %d", *InputProperty->GetName(), *InputProperty->GetClass()->GetName(), startOffset, Size, bytesToAdd);
	}
}

void Property::Deserialize(UFusionClient* Client, uint8_t* Buffer, void* Container, const FFunctionProperty& FunctionProperty, int& CurrentOffset)
{
	void* ResolvedContainer = Container;
	FProperty* ResolvedProperty = EngineProperty;

	if (ResolvedProperty->IsA<FStrProperty>() || ResolvedProperty->IsA<FNameProperty>())
	{
		void* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<void>(ResolvedContainer);

		int32 StringSize = 0;
		FMemory::Memcpy(&StringSize, Buffer + CurrentOffset, sizeof(int32));

		//Place offset at start of the string.
		CurrentOffset += sizeof(int32);

		const TCHAR* StringData = reinterpret_cast<const TCHAR*>(Buffer + CurrentOffset);
		FString ResultString = StringSize > 0 ? FString(StringSize, StringData) : "";

		if (FStrProperty* StrProp = CastField<FStrProperty>(ResolvedProperty))
		{
			StrProp->InitializeValue(DestAddr);
			StrProp->SetPropertyValue(DestAddr, ResultString);
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(ResolvedProperty))
		{
			FName FNameValue(*ResultString);

			NameProp->InitializeValue(DestAddr);
			NameProp->SetPropertyValue(DestAddr, FNameValue);
		}

		//FUSION_LOG("Deserialize Type: %s  with size: %d  At Offset. %d  StringSize: %d String: %s", *EngineProperty->GetName(), WordCount, WordOffset, StringSize, *ResultString);

		//Set offset at the next property after the string data.
		int32 StringByteSize = StringSize * sizeof(TCHAR);
		CurrentOffset += StringByteSize;

		return;
	}

	if (DataType == EFusionDataTypes::Vector)
	{
		const int32* Compressed = reinterpret_cast<const int32*>(Buffer + CurrentOffset);

		FVector Value;
		Value.X = DecompressFloat(Compressed[0]);
		Value.Y = DecompressFloat(Compressed[2]);
		Value.Z = DecompressFloat(Compressed[4]);

		FVector* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<FVector>(ResolvedContainer);
		*DestAddr = Value;

		CurrentOffset += WordCount * sizeof(int32);
		return;
	}
	if (DataType == EFusionDataTypes::Quat)
	{
		const float* DataPtr = reinterpret_cast<const float*>(Buffer + CurrentOffset);

		FQuat Value;
		Value.X = DataPtr[0];
		Value.Y = DataPtr[2];
		Value.Z = DataPtr[4];
		Value.W = DataPtr[6];

		FQuat* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<FQuat>(ResolvedContainer);
		*DestAddr = Value;

		CurrentOffset += WordCount * sizeof(int32);
		return;
	}
	if (DataType == EFusionDataTypes::Rotator)
	{
		const int32* DataPtr = reinterpret_cast<const int32*>(Buffer + CurrentOffset);

		FRotator Value;
		Value.Pitch = DecompressFloat(DataPtr[0]);
		Value.Yaw = DecompressFloat(DataPtr[2]);
		Value.Roll = DecompressFloat(DataPtr[4]);

		FRotator* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<FRotator>(ResolvedContainer);
		*DestAddr = Value;

		CurrentOffset += WordCount * sizeof(int32);
		return;
	}
	if (DataType == EFusionDataTypes::Int)
	{
		void* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<void>(Container);
		const int32* Data = reinterpret_cast<const int32*>(Buffer + CurrentOffset);

		FIntProperty* IntProp = CastField<FIntProperty>(ResolvedProperty);
		
		IntProp->InitializeValue(DestAddr);
		IntProp->SetPropertyValue(DestAddr, *Data);
		
		CurrentOffset += WordCount * sizeof(int32);
		return;
	}
	if (DataType == EFusionDataTypes::ClassId)
	{
		int32 StringSize = 0;
		FMemory::Memcpy(&StringSize, Buffer + CurrentOffset, sizeof(int32));

		//Place offset at start of the string.
		CurrentOffset += sizeof(int32);

		UClass* Class = nullptr;

		if (StringSize > 0)
		{
			const TCHAR* StringData = reinterpret_cast<const TCHAR*>(Buffer + CurrentOffset);
			FString ClassPath(StringSize, StringData);

			//Set offset at the next property after the string data.
			int32 StringByteSize = StringSize * sizeof(TCHAR);
			CurrentOffset += StringByteSize;

			Class = LoadObject<UClass>(nullptr, *ClassPath);

			if (Class != nullptr)
			{
				checkf(ResolvedProperty != nullptr, TEXT("Unable to resolve Class Property"));

				if (!Client->Lookup->FindClassDescriptor(Class))
				{
					FPropertyBuildOptions BuildOptions = UTypeLookup::GetDefaultBuildOptions();
					if ([[maybe_unused]] UTypeDescriptor* NewDescriptor = Client->Lookup->CreateTypeDescriptor(Class, BuildOptions))
					{
						FUSION_LOG("Create new descriptor for received class: %s", *Class->GetName());
					}
				}
			}
			else
			{
				FUSION_LOG_WARN("No Class Found With Path: %s", *ClassPath);
			}
		}

		FClassProperty* ClassProp = CastField<FClassProperty>(ResolvedProperty);
		checkf(ClassProp != nullptr, TEXT("Indexed Class Property in Descriptor of the incorrect type"));

		void* DestAddr = ClassProp->ContainerPtrToValuePtr<void>(ResolvedContainer);
		UObject* CurrentValue = ClassProp->GetObjectPropertyValue(DestAddr);

		if (CurrentValue != Class)
		{
			ClassProp->SetObjectPropertyValue(DestAddr, Class);
		}

		return;
	}
	

#define HANDLE_PROP(PropClass, CppType) \
if (PropClass* Prop = CastField<PropClass>(ResolvedProperty)) \
{ \
SetPropertyValue<CppType>(Prop, ResolvedContainer, Buffer, CurrentOffset); \
CurrentOffset += WordCount * sizeof(int32); \
return; \
} 

	HANDLE_PROP(FByteProperty, uint8);
	HANDLE_PROP(FIntProperty, int32);
	HANDLE_PROP(FUInt32Property, uint32);
	HANDLE_PROP(FInt64Property, int64);
	HANDLE_PROP(FUInt64Property, uint64);
	HANDLE_PROP(FInt16Property, int16);
	HANDLE_PROP(FUInt16Property, uint16);
	HANDLE_PROP(FFloatProperty, float);
	HANDLE_PROP(FDoubleProperty, double);
	HANDLE_PROP(FBoolProperty, bool);
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(ResolvedProperty))
	{
		void* DestAddr = EnumProp->ContainerPtrToValuePtr<void>(ResolvedContainer);
		
		FProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		UnderlyingProp->InitializeValue(DestAddr);
		
		if (FByteProperty* LocalByteProp = CastField<FByteProperty>(UnderlyingProp))
		{
			const uint8 Value = *reinterpret_cast<const uint8*>(Buffer + CurrentOffset);
			LocalByteProp->SetPropertyValue(DestAddr, Value);
		}
		else if (FInt16Property* LocalInt16Prop = CastField<FInt16Property>(UnderlyingProp))
		{
			const int16 Value = *reinterpret_cast<const int16*>(Buffer + CurrentOffset);
			LocalInt16Prop->SetPropertyValue(DestAddr, Value);
		}
		else if (FUInt16Property* LocalUInt16Prop = CastField<FUInt16Property>(UnderlyingProp))
		{
			const uint16 Value = *reinterpret_cast<const uint16*>(Buffer + CurrentOffset);
			LocalUInt16Prop->SetPropertyValue(DestAddr, Value);
		}
		else if (FIntProperty* LocalIntProp = CastField<FIntProperty>(UnderlyingProp))
		{
			const int32 Value = *reinterpret_cast<const int32*>(Buffer + CurrentOffset);
			LocalIntProp->SetPropertyValue(DestAddr, Value);
		}
		else if (FUInt32Property* LocalUIntProp = CastField<FUInt32Property>(UnderlyingProp))
		{
			const uint32 Value = *reinterpret_cast<const uint32*>(Buffer + CurrentOffset);
			LocalUIntProp->SetPropertyValue(DestAddr, Value);
		}
		else
		{
			FUSION_LOG("Enum has unknown underlying type: %s", *UnderlyingProp->GetClass()->GetName());
		}

		CurrentOffset += WordCount * sizeof(int32); \
	}
}

StructProperty::~StructProperty()
{
	FMemory::Free(ValueBuffer);
	ValueBuffer = nullptr;
}

StructProperty::StructProperty(const FStructProperty* Prop)
{
	const int32 PropSize = Prop->GetSize();
	const int32 PropAlignment = Prop->GetMinAlignment();

	ValueBuffer = static_cast<uint8*>(FMemory::Malloc(PropSize, PropAlignment));
		
	Prop->InitializeValue(ValueBuffer);
}

Property::~Property()
{
	while (SubProperties.Num() > 0)
	{
		const Property* Property = SubProperties[0];
		SubProperties.RemoveAt(0);
		delete Property;
	}
	
	SubProperties.Empty();
}
