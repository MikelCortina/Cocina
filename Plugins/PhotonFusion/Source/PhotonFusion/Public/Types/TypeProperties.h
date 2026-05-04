// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertyHelpers.h"
#include "Fusion/Aliases.h"
#include "Fusion/Types.h"
#include "UObject/Object.h"

#include "TypeProperties.generated.h"

UENUM(BlueprintType)
enum class EFusionDataTypes : uint8
{
	Unknown = 0 UMETA(DisplayName = "Unknown"),
	Struct = 1 UMETA(DisplayName = "Struct"),
	Byte = 2 UMETA(DisplayName = "Byte"),
	Int = 3 UMETA(DisplayName = "Int"),
	UInt = 4 UMETA(DisplayName = "UInt"),
	Int16 = 5 UMETA(DisplayName = "Int16"),
	UInt16 = 6 UMETA(DisplayName = "UInt16"),
	UInt64 = 7 UMETA(DisplayName = "UInt64"),
	Bool = 8 UMETA(DisplayName = "Bool"),
	Float = 9 UMETA(DisplayName = "Float"),
	Vector = 10 UMETA(DisplayName = "Vector"),
	Rotator = 11 UMETA(DisplayName = "Rotator"),
	Quat = 12 UMETA(DisplayName = "Quat"),
	Int64  = 13 UMETA(DisplayName = "Int64"),
	Double = 14 UMETA(DisplayName = "Double"),
	ActorId = 15 UMETA(DisplayName = "ActorId"),
	ObjectId = 16 UMETA(DisplayName = "ObjectId"),
	FusionArray = 17 UMETA(DisplayName = "Fusion Array"),
	Array = 18 UMETA(DisplayName = "Array"),
	Name = 19 UMETA(DisplayName = "Name"),
	ClassId = 20 UMETA(DisplayName = "ClassId"),
	String = 21 UMETA(DisplayName = "String"),
	Custom = 22 UMETA(DisplayName = "Custom"),
};

UENUM(BlueprintType)
enum class EObjectPointerType : uint8
{
	ObjectPointer       = 0 UMETA(DisplayName = "Object Pointer"),
	WeakObjectPointer   = 1	UMETA(DisplayName = "Weak Object Pointer"),
	SoftObjectPointer   = 2	UMETA(DisplayName = "Soft Object Pointer"),
};

UENUM(BlueprintType)
enum class EEncodedObjectType : uint8
{
	None								= 0 UMETA(DisplayName = "None"),
	NetworkedObject						= 1 UMETA(DisplayName = "Networked Object"),
	MapObjectHash						= 2	UMETA(DisplayName = "Map Object Hash"),
	NetworkedObjectComponentStringPath	= 3 UMETA(DisplayName = "Networked Object Component String Path"),
	MapObjectComponentStringPath		= 4	UMETA(DisplayName = "Map Object Component String Path"),
	ClassPath							= 5	UMETA(DisplayName = "Class Path"),
	ObjectPath							= 6	UMETA(DisplayName = "Object Path"),
	BlueprintCDOClass					= 7 UMETA(DisplayName = "Blueprint CDO Class"),
	SoftObjectPath						= 8 UMETA(DisplayName = "Soft Object Path"),
};

class UFusionClient;
struct FCopyContext;

class PHOTONFUSION_API Property
{
public:
	virtual ~Property();
	
	virtual Property* Clone() const { return new Property(*this); }
	
	int32 Alignment{0};
	int32 WordOffset{0};
	int32 WordCount{0};
	bool Interpolated{false};
	bool IsTransformProperty{false};

	FGuid ItemId{};
	
	bool SkipResolve{false};
	
	UFunction* RepNotify {nullptr};

	EFusionDataTypes DataType{EFusionDataTypes::Unknown};

	FProperty* EngineProperty {nullptr};

	virtual void CopyTo(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words) const;
	virtual bool CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow, bool Interpolating) const;
	
	void  ResolveContainerAndProperty(void* Container, void*& ResolvedContainer, FProperty*& ResolvedProperty) const;

	void AddSubProperty(Property* Property);

	virtual void CleanupPreviousState(UFusionClient* Client, const FCopyContext& Context, void* Container, int* Words);

	virtual void Serialize(UFusionClient* Client, void* Container, const struct FFunctionProperty& FunctionProperty, TArray<uint8>& Buffer, bool RootArgument);
	virtual void Deserialize(UFusionClient* Client, uint8_t* Buffer, void* Container, const FFunctionProperty& FunctionProperty, int& CurrentOffset);
	
	TArray<Property*> SubProperties{};

	template<typename T, typename PropType>
	void SetPropertyValue(PropType* Prop, void* Container, const uint8* Buffer, size_t Offset)
	{
		void* DestAddr = Prop->template ContainerPtrToValuePtr<void>(Container);
		const T* Data = reinterpret_cast<const T*>(Buffer + Offset);
		Prop->InitializeValue(DestAddr);
		Prop->SetPropertyValue(DestAddr, *Data);
	}

protected:
	bool ResolveObjectProperty(void* Container, void*& ResolvedContainer, FProperty*& ResolvedProperty) const;
};

class PHOTONFUSION_API StructProperty : public Property
{
	virtual ~StructProperty() override;
	
public:

	StructProperty() = default;
	StructProperty(const FStructProperty* Prop);
	
