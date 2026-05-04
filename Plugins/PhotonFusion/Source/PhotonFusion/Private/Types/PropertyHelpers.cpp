
#include "Types/PropertyHelpers.h"
#include <Fusion/StringHeap.h>
#include <Fusion/Types.h>
#include "FusionShared.h"

TMap<TPair<FName, FName>, int32>& FusionMeta::GetArraySizeRegistry()
{
	static TMap<TPair<FName, FName>, int32> Registry;
	return Registry;
}

void FusionMeta::RegisterArraySize(FName OwnerName, FName PropertyName, int32 Size)
{
	GetArraySizeRegistry().Add(TPair<FName, FName>(OwnerName, PropertyName), Size);
}

SharedMode::StringHandle UPropertyHelpers::EncodeString(SharedMode::Object* Object, const FString& String, const SharedMode::StringHandle& ExistingHandle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPropertyHelpers::EncodeString);
	
	if (String.IsEmpty())
	{
		Object->FreeString(ExistingHandle);
		return SharedMode::StringHandle{UINT32_MAX, 0}; //Return empty string handle.
	}
	
	SharedMode::StringMessage OutStringStatus{};
	const PhotonCommon::CharType* Ptr = Object->ResolveString(ExistingHandle, OutStringStatus);
	EncodedStringStatus Status = GetEncodedStringStatus(OutStringStatus);
	if (Status != EncodedStringStatus::Valid)
	{
		const FString StatusString = StaticEnum<EncodedStringStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status)).ToString();
		FUSION_LOG_ERROR("Encoded string has error: %s", *StatusString);
		return ExistingHandle;
	}
	
	if (Ptr)
	{
		if (const TStringConversion<TStringConvert<UTF8CHAR, TCHAR>> PtrAsTchar = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Ptr)); FString(PtrAsTchar.Get()) != String)
		{
			Object->FreeString(ExistingHandle);
			const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> StringAsWchar = StringCast<UTF8CHAR>(*String);
	
			//Return updated/new handle for the allocated string.
			return Object->AddString(reinterpret_cast<const PhotonCommon::CharType*>(StringAsWchar.Get()));
		}
	
		//String didnt change, return same handle.
		return ExistingHandle;
	}
	
	const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> StringAsWchar = StringCast<UTF8CHAR>(*String);
	const SharedMode::StringHandle Handle = Object->AddString(reinterpret_cast<const PhotonCommon::CharType*>(StringAsWchar.Get()));
	return Handle;
}

EncodedStringStatus UPropertyHelpers::GetEncodedStringStatus(SharedMode::StringMessage StringStatus)
{
	switch (StringStatus)
	{
		case SharedMode::StringMessage::OutOfRange:
		return EncodedStringStatus::OutOfRange;
	case SharedMode::StringMessage::WrongGeneration:
		return EncodedStringStatus::WrongGeneration;
	case SharedMode::StringMessage::WrongSize:
		return EncodedStringStatus::WrongSize;
	case SharedMode::StringMessage::NotALiveEntry:
		return EncodedStringStatus::NotAliveEntry;
	default:
		return EncodedStringStatus::Valid;
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
