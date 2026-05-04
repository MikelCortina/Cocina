// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "FusionGlobals.h"
#include "Fusion/Client.h"
#include "CoreMinimal.h"
#include "FusionHelpers.h"
#include "ObjectActorPair.generated.h"

UENUM()
enum class EObjectPairType : uint8
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
};

USTRUCT()
struct PHOTONFUSION_API FObjectActorPair
{
	GENERATED_BODY()

	UPROPERTY()
	EObjectPairType ObjectType{EObjectPairType::Actor};

	UPROPERTY()
	TObjectPtr<AActor> Actor{nullptr};
	
	UPROPERTY()
	TObjectPtr<UObject> EngineObject{nullptr};
	
	UPROPERTY()
	TObjectPtr<class UFusionActorComponent> Settings{nullptr};
	
	SharedMode::Object* Object{nullptr};
	SharedMode::ObjectId ObjectId;

	TArray<FPropertyWordState> PropertyStates{};

	FORCEINLINE bool IsValid() const
	{
		return EngineObject != nullptr && Object->HasValidData;
	}
};

