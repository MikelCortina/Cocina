// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once
#include <Fusion/Types.h>
#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "FusionActorComponent.h"
#include "FusionShared.h"
#include "FusionObjectActorPair.h"
#include "FusionPropertyHelpers.generated.h"

namespace FusionMeta
{
	static const TCHAR* FusionArraySize = TEXT("FusionArraySize");
	static constexpr int32 FusionArrayMaxSize = 64;

	// Runtime registry for FusionArraySize values, keyed by (OwnerStructName, PropertyName).
	// FProperty::FindMetaData is only available in editor builds (WITH_EDITORONLY_DATA).
	// This map is populated from UBT-generated code (all builds) or FindMetaData (editor fallback).
	PHOTONFUSION_API TMap<TPair<FName, FName>, int32>& GetArraySizeRegistry();
	PHOTONFUSION_API void RegisterArraySize(FName OwnerName, FName PropertyName, int32 Size);
}

UENUM(BlueprintType)
enum class EFusionEncodedStringStatus : uint8
{
	NullPointer = 0 UMETA(DisplayName = "Null Pointer"),
	OutOfRange = 1 UMETA(DisplayName = "Out of range"),
	NotAliveEntry = 2 UMETA(DisplayName = "Entry not alive"),
	WrongGeneration = 3 UMETA(DisplayName = "Wrong string generation"),
	WrongSize = 4 UMETA(DisplayName = "Wrong string size"),
	EmptyHeap = 5 UMETA(DisplayName = "Empty Heap"),
	Valid = 6 UMETA(DisplayName = "Valid"),
};



UCLASS()
class UFusionPropertyHelpers : public UObject
{
	GENERATED_BODY()

public:
	static FusionCore::StringHandle EncodeString(FusionCore::Object* Object, FPropertyWordState& PropertyState, const FString& String, const FusionCore::StringHandle& ExistingHandle);
	static EFusionEncodedStringStatus GetEncodedStringStatus(FusionCore::StringMessage StringStatus);
};

struct FRepValue
{
	UFunction* RepFunction{nullptr};
	FProperty* Property{nullptr};
	uint8* PreviousValueData{nullptr};
	UObject* PreviousPointer{nullptr};

	bool operator==(const FRepValue& Other) const
	{
		return RepFunction == Other.RepFunction;
	}
};

FORCEINLINE uint32 GetTypeHash(const FRepValue& InId)
{
	return GetTypeHash(InId.RepFunction);
}

FORCEINLINE int32 GetPreAllocationSize(const FProperty* Property, int32 CurrentSize)
{
	if (Property)
	{
		const FName OwnerName = Property->GetOwnerStruct() ? Property->GetOwnerStruct()->GetFName() : NAME_None;
		const TPair<FName, FName> Key(OwnerName, Property->GetFName());

		// Check the runtime registry (populated from UBT-generated code in all builds)
		if (const int32* RegisteredSize = FusionMeta::GetArraySizeRegistry().Find(Key))
		{
			return FMath::Min(*RegisteredSize, FusionMeta::FusionArrayMaxSize);
		}
	}

	return CurrentSize;
}

class UFusionClient;

struct FCopyContext
{
	FFusionObjectActorPair Pair;
	UFusionClient* FusionClient{nullptr};
	FPackagedSettings Settings;
	bool bDoDependencyChecks{true};
	uint32 MaxWordCount{0};
	uint32 CurrentDepth{0};
	TSet<FRepValue> OnReps{};

public:
	void AddRepFunction(UFunction* Rep, FProperty* Property, const void* PreviousValue);
	void AddRepFunctionPointer(UFunction* Rep, FProperty* Property, UObject* PreviousPointer);
};
