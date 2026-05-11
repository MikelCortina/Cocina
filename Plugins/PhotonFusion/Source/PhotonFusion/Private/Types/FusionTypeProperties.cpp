// Copyright 2026 Exit Games GmbH. All Rights Reserved.
// ReSharper disable CppUnusedIncludeDirective

#include "Types/FusionTypeProperties.h"

#include <Fusion/LogUtils.h>

#include "Misc/AssertionMacros.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "FusionClient.h"
#include "FusionShared.h"
#include "FusionUtils.h"
#include "UObject/UObjectBaseUtility.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Types/FusionTypeDescriptor.h"
#include "Misc/EngineVersionComparison.h"
#include "Types/FusionPropertyHelpers.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/GameEngine.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
// ReSharper restore CppUnusedIncludeDirective

static int32 CompressFloat(float F)
{
	return *reinterpret_cast<uint32*>(&F);
}

static float DecompressFloat(int32 F)
{
	return *reinterpret_cast<float*>(&F);
}

static FString ReadString(SharedMode::ReadBuffer& Buffer)
{
	const int32 CharCount = Buffer.Int();

	if (CharCount > 0)
	{
		const int32 StringByteSize = CharCount * sizeof(TCHAR);
		TArray<uint8> RawBytes;
		RawBytes.SetNumUninitialized(StringByteSize);
		for (int32 i = 0; i < StringByteSize; i++)
		{
			RawBytes[i] = Buffer.Byte();
		}
		return FString(CharCount, reinterpret_cast<const TCHAR*>(RawBytes.GetData()));
	}

	return FString();
}

static void AddString(SharedMode::WriteBuffer& Buffer, const FString& StrValue)
{
	const int32 CharCount = StrValue.Len();
	Buffer.Int(CharCount);

	if (CharCount > 0)
	{
		const int32 StringByteSize = CharCount * sizeof(TCHAR);
		const uint8* RawBytes = reinterpret_cast<const uint8*>(StrValue.GetCharArray().GetData());
		for (int32 i = 0; i < StringByteSize; i++)
		{
			Buffer.Byte(RawBytes[i]);
		}
	}
}

static void WriteWords(SharedMode::WriteBuffer& Buffer, const void* Data, int32 WordCount)
{
	const int32* Words = static_cast<const int32*>(Data);
	for (int32 i = 0; i < WordCount; i++)
	{
		Buffer.Int(Words[i]);
	}
}

static void ReadWords(SharedMode::ReadBuffer& Buffer, void* Data, int32 WordCount)
{
	int32* Words = static_cast<int32*>(Data);
	for (int32 i = 0; i < WordCount; i++)
	{
		Words[i] = Buffer.Int();
	}
}

void AddOneInt(SharedMode::WriteBuffer& Buffer, const uint32 Value)
{
	Buffer.UInt(Value);
}

void AddTwoInts(SharedMode::WriteBuffer& Buffer, const uint32 IntOne, const uint32 IntTwo)
{
	Buffer.UInt(IntOne);
	Buffer.UInt(IntTwo);
}

void AddObjectType(SharedMode::WriteBuffer& Buffer, const EFusionEncodedObjectType Type)
{
	Buffer.Int(static_cast<int32>(Type));
}

FString GetSerializedLevelPath(const UObject* Owner, const UActorComponent* ActorComponent)
{
	const unsigned int ObjectHash = UFusionHelpers::SafeObjectNameHash(Owner);
	//const unsigned int Hash = UFusionHelpers::GetObjectHash(Owner);
	const FString ComponentName = ActorComponent ? ActorComponent->GetName() : "";

	FString SerializedLevelPath = FString::Printf(TEXT("%u:%s"), ObjectHash, *ComponentName);
	return SerializedLevelPath;
}

void Property::CopyTo(UFusionClient* Client, FCopyContext& Context, FPropertyWordState& PropertyState, void* Container, int* Words) const
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
				const FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(Words);
				FString Path = "";
				
				if (const TObjectPtr<UObject> ClassValue = ClassProp->GetPropertyValue(ResolvedContainer))
				{
					//FUSION_LOG("ClassProp: %s", *ClassValue->GetName());
					if (UClass* StoredClass = Cast<UClass>(ClassValue))
					{
						Path = StoredClass->GetPathName();

						if (!Client->Lookup->FindClassDescriptor(StoredClass))
						{
							FPropertyBuildOptions BuildOptions = UFusionTypeLookup::GetDefaultBuildOptions();
							
							if (const UFusionTypeDescriptor* NewDescriptor = Client->Lookup->CreateTypeDescriptor(
								StoredClass, BuildOptions))
							{
								FUSION_LOG("Create new class descriptor: %s", *NewDescriptor->Type->GetName());
							}
							//FUSION_LOG("Found class descriptor: %s", *StoredClass->GetName());
						}
					}
				}

				const FusionCore::StringHandle Handle = UFusionPropertyHelpers::EncodeString(Context.Pair.Object, PropertyState, Path, *ExistingHandle);
				std::memcpy(Words, &Handle, sizeof(FusionCore::StringHandle));
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

			const FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(Words);
			const FusionCore::StringHandle Handle = UFusionPropertyHelpers::EncodeString(Context.Pair.Object, PropertyState, StoredString, *ExistingHandle);

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

void Property::CleanupPreviousState(UFusionClient* Client, const FCopyContext& Context, FPropertyWordState& PropertyState, void* Container, int* Words)
{
	// ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
	// ReSharper disable once CppIncompleteSwitchStatement
	switch (DataType)
	{
	case EFusionDataTypes::ClassId:
	case EFusionDataTypes::Name:
	case EFusionDataTypes::String:
		{
			PropertyState.PropertyReferenceLength = 0;
			PropertyState.PropertyStateHash = 0;
			
			const FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(Words);
			const FusionCore::StringHandle Handle = Context.Pair.Object->FreeString(*ExistingHandle);
			std::memcpy(Words, &Handle, sizeof(FusionCore::StringHandle)); //Make sure to wipe the state handle.
		}
		break;
	}
}

