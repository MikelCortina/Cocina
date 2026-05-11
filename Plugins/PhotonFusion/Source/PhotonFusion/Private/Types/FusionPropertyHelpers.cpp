// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Types/FusionPropertyHelpers.h"
#include <Fusion/StringHeap.h>
#include <Fusion/Types.h>
#include "FusionShared.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

TMap<TPair<FName, FName>, int32>& FusionMeta::GetArraySizeRegistry()
{
	static TMap<TPair<FName, FName>, int32> Registry;
	return Registry;
}

void FusionMeta::RegisterArraySize(FName OwnerName, FName PropertyName, int32 Size)
{
	GetArraySizeRegistry().Add(TPair<FName, FName>(OwnerName, PropertyName), Size);
}

FusionCore::StringHandle UFusionPropertyHelpers::EncodeString(FusionCore::Object* Object, FPropertyWordState& PropertyState, const FString& String, const FusionCore::StringHandle& ExistingHandle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPropertyHelpers::EncodeString);

	if (String.IsEmpty())
	{
		PropertyState.PropertyStateHash = 0;
		PropertyState.PropertyReferenceLength = 0;
		
		Object->FreeString(ExistingHandle);

		return FusionCore::StringHandle{UINT32_MAX, 0}; //Return empty string handle.
	}

	FusionCore::StringMessage OutStringStatus{};
	const PhotonCommon::CharType* Ptr = Object->ResolveString(ExistingHandle, OutStringStatus);
	EFusionEncodedStringStatus Status = GetEncodedStringStatus(OutStringStatus);
	if (Status != EFusionEncodedStringStatus::Valid)
	{
		const FString StatusString = StaticEnum<EFusionEncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
		FUSION_LOG_ERROR("Encoded string has error: %s", *StatusString);
		return ExistingHandle;
	}
	
	const int32 InputHash = GetTypeHash(String);
	const int32 InputLength = String.Len();
	
	if (Ptr)
	{
		// Fast path: if input string hash and length match the stored state, the string
		// hasn't changed since the last encode. Skip the expensive StringCast.
		if (InputHash == PropertyState.PropertyStateHash && InputLength == PropertyState.PropertyReferenceLength)
		{
			return ExistingHandle;
		}
		
		if (const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> PtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Ptr)); FString(PtrAsTchar.Get()) != String)
		{
			Object->FreeString(ExistingHandle);
			const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> StringAsWchar = StringCast<UTF8CHAR>(*String);

			PropertyState.PropertyStateHash = InputHash;
			PropertyState.PropertyReferenceLength = InputLength;

			//Return updated/new handle for the allocated string.
			return Object->AddString(reinterpret_cast<const PhotonCommon::CharType*>(StringAsWchar.Get()));
		}
		
		PropertyState.PropertyStateHash = InputHash;
		PropertyState.PropertyReferenceLength = InputLength;
		
		return ExistingHandle;
	}

	const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> StringAsWchar = StringCast<UTF8CHAR>(*String);
	const FusionCore::StringHandle Handle = Object->AddString(reinterpret_cast<const PhotonCommon::CharType*>(StringAsWchar.Get()));

	PropertyState.PropertyStateHash = InputHash;
	PropertyState.PropertyReferenceLength = InputLength;

	return Handle;
}

EFusionEncodedStringStatus UFusionPropertyHelpers::GetEncodedStringStatus(FusionCore::StringMessage StringStatus)
{
	switch (StringStatus)
	{
		case FusionCore::StringMessage::OutOfRange:
		return EFusionEncodedStringStatus::OutOfRange;
	case FusionCore::StringMessage::WrongGeneration:
		return EFusionEncodedStringStatus::WrongGeneration;
	case FusionCore::StringMessage::WrongSize:
		return EFusionEncodedStringStatus::WrongSize;
	case FusionCore::StringMessage::NotALiveEntry:
		return EFusionEncodedStringStatus::NotAliveEntry;
	default:
		return EFusionEncodedStringStatus::Valid;
	}
}

void FCopyContext::AddRepFunction(UFunction* Rep, FProperty* Property, const void* PreviousValue)
{
	uint8* Data{nullptr};
	if (Rep->NumParms > 0 && PreviousValue)
	{
		const int32 Size = Property->GetSize();
		const int32 Alignment = Property->GetMinAlignment();

		Data = static_cast<uint8*>(FMemory::Malloc(Size, Alignment));
		
		Property->InitializeValue(Data);
		Property->CopyCompleteValue(Data, PreviousValue);
	}
	
	OnReps.Add({Rep, Property, Data, nullptr});
}

void FCopyContext::AddRepFunctionPointer(UFunction* Rep, FProperty* Property, UObject* PreviousPointer)
{
	OnReps.Add({Rep, Property, nullptr, PreviousPointer});
}
