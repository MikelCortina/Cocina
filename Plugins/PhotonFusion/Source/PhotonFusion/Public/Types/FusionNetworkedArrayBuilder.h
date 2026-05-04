// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CustomTypeDescriptorBuilder.h"
#include "FusionNetworkedArrayBuilder.generated.h"


/**
 * 
 */
UCLASS()
class PHOTONFUSION_API UFusionNetworkedArrayBuilder : public UCustomTypeDescriptorBuilder
{
	GENERATED_BODY()

public:
	virtual TStrongObjectPtr<UTypeDescriptor> CreateDescriptor(UTypeLookup* Lookup, UStruct* Type, FProperty* ParentProperty, FPropertyBuildOptions BuildOptions) override;
};