bool Property::CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow) const
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
				
				*Ptr = Words[0];
				Changed = true;
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
				
				*Ptr = *reinterpret_cast<uint64*>(Words);
				Changed = true;
			}
		}
		break;

	case EFusionDataTypes::ClassId:
		{
			FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(Words);

			FusionCore::StringMessage OutStringStatus{};
			const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
			EFusionEncodedStringStatus Status = UFusionPropertyHelpers::GetEncodedStringStatus(OutStringStatus);
			
			if (Status != EFusionEncodedStringStatus::Valid)
			{
				FString StatusString = StaticEnum<EFusionEncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
				FUSION_LOG_ERROR("Encoded string has error: %s", *StatusString);
				return false;
			}

			if (OutStringStatus == FusionCore::StringMessage::EmptyHeap)
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
						FPropertyBuildOptions BuildOptions = UFusionTypeLookup::GetDefaultBuildOptions();
						if ([[maybe_unused]] UFusionTypeDescriptor* NewDescriptor = Client->Lookup->CreateTypeDescriptor(
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
				
				*Ptr = NewValue;
				Changed = true;
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
				
				*Ptr = NewValue;
				Changed = true;
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

				*Ptr = NewValue;
				Changed = true;
			}
		}
		break;
	case EFusionDataTypes::Name:
	case EFusionDataTypes::String:
		{
			FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(Words);
			
			FusionCore::StringMessage OutStringStatus{};
			const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
			const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> StringPtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(StringPtr));
			EFusionEncodedStringStatus Status = UFusionPropertyHelpers::GetEncodedStringStatus(OutStringStatus);
			
			if (Status != EFusionEncodedStringStatus::Valid)
			{
				FString StatusString = StaticEnum<EFusionEncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
				FUSION_LOG_ERROR("Encoded string has error: %s", *StatusString);
				return false;
			}

			if (OutStringStatus == FusionCore::StringMessage::EmptyHeap)
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

void StructProperty::CopyTo(UFusionClient* Client, FCopyContext& CopyRoot, FPropertyWordState& PropertyState, void* Container, int* Words) const
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
		
		ItemProperty->CopyTo(Client, CopyRoot, PropertyState.SubPropertyStates[j], ResolvedContainer, TargetAddress);
	}
}

bool StructProperty::CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow) const
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
		
		Changed |= ItemProperty->CopyFrom(Client, Context, ResolvedContainer, TargetAddress, ShadowAddress);
	}

	if (Changed && RepNotify)
	{
		Context.AddRepFunction(RepNotify, ResolvedProperty, ValueBuffer);
	}

	return Changed;
}

void StructProperty::CleanupPreviousState(UFusionClient* Client, const FCopyContext& Context, FPropertyWordState& PropertyState, void* Container, int* Words)
{
	check(EngineProperty);
	
	void* ResolvedContainer = Container ? EngineProperty->ContainerPtrToValuePtr<uint8>(Container) : nullptr;
	
	for (int j = 0; j < SubProperties.Num(); j++)	
	{
		Property* ItemProperty = SubProperties[j];
		int* TargetAddress = Words + ItemProperty->WordOffset;
		
		ItemProperty->CleanupPreviousState(Client, Context, PropertyState.SubPropertyStates[j], ResolvedContainer, TargetAddress);
	}
}

void StructProperty::Serialize(UFusionClient* Client, void* Container, const FFusionFunctionProperty& FunctionProperty, SharedMode::WriteBuffer& Buffer, bool RootArgument)
{
	check(EngineProperty);

	for (int j = 0; j < SubProperties.Num(); j++) {
		Property* ItemProperty = SubProperties[j];
		const int32 Offset = ItemProperty->EngineProperty ? ItemProperty->EngineProperty->GetOffset_ForInternal() : 0;
		uint8* SubPropertyContainer = static_cast<uint8*>(Container) + Offset;
		ItemProperty->Serialize(Client, SubPropertyContainer, FunctionProperty, Buffer, false);
	}
}

