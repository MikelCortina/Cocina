// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "FusionShared.h"

#include "Developer/Settings/Public/ISettingsModule.h"
#include "FusionClient.h"
#include "FusionOnlineSubsystemSettings.h"
#include "Logging/FusionStandardLogOutput.h"
#include "Logging/FusionOnScreenDebugMessageLogOutput.h"

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

	const UFusionOnlineSubsystemSettings* Settings = UFusionOnlineSubsystemSettings::GetPhotonOnlineSettings();

	if (Settings->EnabledLogOutput & StaticCast<uint8>(EFusionLogOutput::StandardLogOutput))
	{
		FusionStandardLogOutput* LogOutput = new FusionStandardLogOutput();
		LogOutputArr.Add(LogOutput);
		PhotonCommon::AddLogOutput(LogOutput);
	}

	if (Settings->EnabledLogOutput & StaticCast<uint8>(EFusionLogOutput::OnScreenDebugMessageLogOutput))
	{
		FusionOnScreenDebugMessageLogOutput* LogOutput = new FusionOnScreenDebugMessageLogOutput();
		LogOutputArr.Add(LogOutput);
		PhotonCommon::AddLogOutput(LogOutput);
	}

	PhotonCommon::SetLogLevelsFromBitmask(Settings->EnabledLogLevels);
}

void FPhotonFusion::ShutdownModule()
{
#if WITH_EDITOR
	FEditorDelegates::BeginPIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
#endif

	for (PhotonCommon::LogOutput* LogOutput : LogOutputArr)
	{
		if (!PhotonCommon::RemoveLogOutput(LogOutput))
		{
			UE_LOG(LogFusion, Error, TEXT("Error removing Fusion log output"));
		}
		delete LogOutput;
	}
	LogOutputArr.Empty();
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
