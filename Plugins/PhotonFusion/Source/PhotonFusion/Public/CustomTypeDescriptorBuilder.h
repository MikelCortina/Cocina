// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/TypeDescriptor.h"
#include "UObject/Object.h"
#include "CustomTypeDescriptorBuilder.generated.h"

/**
 * 
 */
UCLASS()
class PHOTONFUSION_API UCustomTypeDescriptorBuilder : public UObject
{
	GENERATED_BODY()

public:
	virtual TStrongObjectPtr<UTypeDescriptor> CreateDescriptor(UTypeLookup* Lookup, UStruct* Type, FProperty* ParentProperty, FPropertyBuildOptions BuildOptions);
};
