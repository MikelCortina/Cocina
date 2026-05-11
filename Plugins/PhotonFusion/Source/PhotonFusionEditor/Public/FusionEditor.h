// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class SDockTab;
class FSpawnTabArgs;

DECLARE_LOG_CATEGORY_EXTERN(PhotonFusionEditorLog, Log, All);

class FPhotonFusionEditorModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// Fusion Debug Tools (bandwidth monitor + string heap viewer) — registered alongside
	// the editor module so we don't ship a separate plugin for it.
	TSharedRef<SDockTab> SpawnDebugToolsTab(const FSpawnTabArgs& Args);
	void RegisterDebugToolsMenus();

	static const FName DebugToolsTabId;
};
