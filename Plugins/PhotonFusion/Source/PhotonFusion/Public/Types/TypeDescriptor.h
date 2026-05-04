// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "FusionShared.h"
#include "FusionTypes.h"
#include "TypeLookup.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Fusion/Types.h"
#include "Types/TypeProperties.h"
#include "UObject/StrongObjectPtr.h"

#include "TypeDescriptor.generated.h"


int32 EFusionDataTypeByteSize(const EFusionDataTypes Type);
int32 EFusionDataTypeWordCount(const EFusionDataTypes Type);

EFusionDataTypes EFusionDataTypeParseCpp(const FString& Name);

class UFusionClient;
class UObject;
class FProperty;

USTRUCT(BlueprintType)
struct PHOTONFUSION_API FFunctionProperty
{
	GENERATED_BODY()

	FProperty* EngineProperty{nullptr};
	int32 WordOffset{0};
	int32 WordCount{0};
	int32 StartRange{0};
	int32 EndRange{0};
};


UCLASS(BlueprintType)
class PHOTONFUSION_API UTypeBase : public UObject
{
	GENERATED_BODY()

public:

	TStrongObjectPtr<UStruct> Type{};

	uint32 WordCount{0};
	uint64 TypeHash{0};
	
	TArray<Property*> Properties{};
	
	void AddProperty(Property* Item, FProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(FClassProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(FNameProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(FStrProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(FBoolProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(FStructProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(UTypeLookup* Lookup, FObjectProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(UTypeLookup* Lookup, FWeakObjectProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(UTypeLookup* Lookup, FSoftObjectProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void HandleObjectProperty(UTypeLookup* Lookup, FObjectPropertyBase* Prop, EObjectPointerType Type, FPropertyBuildOptions& BuildOptions);
	void AddFusionNetworkArrayProperty(UTypeLookup* Lookup, FArrayProperty* ArrayProp, FProperty* ParentContainerProperty, int MaxItems, FPropertyBuildOptions& BuildOptions);
	void AddProperty(UTypeLookup* Lookup, FArrayProperty* ArrayProp, FPropertyBuildOptions& BuildOptions);
	void AddProperty(FNumericProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(FFloatProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(FDoubleProperty* Prop, FPropertyBuildOptions& BuildOptions);
	void AddProperty(FEnumProperty* Prop, EFusionDataTypes DataType, FPropertyBuildOptions& BuildOptions);
	

	bool BuildArrayItem(UTypeLookup* Lookup, const FArrayProperty* ArrayProp, ArrayProperty* ArrayItem, int MaxItems);
};

UCLASS(BlueprintType)
class PHOTONFUSION_API UFunctionDescriptor : public UTypeBase
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UFunction> Function {nullptr};

	int32 ParametersSize{0};

	TArray<FFunctionProperty> FunctionProperties;
	
	void SerializeParams(void* Container, int PropertyIndex, TArray<uint8>& Buffer, const UObject* Source);
	void DeserializeParams(UFusionClient* Client, const SharedMode::Rpc& Rpc, void* Params);
};

UCLASS(BlueprintType)
class PHOTONFUSION_API UTypeDescriptor : public UTypeBase
{
	GENERATED_BODY()
	
public:
	virtual ~UTypeDescriptor() override;

	UPROPERTY()
	TMap<FString, TObjectPtr<UFunctionDescriptor>> EventFunctions{};
	
	TMap<uint64, FString> EventHashToName{};
	TMap<FString, uint64> EventNameToHash{};
};

UCLASS()
class PHOTONFUSION_API UTypeDescriptorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	
	
	static void RegisterArrayHooks(const UScriptStruct* SourceStruct, const FFusionArrayHooks& Hooks)
	{
		if (HooksMap.Contains(SourceStruct->GetName()))
		{
			FUSION_LOG_ERROR("HooksMap already contains mapping for : %s", *SourceStruct->GetName());
			return;
		}
			
		HooksMap.Add(SourceStruct->GetName(), Hooks);
	}

	static TMap<FString, FFusionArrayHooks> HooksMap;
};

template<
	typename DerivedType,
	typename PreRemoveFn,
	typename PostAddFn,
	typename PostChangeFn
>
void RegisterFusionArrayHooks(
	UScriptStruct* SourceStruct,
	DerivedType*,
	PreRemoveFn PreRemove,
	PostAddFn PostAdd,
	PostChangeFn PostChange
)
{
	FFusionArrayHooks Hooks;

	Hooks.PreRemove = [PreRemove](void* Container, const TArrayView<int32>& RemovedIndices, int32 FinalSize)
	{
		DerivedType* Obj = static_cast<DerivedType*>(Container);
		(Obj->*PreRemove)(RemovedIndices, FinalSize);
	};

	Hooks.PostAdd = [PostAdd](void* Container, const TArrayView<int32>& AddedIndices, int32 FinalSize)
	{
		DerivedType* Obj = static_cast<DerivedType*>(Container);
		(Obj->*PostAdd)(AddedIndices, FinalSize);
	};

	Hooks.PostChange = [PostChange](void* Container, const TArrayView<int32>& ChangedIndices, int32 FinalSize)
	{
		DerivedType* Obj = static_cast<DerivedType*>(Container);
		(Obj->*PostChange)(ChangedIndices, FinalSize);
	};

	UTypeDescriptorLibrary::RegisterArrayHooks(SourceStruct, Hooks);
}