// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FusionCustomTypeDescriptorBuilder.h"
#include "FusionNetworkedArrayBuilder.generated.h"


/**
 * 
 */
UCLASS()
class PHOTONFUSION_API UFusionNetworkedArrayBuilder : public UFusionCustomTypeDescriptorBuilder
{
	GENERATED_BODY()

public:
	virtual TStrongObjectPtr<UFusionTypeDescriptor> CreateDescriptor(UFusionTypeLookup* Lookup, UStruct* Type, FProperty* ParentProperty, FPropertyBuildOptions BuildOptions) override;
};
