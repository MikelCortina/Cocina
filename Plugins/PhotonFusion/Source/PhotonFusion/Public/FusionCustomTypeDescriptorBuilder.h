// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/FusionTypeDescriptor.h"
#include "UObject/Object.h"
#include "FusionCustomTypeDescriptorBuilder.generated.h"

/**
 * 
 */
UCLASS()
class PHOTONFUSION_API UFusionCustomTypeDescriptorBuilder : public UObject
{
	GENERATED_BODY()

public:
	virtual TStrongObjectPtr<UFusionTypeDescriptor> CreateDescriptor(UFusionTypeLookup* Lookup, UStruct* Type, FProperty* ParentProperty, FPropertyBuildOptions BuildOptions);
};
