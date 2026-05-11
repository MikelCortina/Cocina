// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Types/FusionNetworkedArrayBuilder.h"

TStrongObjectPtr<UFusionTypeDescriptor> UFusionNetworkedArrayBuilder::CreateDescriptor(UFusionTypeLookup* Lookup, UStruct* Type, FProperty* ParentProperty, FPropertyBuildOptions BuildOptions)
{
	const FString ClassId = Type->GetName();
	const uint64 TypeHash = CityHash64(TCHAR_TO_ANSI(*ClassId), ClassId.Len());

	TStrongObjectPtr<UFusionTypeDescriptor> Descriptor = TStrongObjectPtr(NewObject<UFusionTypeDescriptor>(Lookup));
	
	Descriptor->Type = TStrongObjectPtr(Type);
	Descriptor->TypeHash = TypeHash;

	for (TFieldIterator<FProperty> It(Type); It; ++It)
	{
		FProperty* Prop = *It;
			
		//Skip none replicated properties.
		if ((Prop->GetPropertyFlags() & CPF_RepSkip) != 0)
		{
			continue;
		}

		if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			BuildOptions.ArrayPreAllocSize = GetPreAllocationSize(ArrayProp, BuildOptions.ArrayPreAllocSize);
			
			Descriptor->AddFusionNetworkArrayProperty(Lookup, ArrayProp, ParentProperty, BuildOptions.ArrayPreAllocSize, BuildOptions);
		}
	}

	return Descriptor;
}
