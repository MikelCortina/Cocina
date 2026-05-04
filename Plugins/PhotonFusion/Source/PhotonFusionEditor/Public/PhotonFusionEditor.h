// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(PhotonFusionEditorLog, Log, All);

class FPhotonFusionEditorModule : public IModuleInterface
{
public:
	
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
