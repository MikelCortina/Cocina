// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Fusion/Aliases.h"
#include "UObject/StrongObjectPtr.h"

#include "TypeLookup.generated.h"

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EBuildStructOptions : uint8
{
	None                = 0 UMETA(Hidden), // explicit "no flags"
	SkipNotReplicated   = 1 << 0 UMETA(DisplayName = "Skip Not Replicated"),
	AddDefaultProperties= 1 << 1 UMETA(DisplayName = "Add Default Properties"),
	AddStringProperties = 1 << 2 UMETA(DisplayName = "Add String Properties"),
	AddNameProperties = 1 << 3 UMETA(DisplayName = "Add Name Properties"),
};
ENUM_CLASS_FLAGS(EBuildStructOptions);

struct FPropertyBuildOptions
{
	EBuildStructOptions OptionsFlags;
	int32 ArrayPreAllocSize{8};
	bool IsRootTransform = false;
};

class UTypeBase;
class UTypeDescriptor;
class UCustomTypeDescriptorBuilder;
/**
 * 
 */
UCLASS()
class PHOTONFUSION_API UTypeLookup : public UObject
{
	GENERATED_BODY()

public:
	TMap<const UStruct*, TStrongObjectPtr<UTypeDescriptor>> ClassDescriptors;
	TMap<const UStruct*, TStrongObjectPtr<UTypeDescriptor>> StructDescriptors;
	
	TMap<uint64, TStrongObjectPtr<UTypeDescriptor>> HashToDescriptor;
	
	TMap<TWeakObjectPtr<UStruct>, TObjectPtr<UClass>> CustomTypeBuilders;
	TMap<TWeakObjectPtr<UStruct>, TStrongObjectPtr<UCustomTypeDescriptorBuilder>> TypeBuilderInstances;

	
	UTypeDescriptor* CreateTypeDescriptor(UClass* Type, FPropertyBuildOptions& BuildOptions, bool CreateForComponents = false);
	UTypeDescriptor* CreateTypeStructDescriptor(UStruct* Type, FProperty* ParentProperty, FPropertyBuildOptions& BuildOptions);
	bool ShouldAddProperty(const UTypeBase* Descriptor, const FProperty* Prop, const FPropertyBuildOptions& BuildOptions);
	void AddPropertyIfExists(const TStrongObjectPtr<UTypeDescriptor>& Descriptor, const UStruct* Type, const UStruct* PropType, FName PropName);
	void AddPropertyToTypeDescriptor(UTypeBase* Descriptor, FProperty* Property, FPropertyBuildOptions& BuildOptions);
	void AddFunctionLookup(const TStrongObjectPtr<UTypeDescriptor>& Descriptor, const UClass* Class, const FString& EventName);
	int32 GetReplicatedActorTypeLayout(UClass* Type, TArray<SharedMode::TypeRef>& Types);
	int32 EnsureDefaultActorTypeDescriptor(UClass* Type);
	UTypeDescriptor* FindClassDescriptor(const UStruct* Type);
	UTypeDescriptor* FindClassDescriptor(uint64 Type);
	UTypeDescriptor* FindStructDescriptor(const UStruct* Type);
	void RegisterTypeBuilder(UStruct* Target, UClass* Builder);
	void UnRegisterTypeBuilder(UStruct* Target);
	void Destroy();

	static FPropertyBuildOptions GetDefaultBuildOptions();
};
