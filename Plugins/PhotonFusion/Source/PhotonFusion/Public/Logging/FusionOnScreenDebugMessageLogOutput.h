// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <Fusion/StringType.h>

#include "Fusion/LogOutput.h"

class FusionOnScreenDebugMessageLogOutput : public PhotonCommon::LogOutput
{
public:
	FusionOnScreenDebugMessageLogOutput() {}
	virtual ~FusionOnScreenDebugMessageLogOutput() override {}
	
	virtual void LogTrace(const PhotonCommon::CharType* Message) override;
	virtual void LogDebug(const PhotonCommon::CharType* Message) override;
	virtual void LogInfo(const PhotonCommon::CharType* Message) override;
	virtual void LogWarning(const PhotonCommon::CharType* Message) override;
	virtual void LogError(const PhotonCommon::CharType* Message) override;
};
