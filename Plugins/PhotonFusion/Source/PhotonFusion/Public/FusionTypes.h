// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "UObject/Object.h"
#include "FusionTypes.generated.h"


USTRUCT(BlueprintType)
struct FFusionNetworkedArray
{
	GENERATED_BODY()

	template<typename Type, typename SerializerType>
	static bool FusionArrayUpdate([[maybe_unused]] TArray<Type>& Items, [[maybe_unused]] SerializerType& ArraySerializer)
	{
		// Your actual replication code
		return false;
	}

public:
	void MarkItemDirty(FFastArraySerializerItem& Item)
	{
		//Do nothing for now
	}

	void MarkArrayDirty()
	{
		//Do nothing for now
	}
};

struct FFusionArrayHooks
{
	TFunction<void(void*, const TArrayView<int32>&, int32)> PreRemove;
	TFunction<void(void*, const TArrayView<int32>&, int32)> PostAdd;
	TFunction<void(void*, const TArrayView<int32>&, int32)> PostChange;
};