void StructProperty::Deserialize(UFusionClient* Client, SharedMode::ReadBuffer& Buffer, void* Container,
	const FFusionFunctionProperty& FunctionProperty)
{
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	ResolveContainerAndProperty(Container, ResolvedContainer, ResolvedProperty);

	for (int j = 0; j < SubProperties.Num(); j++)
	{
		Property* ItemProperty = SubProperties[j];
		ItemProperty->Deserialize(Client, Buffer, ResolvedContainer, FunctionProperty);
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
	const EFusionEncodedObjectType PreviousType = static_cast<EFusionEncodedObjectType>(Words[0]);

	if (PreviousType == EFusionEncodedObjectType::MapObjectComponentStringPath ||
						PreviousType == EFusionEncodedObjectType::NetworkedObjectComponentStringPath ||
						PreviousType == EFusionEncodedObjectType::ObjectPath ||
						PreviousType == EFusionEncodedObjectType::ClassPath ||
						PreviousType == EFusionEncodedObjectType::BlueprintCDOClass ||
						PreviousType == EFusionEncodedObjectType::SoftObjectPath)
	{
		int* StringTargetAddress = Words + 1;
		const FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(StringTargetAddress);
		FusionCore::StringHandle resultHandle = Context.Pair.Object->FreeString(*ExistingHandle);
	}
}

void ObjectProperty::CopyTo(UFusionClient* Client, FCopyContext& Context, FPropertyWordState& PropertyState, void* Container, int* Words) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FusionClient::CopyTo::Object);
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	if (!ResolveObjectProperty(Container, ResolvedContainer, ResolvedProperty))
		return;
	
	UObject* TargetObject {nullptr};
	switch (PointerType)
	{
		case EFusionObjectPointerType::ObjectPointer:
			{
				const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(ResolvedProperty);
				const TObjectPtr<UObject> ObjectPtr = ObjectProperty->GetPropertyValue(ResolvedContainer);
				TargetObject = ObjectPtr.Get();
			}
		break;
	case EFusionObjectPointerType::WeakObjectPointer:
		{
			const FWeakObjectProperty* ObjectProperty = CastField<FWeakObjectProperty>(ResolvedProperty);
			const FWeakObjectPtr ObjectPtr = ObjectProperty->GetPropertyValue(ResolvedContainer);
			TargetObject = ObjectPtr.Get();
		}
		break;
	case EFusionObjectPointerType::SoftObjectPointer:
		{
			const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(ResolvedProperty);
			const FSoftObjectPtr SoftPtr = SoftProperty->GetPropertyValue(ResolvedContainer);
			const FSoftObjectPath& SoftPath = SoftPtr.GetUniqueID();

			if (SoftPath.IsValid())
			{
				Words[0] = static_cast<int32>(EFusionEncodedObjectType::SoftObjectPath);
				
				int* StringTargetAddress = Words + 1;
				const FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(StringTargetAddress);

				// If previous encoding was non-string (e.g. NetworkedObject), words contain ObjectId data, not a valid string handle.
				if (!Context.Pair.Object->IsValidStringHandle(*ExistingHandle))
				{
					Words[1] = 0;
					Words[2] = 0;
				}

				const FString Path = SoftPath.ToString();
				const FusionCore::StringHandle Handle = UFusionPropertyHelpers::EncodeString(Context.Pair.Object, PropertyState, Path, *ExistingHandle);
				std::memcpy(StringTargetAddress, &Handle, sizeof(FusionCore::StringHandle));
			}
			else
			{
				CheckReleaseString(Context, Words);

				Words[0] = static_cast<int32>(EFusionEncodedObjectType::None);
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

		if (const FusionCore::Object* FoundObject = Client->FindObject(TargetObject))
		{
			//In case previous encoding was pointing to string heap, release first.
			CheckReleaseString(Context, Words);
			
			Words[0] = static_cast<int32>(EFusionEncodedObjectType::NetworkedObject);
			memcpy(&Words[1], &FoundObject->Id, sizeof(FusionCore::ObjectId));

			return;
		}
	
		if (const UActorComponent* ActorComponent = Cast<UActorComponent>(TargetObject))
		{
			if (ActorComponent->GetOwner()->GetLevel())
			{
				FString ComponentPath;

				if (const FusionCore::ObjectId OwnerId = Client->FindObjectId(ActorComponent->GetOwner()); OwnerId.IsSome())
				{
					Words[0] = static_cast<int32>(EFusionEncodedObjectType::NetworkedObjectComponentStringPath);
					ComponentPath = FString::Printf(TEXT("%u:%u:%s"), OwnerId.Origin, OwnerId.Counter, *ActorComponent->GetName());
				}
				else
				{
					Words[0] = static_cast<int32>(EFusionEncodedObjectType::MapObjectComponentStringPath);
					ComponentPath = GetSerializedLevelPath(ActorComponent->GetOwner(), ActorComponent);
				}

				int* TargetAddress = Words + 1;
				const FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(TargetAddress);

				// If previous encoding was non-string (e.g. NetworkedObject), words contain ObjectId data, not a valid string handle.
				if (!Context.Pair.Object->IsValidStringHandle(*ExistingHandle))
				{
					Words[1] = 0;
					Words[2] = 0;
				}

				const FusionCore::StringHandle Handle = UFusionPropertyHelpers::EncodeString(Context.Pair.Object, PropertyState, ComponentPath, *ExistingHandle);
				std::memcpy(TargetAddress, &Handle, sizeof(FusionCore::StringHandle));
			}
			else
			{
				CheckReleaseString(Context, Words);
				
				Words[0] = static_cast<int32>(EFusionEncodedObjectType::None);
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
				Words[0] = static_cast<int32>(EFusionEncodedObjectType::MapObjectHash);
				Words[1] = Hash;
				Words[2] = 0;
			}
			else
			{
				Words[0] = static_cast<int32>(EFusionEncodedObjectType::None);
				Words[1] = 0;
				Words[2] = 0;
			}
			return;
		}

		//Exhaustive check to determine if object is a runtime created instance or if it's loaded from an asset (don't want objects for those)
		if (!IsClassPointer && IsObjectTransient(TargetObject))
		{
			CheckReleaseString(Context, Words);

			FPropertyBuildOptions BuildOptions = UFusionTypeLookup::GetDefaultBuildOptions();
			const UFusionTypeDescriptor* Descriptor = Client->Lookup->CreateTypeDescriptor(TypeClass, BuildOptions);
		
			if (FusionCore::Object* CustomObject = Client->CreateCustomObject(Context, TargetObject, Descriptor, Client->CurrentMapInstance.Sequence))
			{
				//Ensure local client has this set immediately.
				CustomObject->Engine = TargetObject;

				Words[0] = static_cast<int32>(EFusionEncodedObjectType::NetworkedObject);
				memcpy(&Words[1], &CustomObject->Id, sizeof(FusionCore::ObjectId));
			}
			else
			{
				Words[0] = static_cast<int32>(EFusionEncodedObjectType::None);
				Words[1] = 0;
				Words[2] = 0;
			}
			return;
		}

		int* StringTargetAddress = Words + 1;
		const FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(StringTargetAddress);

		// If previous encoding was non-string (e.g. NetworkedObject), words contain ObjectId data, not a valid string handle.
		if (!Context.Pair.Object->IsValidStringHandle(*ExistingHandle))
		{
			Words[1] = 0;
			Words[2] = 0;
		}

		if (IsClassPointer)
		{
			Words[0] = static_cast<int32>(EFusionEncodedObjectType::ClassPath);

			const FString Path = TypeClass->GetPathName();
			const FusionCore::StringHandle Handle = UFusionPropertyHelpers::EncodeString(Context.Pair.Object, PropertyState, Path, *ExistingHandle);
			std::memcpy(StringTargetAddress, &Handle, sizeof(FusionCore::StringHandle));
		}
		else if (const UPackage* Package = TargetObject->GetOutermost())
		{
			if (!Package->HasAnyPackageFlags(PKG_InMemoryOnly))
			{
				if ([[maybe_unused]] bool bIsBlueprintGenerated = TypeClass->IsChildOf(UBlueprintGeneratedClass::StaticClass())) {
					Words[0] = static_cast<int32>(EFusionEncodedObjectType::BlueprintCDOClass);

					const FString Path = TypeClass->GetPathName();
					const FusionCore::StringHandle Handle = UFusionPropertyHelpers::EncodeString(Context.Pair.Object, PropertyState, Path, *ExistingHandle);
					std::memcpy(StringTargetAddress, &Handle, sizeof(FusionCore::StringHandle));
				}
				else {
					Words[0] = static_cast<int32>(EFusionEncodedObjectType::ObjectPath);

					const FString Path = TargetObject->GetPathName();
					const FusionCore::StringHandle Handle = UFusionPropertyHelpers::EncodeString(Context.Pair.Object, PropertyState, Path, *ExistingHandle);
					std::memcpy(StringTargetAddress, &Handle, sizeof(FusionCore::StringHandle));
				}
			}
		}
		else
		{
			CheckReleaseString(Context, Words);
			
			Words[0] = static_cast<int32>(EFusionEncodedObjectType::None);
			Words[1] = 0;
			Words[2] = 0;
		}
		return;
	}


	CheckReleaseString(Context, Words);
	
	Words[0] = static_cast<int32>(EFusionEncodedObjectType::None);
	Words[1] = 0;
	Words[2] = 0;
}

void ObjectProperty::Serialize(UFusionClient* Client, void* Container, const FFusionFunctionProperty& FunctionProperty, SharedMode::WriteBuffer& Buffer, bool RootArgument)
{
	if (EngineProperty && Container)
	{
		UObject* TargetObject{nullptr};
		switch (PointerType)
		{
			case EFusionObjectPointerType::ObjectPointer:
				{
					const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(EngineProperty);
					const TObjectPtr<UObject> ObjectPtr = ObjectProperty->GetPropertyValue(Container);
					TargetObject = ObjectPtr.Get();
				}
				break;
			case EFusionObjectPointerType::WeakObjectPointer:
				{
					const FWeakObjectProperty* ObjectProperty = CastField<FWeakObjectProperty>(EngineProperty);
					const FWeakObjectPtr ObjectPtr = ObjectProperty->GetPropertyValue(Container);
					TargetObject = ObjectPtr.Get();
				}
				break;
			case EFusionObjectPointerType::SoftObjectPointer:
				{
					const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(EngineProperty);
					const FSoftObjectPtr SoftPtr = SoftProperty->GetPropertyValue(Container);
					const FSoftObjectPath& SoftPath = SoftPtr.GetUniqueID();

					if (SoftPath.IsValid())
					{
						AddObjectType(Buffer, EFusionEncodedObjectType::SoftObjectPath);
						AddString(Buffer, SoftPath.ToString());
					}
					else
					{
						AddObjectType(Buffer, EFusionEncodedObjectType::SoftObjectPath);
						Buffer.Int(0);
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
				if (const FusionCore::Object* FoundSubObject = Client->FindObject(Actor))
				{
					AddObjectType(Buffer, EFusionEncodedObjectType::NetworkedObject);

					Buffer.WriteObjectId(FoundSubObject->Id);
				}
				else
				{
					AddObjectType(Buffer, EFusionEncodedObjectType::MapObjectHash);

					const unsigned int ObjectHash = UFusionHelpers::SafeObjectNameHash(Actor);
					AddOneInt(Buffer, ObjectHash);
				}

				return;
			}
			if (UActorComponent* Component = Cast<UActorComponent>(TargetObject))
			{
				if (const FusionCore::Object* FoundSubObject = Client->FindObject(Component))
				{
					AddObjectType(Buffer, EFusionEncodedObjectType::NetworkedObject);

					Buffer.WriteObjectId(FoundSubObject->Id);
				}
				else
				{
					if (Component->GetOwner()->GetLevel())
					{
						AddObjectType(Buffer, EFusionEncodedObjectType::MapObjectComponentStringPath);
						
						const FString SerializedLevelPath = GetSerializedLevelPath(Component->GetOwner(), Component);
						AddString(Buffer, SerializedLevelPath);
					}
					else
					{
						AddObjectType(Buffer, EFusionEncodedObjectType::None);
					}
				}

				return;
			}

			if (const FusionCore::ObjectId ObjectId = Client->FindObjectId(TargetObject); ObjectId.IsSome())
			{
				AddObjectType(Buffer, EFusionEncodedObjectType::NetworkedObject);
				Buffer.WriteObjectId(ObjectId);
				return;
			}
			
			const unsigned int ObjectHash = UFusionHelpers::SafeObjectNameHash(TargetObject);
			if ([[maybe_unused]] TObjectPtr<AActor>* FoundActor = Client->MapActors.Find(ObjectHash))
			{
				AddObjectType(Buffer, EFusionEncodedObjectType::MapObjectHash);
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
						AddObjectType(Buffer, EFusionEncodedObjectType::BlueprintCDOClass);
						AddString(Buffer, TypeClass->GetPathName());
					}
					else
					{
						AddObjectType(Buffer, EFusionEncodedObjectType::ObjectPath);
						AddString(Buffer, TargetObject->GetPathName());
					}
					return;
				}
			}
		}

		AddObjectType(Buffer, EFusionEncodedObjectType::None);
	}
}

