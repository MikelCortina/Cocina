// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <vector>
#include "CoreMinimal.h"

#define SEND_FUSIONRPC(...);

#define FUSION_BODY_MACRO_COMBINE_INNER(A,B,C,D) A##B##C##D

// Second level: Uses the first level to concatenate after expansion
#define FUSION_BODY_MACRO_COMBINE(A,B,C,D) FUSION_BODY_MACRO_COMBINE_INNER(A,B,C,D)


//Primary macro for generatic statics.
#define FUSION_BODY() FUSION_BODY_MACRO_COMBINE(CURRENT_FILE_ID,_,__LINE__,_FUSION_BODY)