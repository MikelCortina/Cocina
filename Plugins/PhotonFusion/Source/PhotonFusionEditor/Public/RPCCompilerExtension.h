// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintCompilerExtension.h"
#include "RPCCompilerExtension.generated.h"

UCLASS()
class URPCCompilerExtension : public UBlueprintCompilerExtension
{
	GENERATED_BODY()

public:
	virtual void ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data) override;
};