	StructProperty(const StructProperty& Other) : Property(Other)
	{
		SubProperties.Empty();
		for (const Property* Item : Other.SubProperties)
		{
			SubProperties.Add(Item->Clone());
		}
		if (!ValueBuffer)
		{
			const int32 PropSize = Other.EngineProperty->GetSize();
			const int32 PropAlignment =  Other.EngineProperty->GetMinAlignment();

			ValueBuffer = static_cast<uint8*>(FMemory::Malloc(PropSize, PropAlignment));
		}
	}

	virtual Property* Clone() const override
	{
		return new StructProperty(*this);
	}
	
	virtual void CopyTo(UFusionClient* Client, FCopyContext& CopyRoot, void* Container, int* Words) const override;
	virtual bool CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow, bool Interpolate) const override;
	virtual void CleanupPreviousState(UFusionClient* Client, const FCopyContext& Context, void* Container, int* Words) override;

	virtual void Serialize(UFusionClient* Client, void* Container, const FFunctionProperty& FunctionProperty, TArray<uint8>& Buffer, bool RootArgument) override;
	virtual void Deserialize(UFusionClient* Client, uint8_t* Buffer, void* Container, const FFunctionProperty& FunctionProperty, int& CurrentOffset) override;

private:
	uint8* ValueBuffer {nullptr};
};

class ObjectProperty : public Property
{
public:

	virtual ~ObjectProperty() override;

	ObjectProperty() = default;
	ObjectProperty(const ObjectProperty& Other) : Property(Other)
	{
		PointerType = Other.PointerType;
	}
	
	virtual Property* Clone() const override;

	void CheckReleaseString(FCopyContext& Context, int* Words) const;
	
	virtual void CopyTo(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words) const override;
	virtual bool CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow, bool Interpolate) const override;
	
	virtual void Serialize(UFusionClient* Client, void* Container, const FFunctionProperty& FunctionProperty, TArray<uint8>& Buffer, bool RootArgument) override;
	virtual void Deserialize(UFusionClient* Client, uint8_t* Buffer, void* Container, const FFunctionProperty& FunctionProperty, int& CurrentOffset) override;

	EObjectPointerType PointerType;

	template<typename TObjectPropertyType>
	bool SetObjectPropertyValueIfDifferent(FCopyContext& Context, TObjectPropertyType* ObjProp, void* Container, UObject* NewValue) const
	{
		if (!ObjProp) return false;

		auto OldValue = ObjProp->GetPropertyValue(Container);
		if (OldValue != NewValue)
		{
			if (RepNotify)
			{
				FProperty* Property = const_cast<FProperty*>(static_cast<const FProperty*>(ObjProp));
				Context.AddRepFunctionPointer(RepNotify, Property, OldValue.Get());
			}
			ObjProp->SetObjectPropertyValue(Container, NewValue);

			return true;
		}

		return false;
	}

	template<typename TObjectPropertyType>
	bool SetObjectPropertyValueIfDifferent(TObjectPropertyType* ObjProp, void* Container, UObject* NewValue) const
	{
		if (!ObjProp) return false;

		auto OldValue = ObjProp->GetPropertyValue(Container);
		if (OldValue != NewValue)
		{
			ObjProp->SetObjectPropertyValue(Container, NewValue);

			return true;
		}

		return false;
	}
};


class ArrayProperty : public Property
{
public:

	virtual ~ArrayProperty() override;

	ArrayProperty() = default;

	explicit ArrayProperty(const int MaxItemsValue) : Property()
	{
		MaxItems = MaxItemsValue;
	}
	
	ArrayProperty(const ArrayProperty& Other) : Property(Other)
	{
		MaxItems = Other.MaxItems;
		
		SubProperties.Empty();
		for (const Property* Item : Other.SubProperties)
		{
			SubProperties.Add(Item->Clone());
		}
	}
	
	virtual Property* Clone() const override { return new ArrayProperty(*this); }
	
	virtual void CopyTo(UFusionClient* Client, FCopyContext& CopyRoot, void* Container, int* Words) const override;
	virtual bool CopyFrom(UFusionClient* Client, FCopyContext& Context, void* Container, int* Words, int* Shadow, bool Interpolate) const override;
	
	virtual void CleanupPreviousState(UFusionClient* Client, const FCopyContext& Context, void* Container, int* Words) override;

	virtual void Serialize(UFusionClient* Client, void* Container, const FFunctionProperty& FunctionProperty, TArray<uint8>& Buffer, bool RootArgument) override;
	virtual void Deserialize(UFusionClient* Client, uint8_t* Buffer, void* Container, const FFunctionProperty& FunctionProperty, int& CurrentOffset) override;
	
	void ResolveItemProperty(const Property* SourceProperty, void* ItemAddress, void*& ResolvedContainer, FProperty*& ResolvedProperty) const;

protected:
	int MaxItems{-1};
};

class FusionArrayProperty : public ArrayProperty
{

public:
	FProperty* ParentContainerProperty{nullptr};
	
	virtual ~FusionArrayProperty() override;

	FusionArrayProperty() = default;
	FusionArrayProperty(const int MaxItems, FProperty* ParentProperty) : ArrayProperty(MaxItems)
	{
		ParentContainerProperty = ParentProperty;
	}
	FusionArrayProperty(const FusionArrayProperty& Other) : ArrayProperty(Other)
	{
		ParentContainerProperty = Other.ParentContainerProperty;
	}
	
	virtual Property* Clone() const override { return new FusionArrayProperty(*this); };

	//Custom copy from, since this array type supports more complex callbacks when indices change.
	virtual bool CopyFrom(UFusionClient* Client, FCopyContext& CopyRoot, void* Container, int* Words, int* Shadow, bool Interpolate) const override;
};