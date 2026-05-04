// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "FusionShared.h"

#include "Developer/Settings/Public/ISettingsModule.h"
#include "FusionClient.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "FPhotonFusion"


DEFINE_LOG_CATEGORY(LogFusion);

void FPhotonFusion::StartupModule()
{
	if (FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{

	}

#if WITH_EDITOR
	FEditorDelegates::BeginPIE.AddRaw(this, &FPhotonFusion::OnBeginPIE);
	FEditorDelegates::EndPIE.AddRaw(this, &FPhotonFusion::OnEndPIE);
#endif
}

void FPhotonFusion::ShutdownModule()
{
#if WITH_EDITOR
	FEditorDelegates::BeginPIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
#endif
}

void FPhotonFusion::OnBeginPIE([[maybe_unused]] bool bArg)
{
	UFusionHelpers::InstanceId = FGuid::NewGuid();
}

void FPhotonFusion::OnEndPIE([[maybe_unused]] bool bArg)
{
	UFusionHelpers::InstanceId = FGuid();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPhotonFusion, PhotonFusion)
