// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "FusionGlobals.h"
#include "Fusion/Client.h"
#include "CoreMinimal.h"
#include "FusionHelpers.h"
#include "FusionObjectActorPair.generated.h"

UENUM()
enum class EFusionObjectPairType : uint8
{
	Actor,
	Component,
	CustomObject,
	GlobalInstance
};

struct FPropertyWordState
{
	FProperty* EngineProperty{nullptr};
	int32 WordOffset{0};
	int32 WordCount{0};
	int32 ChangedWordCount{0};

	int32 PropertyStateHash;
	int32 PropertyReferenceLength;

	// Per-property snapshot of the most recently received word values. Used by the receive
	// path (CopyRemoteStateToObject) to compute ChangedWordCount, since the shadow buffer
	// cannot be relied upon as a previous-state reference when receiving.
	TArray<FusionCore::Word> PreviousReceivedWords;

	// Nested sub-property states for struct properties. Mirrors the Property::SubProperties
	// tree so that change tracking works down to each leaf property.
	TArray<FPropertyWordState> SubPropertyStates{};
};


USTRUCT()
struct PHOTONFUSION_API FFusionObjectActorPair
{
	GENERATED_BODY()

	UPROPERTY()
	EFusionObjectPairType ObjectType{EFusionObjectPairType::Actor};

	UPROPERTY()
	TObjectPtr<AActor> Actor{nullptr};

	UPROPERTY()
	TObjectPtr<UObject> EngineObject{nullptr};

	UPROPERTY()
	TObjectPtr<class UFusionActorComponent> Settings{nullptr};

	FusionCore::Object* Object{nullptr};
	FusionCore::ObjectId ObjectId;

	TArray<FPropertyWordState> PropertyStates{};

	FORCEINLINE bool IsValid() const
	{
		return EngineObject != nullptr && Object->GetHasValidData();
	}
};