bool ObjectProperty::CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow) const
{
	void* ResolvedContainer{nullptr};
	FProperty* ResolvedProperty{nullptr};
	if (!ResolveObjectProperty(Container, ResolvedContainer, ResolvedProperty))
		return false;

	const EFusionEncodedObjectType ObjectLoadType = static_cast<EFusionEncodedObjectType>(Words[0]);
	bool Changed = false;
	UObject* NewValue{nullptr};
	
	if (ObjectLoadType == EFusionEncodedObjectType::MapObjectHash) //Reserved for map actors
	{
		uint32 ObjectHash = static_cast<uint32>(Words[1]);

		if (TObjectPtr<AActor>* FoundActor = Client->MapActors.Find(ObjectHash))
		{
			NewValue = FoundActor->Get();
		}
	}
	else if (ObjectLoadType == EFusionEncodedObjectType::NetworkedObject)
	{
		FusionCore::ObjectId Id;
		memcpy(&Id, &Words[1], sizeof(FusionCore::ObjectId));

		if (Id.IsSome()) {
			if (FFusionObjectActorPair FoundPair = Client->FindObjectPair(Id); FoundPair.IsValid())
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
	else if (ObjectLoadType == EFusionEncodedObjectType::MapObjectComponentStringPath || ObjectLoadType == EFusionEncodedObjectType::NetworkedObjectComponentStringPath)
	{
		int* TargetAddress = Words + 1;
		FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(TargetAddress);
			
		FusionCore::StringMessage OutStringStatus{};
		const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
		const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> StringPtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(StringPtr));
		EFusionEncodedStringStatus Status = UFusionPropertyHelpers::GetEncodedStringStatus(OutStringStatus);
			
		if (Status != EFusionEncodedStringStatus::Valid)
		{
			FString StatusString = StaticEnum<EFusionEncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
			FUSION_LOG_ERROR("Encoded string has error: %s   Object: %s", *StatusString, *Context.Pair.EngineObject->GetName());
			return false;
		}

		if (OutStringStatus == FusionCore::StringMessage::EmptyHeap)
		{
			return false;
		}

		bool EmptyString = ExistingHandle->id == UINT32_MAX;
		
		if (!EmptyString && StringPtr)
		{
			FString Path = FString(StringPtrAsTchar.Get());

			if (ObjectLoadType == EFusionEncodedObjectType::MapObjectComponentStringPath)
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
			else if (ObjectLoadType == EFusionEncodedObjectType::NetworkedObjectComponentStringPath)
			{
				const int32 FirstColon = Path.Find(TEXT(":"));
				const int32 SecondColon = (FirstColon != INDEX_NONE) ? Path.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstColon + 1) : INDEX_NONE;

				if (FirstColon != INDEX_NONE && SecondColon != INDEX_NONE)
				{
					uint32 ActorOrigin = FCString::Strtoui64(*Path, nullptr, 10);
					uint32 ActorCounter = FCString::Strtoui64(*Path + FirstColon + 1, nullptr, 10);

					if (FusionCore::ObjectId Id = FusionCore::ObjectId(static_cast<FusionCore::PlayerId>(ActorOrigin), 0, ActorCounter); Id.IsSome())
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
	else if (ObjectLoadType == EFusionEncodedObjectType::ObjectPath ||
			ObjectLoadType == EFusionEncodedObjectType::ClassPath ||
			ObjectLoadType == EFusionEncodedObjectType::BlueprintCDOClass)
	{
		int* TargetAddress = Words + 1;
		FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(TargetAddress);
			
		FusionCore::StringMessage OutStringStatus{};
		const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
		const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> StringPtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(StringPtr));
		EFusionEncodedStringStatus Status = UFusionPropertyHelpers::GetEncodedStringStatus(OutStringStatus);
			
		if (Status != EFusionEncodedStringStatus::Valid)
		{
			FString StatusString = StaticEnum<EFusionEncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
			FUSION_LOG_ERROR("Encoded string has error: %s   Object: %s", *StatusString, *Context.Pair.EngineObject->GetName());
			return false;
		}

		if (OutStringStatus == FusionCore::StringMessage::EmptyHeap)
		{
			return false;
		}

		bool EmptyString = ExistingHandle->id == UINT32_MAX;
		
		if (!EmptyString && StringPtr)
		{
			FString Path = FString(StringPtrAsTchar.Get());

			if (ObjectLoadType == EFusionEncodedObjectType::ObjectPath)
			{
				NewValue = LoadObject<UObject>(nullptr, *Path);
			}
			else if (ObjectLoadType == EFusionEncodedObjectType::ClassPath)
			{
				NewValue = LoadObject<UClass>(nullptr, *Path);
			}
			else if (ObjectLoadType == EFusionEncodedObjectType::BlueprintCDOClass)
			{
				
			}
		}
	}
	else if (ObjectLoadType == EFusionEncodedObjectType::SoftObjectPath)
	{
		if (FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(ResolvedProperty))
		{
			int* TargetAddress = Words + 1;
			FusionCore::StringHandle* ExistingHandle = reinterpret_cast<FusionCore::StringHandle*>(TargetAddress);

			FusionCore::StringMessage OutStringStatus{};
			const PhotonCommon::CharType* StringPtr = Context.Pair.Object->ResolveString(*ExistingHandle, OutStringStatus);
			EFusionEncodedStringStatus Status = UFusionPropertyHelpers::GetEncodedStringStatus(OutStringStatus);

			if (Status != EFusionEncodedStringStatus::Valid)
			{
				FString StatusString = StaticEnum<EFusionEncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
				FUSION_LOG_ERROR("Encoded string has error: %s   Object: %s", *StatusString, *Context.Pair.EngineObject->GetName());
				return false;
			}

			if (OutStringStatus == FusionCore::StringMessage::EmptyHeap)
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
		case EFusionObjectPointerType::ObjectPointer:
			{
				FObjectProperty* ObjectProperty = CastField<FObjectProperty>(ResolvedProperty);
				Changed |= SetObjectPropertyValueIfDifferent<FObjectProperty>(Context, ObjectProperty, ResolvedContainer, NewValue);
			}
			break;
		case EFusionObjectPointerType::WeakObjectPointer:
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

void ObjectProperty::Deserialize(UFusionClient* Client, SharedMode::ReadBuffer& Buffer, void* Container,
	const FFusionFunctionProperty& FunctionProperty)
{
	void* ResolvedContainer{ nullptr };
	FProperty* ResolvedProperty{ nullptr };

	if (!ResolveObjectProperty(Container, ResolvedContainer, ResolvedProperty))
		return;

	[[maybe_unused]] void* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<void>(ResolvedContainer);

	const EFusionEncodedObjectType ObjectType = static_cast<EFusionEncodedObjectType>(Buffer.Int());

	UObject* NewValue{ nullptr };

	if (ObjectType == EFusionEncodedObjectType::None)
	{
		//No nothing for now
	}
	else if (ObjectType == EFusionEncodedObjectType::MapObjectHash)
	{
		const uint32 ObjectHash = Buffer.UInt();
		if (const TObjectPtr<AActor>* FoundActor = Client->MapActors.Find(ObjectHash))
		{
			NewValue = FoundActor->Get();
		}
	}
	else if (ObjectType == EFusionEncodedObjectType::NetworkedObject)
	{
		FusionCore::ObjectId Id = Buffer.ReadObjectId();

		if (Id.IsSome()) {
			if (const FFusionObjectActorPair FoundPair = Client->FindObjectPair(Id); FoundPair.IsValid())
			{
				NewValue = FoundPair.EngineObject;
			}
		}
	}
	else if (ObjectType == EFusionEncodedObjectType::MapObjectComponentStringPath)
	{
		FString Path = ReadString(Buffer);

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
	else if (ObjectType == EFusionEncodedObjectType::ObjectPath)
	{
		FString Path = ReadString(Buffer);

		NewValue = LoadObject<UObject>(nullptr, *Path);
	}
	else if (ObjectType == EFusionEncodedObjectType::ClassPath)
	{
		FString Path = ReadString(Buffer);

		NewValue = LoadObject<UClass>(nullptr, *Path);
	}
	else if (ObjectType == EFusionEncodedObjectType::BlueprintCDOClass)
	{
		FString Path = ReadString(Buffer);

		if (const UClass* Class = LoadObject<UClass>(nullptr, *Path))
		{
			//Loading class path, probably blueprint.
			UObject* CDO = Class->GetDefaultObject<UObject>();
			NewValue = CDO;
		}
	}
	else if (ObjectType == EFusionEncodedObjectType::SoftObjectPath)
	{
		FString Path = ReadString(Buffer);

		if (PointerType == EFusionObjectPointerType::SoftObjectPointer)
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
		case EFusionObjectPointerType::ObjectPointer:
		{
			const FObjectProperty* ObjProp = static_cast<FObjectProperty*>(ResolvedProperty);
			ObjProp->SetObjectPropertyValue(ResolvedContainer, NewValue);
		}
		break;
		case EFusionObjectPointerType::WeakObjectPointer:
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

void ArrayProperty::CopyTo(UFusionClient* Client, FCopyContext& Context, FPropertyWordState& PropertyState, void* Container, int* Words) const
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

		const int32 SubPropCount = SubProperties.Num();
		
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
					
					ItemProperty->CleanupPreviousState(Client, Context, PropertyState.SubPropertyStates[i * SubPropCount + j], ElementAddr, TargetAddress);
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

			for (int j = 0; j < SubPropCount; j++) {
				const Property* ItemProperty = SubProperties[j];
				int* TargetAddress = Words + ItemProperty->WordOffset + CurrentStride;

				ItemProperty->CopyTo(Client, Context, PropertyState.SubPropertyStates[i * SubPropCount + j], ElementAddr, TargetAddress);
			}

			CurrentStride += StrideOffset;
		}

		//FUSION_LOG("ArrayName: %s Size: %d ", *ArrayProp->GetName(), ArrayHelper.Num());
	}
}

bool ArrayProperty::CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow) const
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
				
				Changed |= ItemProperty->CopyFrom(Client, Context, ElementAddr, TargetAddress, ShadowAddress);
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

void ArrayProperty::CleanupPreviousState(UFusionClient* Client, const FCopyContext& Context, FPropertyWordState& PropertyState, void* Container, int* Words)
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

	const int32 SubPropCount = SubProperties.Num();
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
			
			ItemProperty->CleanupPreviousState(Client, Context, PropertyState.SubPropertyStates[i * SubPropCount + j], ElementAddr, TargetAddress);
		}

		CurrentStride += StrideOffset;
	}
}

void ArrayProperty::Serialize(UFusionClient* Client, void* Container, const FFusionFunctionProperty& FunctionProperty, SharedMode::WriteBuffer& Buffer,
                              bool RootArgument)
{
	const FArrayProperty* ArrayProp = static_cast<FArrayProperty*>(EngineProperty);

	FScriptArrayHelper ArrayHelper(ArrayProp, Container);
	const int Size = Container ? ArrayHelper.Num() : 0;
	
	//Encode array size into buffer
	Buffer.Int(Size);

	for (int i = 0; i < Size; i++)
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

void ArrayProperty::Deserialize(UFusionClient* Client, SharedMode::ReadBuffer& Buffer, void* Container, const FFusionFunctionProperty& FunctionProperty)
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

	const int32 ArraySize = Buffer.Int();

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

			ItemProperty->Deserialize(Client, Buffer, SubPropertyContainer, FunctionProperty);
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

bool FusionArrayProperty::CopyFrom(UFusionClient* Client, FCopyContext& CopyRoot, void* Container, int* Words, int* Shadow) const
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
					
					ElementChanged |= ItemProperty->CopyFrom(Client, CopyRoot, ElementAddr, TargetAddress, ShadowAddress);
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
				if (const FFusionArrayHooks* Hooks = UFusionTypeDescriptorLibrary::HooksMap.Find(
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
				if (const FFusionArrayHooks* Hooks = UFusionTypeDescriptorLibrary::HooksMap.Find(
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
				if (const FFusionArrayHooks* Hooks = UFusionTypeDescriptorLibrary::HooksMap.Find(
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

void Property::Serialize(UFusionClient* Client, void* Container, const FFusionFunctionProperty& FunctionProperty, SharedMode::WriteBuffer& Buffer, bool RootArgument)
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

		AddString(Buffer, StrValue);
	}
	else if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(EngineProperty))
	{
		const int32 BoolValue = BoolProp->GetPropertyValue(Container) ? 1 : 0;
		Buffer.Int(BoolValue);
		for (int32 i = 1; i < WordCount; i++) Buffer.Int(0);
	}
	else if (DataType == EFusionDataTypes::Vector)
	{
		const FVector* v = static_cast<FVector*>(Container);
		TArray<int32> Words;
		Words.SetNumZeroed(WordCount);
		Words[0] = CompressFloat(v->X);
		Words[2] = CompressFloat(v->Y);
		Words[4] = CompressFloat(v->Z);
		WriteWords(Buffer, Words.GetData(), WordCount);
	}
	else if (DataType == EFusionDataTypes::Quat)
	{
		const FQuat* q = static_cast<FQuat*>(Container);
		TArray<int32> Words;
		Words.SetNumZeroed(WordCount);
		float* WordsF = reinterpret_cast<float*>(Words.GetData());
		WordsF[0] = static_cast<float>(q->X);
		WordsF[2] = static_cast<float>(q->Y);
		WordsF[4] = static_cast<float>(q->Z);
		WordsF[6] = static_cast<float>(q->W);
		WriteWords(Buffer, Words.GetData(), WordCount);
	}
	else if (DataType == EFusionDataTypes::Rotator)
	{
		const FRotator* r = static_cast<FRotator*>(Container);
		TArray<int32> Words;
		Words.SetNumZeroed(WordCount);
		Words[0] = CompressFloat(r->Pitch);
		Words[2] = CompressFloat(r->Yaw);
		Words[4] = CompressFloat(r->Roll);
		WriteWords(Buffer, Words.GetData(), WordCount);
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
					AddString(Buffer, StrValue);

					if (!Client->Lookup->FindClassDescriptor(StoredClass))
					{
						FPropertyBuildOptions BuildOptions = UFusionTypeLookup::GetDefaultBuildOptions();

						if (const UFusionTypeDescriptor* NewDescriptor = Client->Lookup->CreateTypeDescriptor(StoredClass, BuildOptions))
						{
							FUSION_LOG("Create new class descriptor: %s", *NewDescriptor->Type->GetName());
						}
					}
				}
			}
			else
			{
				// Make it set to none
				Buffer.Int(0);
			}
		}
	}
	else
	{
		WriteWords(Buffer, Container, WordCount);
	}
}

void Property::Deserialize(UFusionClient* Client, SharedMode::ReadBuffer& Buffer, void* Container, const FFusionFunctionProperty& FunctionProperty)
{
	void* ResolvedContainer = Container;
	FProperty* ResolvedProperty = EngineProperty;

	if (ResolvedProperty->IsA<FStrProperty>() || ResolvedProperty->IsA<FNameProperty>())
	{
		void* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<void>(ResolvedContainer);

		FString ResultString = ReadString(Buffer);

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

		return;
	}

	if (DataType == EFusionDataTypes::Vector)
	{
		TArray<int32> Words;
		Words.SetNum(WordCount);
		ReadWords(Buffer, Words.GetData(), WordCount);

		FVector Value;
		Value.X = DecompressFloat(Words[0]);
		Value.Y = DecompressFloat(Words[2]);
		Value.Z = DecompressFloat(Words[4]);

		FVector* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<FVector>(ResolvedContainer);
		*DestAddr = Value;
		return;
	}
	if (DataType == EFusionDataTypes::Quat)
	{
		TArray<int32> Words;
		Words.SetNum(WordCount);
		ReadWords(Buffer, Words.GetData(), WordCount);

		const float* FloatWords = reinterpret_cast<const float*>(Words.GetData());
		FQuat Value;
		Value.X = FloatWords[0];
		Value.Y = FloatWords[2];
		Value.Z = FloatWords[4];
		Value.W = FloatWords[6];

		FQuat* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<FQuat>(ResolvedContainer);
		*DestAddr = Value;
		return;
	}
	if (DataType == EFusionDataTypes::Rotator)
	{
		TArray<int32> Words;
		Words.SetNum(WordCount);
		ReadWords(Buffer, Words.GetData(), WordCount);

		FRotator Value;
		Value.Pitch = DecompressFloat(Words[0]);
		Value.Yaw = DecompressFloat(Words[2]);
		Value.Roll = DecompressFloat(Words[4]);

		FRotator* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<FRotator>(ResolvedContainer);
		*DestAddr = Value;
		return;
	}
	if (DataType == EFusionDataTypes::Int)
	{
		void* DestAddr = ResolvedProperty->ContainerPtrToValuePtr<void>(Container);
		const int32 Data = Buffer.Int();
		// Skip remaining padding words
		for (int32 i = 1; i < WordCount; i++) Buffer.Int();

		FIntProperty* IntProp = CastField<FIntProperty>(ResolvedProperty);

		IntProp->InitializeValue(DestAddr);
		IntProp->SetPropertyValue(DestAddr, Data);
		return;
	}
	if (DataType == EFusionDataTypes::ClassId)
	{
		FString ClassPath = ReadString(Buffer);

		UClass* Class = nullptr;

		if (!ClassPath.IsEmpty())
		{
			Class = LoadObject<UClass>(nullptr, *ClassPath);

			if (Class != nullptr)
			{
				checkf(ResolvedProperty != nullptr, TEXT("Unable to resolve Class Property"));

				if (!Client->Lookup->FindClassDescriptor(Class))
				{
					FPropertyBuildOptions BuildOptions = UFusionTypeLookup::GetDefaultBuildOptions();
					if ([[maybe_unused]] UFusionTypeDescriptor* NewDescriptor = Client->Lookup->CreateTypeDescriptor(Class, BuildOptions))
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

	// Read WordCount int32s into a temporary buffer for generic property handling
	TArray<uint8> TempBuf;
	TempBuf.SetNumUninitialized(WordCount * sizeof(int32));
	ReadWords(Buffer, TempBuf.GetData(), WordCount);

#define HANDLE_PROP(PropClass, CppType) \
if (PropClass* Prop = CastField<PropClass>(ResolvedProperty)) \
{ \
SetPropertyValue<CppType>(Prop, ResolvedContainer, TempBuf.GetData(), 0); \
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
#undef HANDLE_PROP

	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(ResolvedProperty))
	{
		void* DestAddr = EnumProp->ContainerPtrToValuePtr<void>(ResolvedContainer);

		FProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
		UnderlyingProp->InitializeValue(DestAddr);

		if (FByteProperty* LocalByteProp = CastField<FByteProperty>(UnderlyingProp))
		{
			const uint8 Value = *reinterpret_cast<const uint8*>(TempBuf.GetData());
			LocalByteProp->SetPropertyValue(DestAddr, Value);
		}
		else if (FInt16Property* LocalInt16Prop = CastField<FInt16Property>(UnderlyingProp))
		{
			const int16 Value = *reinterpret_cast<const int16*>(TempBuf.GetData());
			LocalInt16Prop->SetPropertyValue(DestAddr, Value);
		}
		else if (FUInt16Property* LocalUInt16Prop = CastField<FUInt16Property>(UnderlyingProp))
		{
			const uint16 Value = *reinterpret_cast<const uint16*>(TempBuf.GetData());
			LocalUInt16Prop->SetPropertyValue(DestAddr, Value);
		}
		else if (FIntProperty* LocalIntProp = CastField<FIntProperty>(UnderlyingProp))
		{
			const int32 Value = *reinterpret_cast<const int32*>(TempBuf.GetData());
			LocalIntProp->SetPropertyValue(DestAddr, Value);
		}
		else if (FUInt32Property* LocalUIntProp = CastField<FUInt32Property>(UnderlyingProp))
		{
			const uint32 Value = *reinterpret_cast<const uint32*>(TempBuf.GetData());
			LocalUIntProp->SetPropertyValue(DestAddr, Value);
		}
		else
		{
			FUSION_LOG("Enum has unknown underlying type: %s", *UnderlyingProp->GetClass()->GetName());
		}
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

void Property::BuildState(TArray<FPropertyWordState>& PropertyStates)
{
	FPropertyWordState NewState;
	NewState.EngineProperty = EngineProperty;
	NewState.WordOffset = WordOffset;
	NewState.WordCount = WordCount;
	NewState.PreviousReceivedWords.SetNumZeroed(WordCount);

	PropertyStates.Add(MoveTemp(NewState));
}

void StructProperty::BuildState(TArray<FPropertyWordState>& PropertyStates)
{
	FPropertyWordState NewState;
	NewState.EngineProperty = EngineProperty;
	NewState.WordOffset = WordOffset;
	NewState.WordCount = WordCount;

	for (Property* SubProp : SubProperties)
	{
		SubProp->BuildState(NewState.SubPropertyStates);
	}

	PropertyStates.Add(MoveTemp(NewState));
}

void ObjectProperty::BuildState(TArray<FPropertyWordState>& PropertyStates)
{
	FPropertyWordState NewState;
	NewState.EngineProperty = EngineProperty;
	NewState.WordOffset = WordOffset;
	NewState.WordCount = WordCount;
	NewState.PreviousReceivedWords.SetNumZeroed(WordCount);

	PropertyStates.Add(MoveTemp(NewState));
}

void ArrayProperty::BuildState(TArray<FPropertyWordState>& PropertyStates)
{
	FPropertyWordState NewState;
	NewState.EngineProperty = EngineProperty;
	NewState.WordOffset = WordOffset;
	NewState.WordCount = WordCount;

	// Compute stride: distance in words between consecutive array elements.
	int32 StrideOffset = 0;
	if (SubProperties.Num() > 0)
	{
		const Property* LastProp = SubProperties[SubProperties.Num() - 1];
		StrideOffset = LastProp->WordOffset + LastProp->WordCount;
	}

	// Word layout: [size_word][elem0 sub-props...][elem1 sub-props...]...
	int32 ElementOffset = 1; // Skip the size word
	for (int i = 0; i < MaxItems; i++)
	{
		for (Property* SubProp : SubProperties)
		{
			const int32 BaseIndex = NewState.SubPropertyStates.Num();
			SubProp->BuildState(NewState.SubPropertyStates);
			// Adjust the top-level offset to account for element position within the array.
			// BuildState set it to SubProp->WordOffset (relative to element start), but we
			// need ElementOffset + SubProp->WordOffset (relative to array start).
			NewState.SubPropertyStates[BaseIndex].WordOffset += ElementOffset;
		}
		ElementOffset += StrideOffset;
	}

	PropertyStates.Add(MoveTemp(NewState));
}
