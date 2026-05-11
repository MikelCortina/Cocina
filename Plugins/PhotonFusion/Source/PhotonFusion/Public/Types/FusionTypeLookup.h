// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fusion/Aliases.h"
#include "UObject/StrongObjectPtr.h"

#include "FusionTypeLookup.generated.h"

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EFusionBuildStructOptions : uint8
{
	None                = 0 UMETA(Hidden), // explicit "no flags"
	SkipNotReplicated   = 1 << 0 UMETA(DisplayName = "Skip Not Replicated"),
	AddDefaultProperties= 1 << 1 UMETA(DisplayName = "Add Default Properties"),
	AddStringProperties = 1 << 2 UMETA(DisplayName = "Add String Properties"),
	AddNameProperties = 1 << 3 UMETA(DisplayName = "Add Name Properties"),
};
ENUM_CLASS_FLAGS(EFusionBuildStructOptions);

struct FPropertyBuildOptions
{
	EFusionBuildStructOptions OptionsFlags;
	int32 ArrayPreAllocSize{8};
	bool IsRootTransform = false;
};

class UFusionTypeBase;
class UFusionTypeDescriptor;
class UFusionCustomTypeDescriptorBuilder;
/**
 * 
 */
UCLASS()
class PHOTONFUSION_API UFusionTypeLookup : public UObject
{
	GENERATED_BODY()

public:
	TMap<const UStruct*, TStrongObjectPtr<UFusionTypeDescriptor>> ClassDescriptors;
	TMap<const UStruct*, TStrongObjectPtr<UFusionTypeDescriptor>> StructDescriptors;
	
	TMap<uint64, TStrongObjectPtr<UFusionTypeDescriptor>> HashToDescriptor;
	
	TMap<TWeakObjectPtr<UStruct>, TObjectPtr<UClass>> CustomTypeBuilders;
	TMap<TWeakObjectPtr<UStruct>, TStrongObjectPtr<UFusionCustomTypeDescriptorBuilder>> TypeBuilderInstances;

	
	UFusionTypeDescriptor* CreateTypeDescriptor(UClass* Type, FPropertyBuildOptions& BuildOptions, bool CreateForComponents = false);
	UFusionTypeDescriptor* CreateTypeStructDescriptor(UStruct* Type, FProperty* ParentProperty, FPropertyBuildOptions& BuildOptions);
	bool ShouldAddProperty(const UFusionTypeBase* Descriptor, const FProperty* Prop, const FPropertyBuildOptions& BuildOptions);
	void AddPropertyIfExists(const TStrongObjectPtr<UFusionTypeDescriptor>& Descriptor, const UStruct* Type, const UStruct* PropType, FName PropName);
	void AddPropertyToTypeDescriptor(UFusionTypeBase* Descriptor, FProperty* Property, FPropertyBuildOptions& BuildOptions);
	void AddFunctionLookup(const TStrongObjectPtr<UFusionTypeDescriptor>& Descriptor, const UClass* Class, const FString& EventName);
	int32 GetReplicatedActorTypeLayout(UClass* Type, TArray<FusionCore::TypeRef>& Types);
	int32 EnsureDefaultActorTypeDescriptor(UClass* Type);
	UFusionTypeDescriptor* FindClassDescriptor(const UStruct* Type);
	UFusionTypeDescriptor* FindClassDescriptor(uint64 Type);
	UFusionTypeDescriptor* FindStructDescriptor(const UStruct* Type);
	void RegisterTypeBuilder(UStruct* Target, UClass* Builder);
	void UnRegisterTypeBuilder(UStruct* Target);
	void Destroy();

	static FPropertyBuildOptions GetDefaultBuildOptions();
};
