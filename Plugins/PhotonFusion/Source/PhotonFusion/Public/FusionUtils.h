// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "FusionShared.h"
#include "Fusion/Aliases.h"

uint64 HashFString(const FString& String);
PHOTONFUSION_API FString ObjectIdToString(SharedMode::ObjectId Id);
uint32 Crc32(const uint8* Buffer, uint32 Len);

