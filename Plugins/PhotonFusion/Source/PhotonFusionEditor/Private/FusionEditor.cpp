// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "FusionEditor.h"

#include "BlueprintCompilationManager.h"
#include "EdGraphSchema_K2.h"
#include "FusionActorComponent.h"
#include "K2Node.h"
#include "Engine/UserDefinedStruct.h"
#include "FusionComponentRefCustomization.h"
#include "FusionRPCFunctionNode.h"
#include "FusionStyle.h"
#include "FusionRPCCompilerExtension.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyEditorModule.h"

// Fusion Debug Tools (formerly the FusionDebugTools plugin)
#include "NetworkDebugger/SFusionDebugTabManager.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"

DEFINE_LOG_CATEGORY(PhotonFusionEditorLog);

#define LOCTEXT_NAMESPACE "FPhotonFusionEditorModule"

const FName FPhotonFusionEditorModule::DebugToolsTabId(TEXT("FusionDebugTools"));

void FPhotonFusionEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomPropertyTypeLayout(
		FFusionComponentRef::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FFusionComponentRefCustomization::MakeInstance)
	);

	FBlueprintCompilationManager::RegisterCompilerExtension(
	 UBlueprint::StaticClass(),
	NewObject<UFusionRPCCompilerExtension>()
	);

	PropertyModule.RegisterCustomClassLayout(
		UFusionRPCFunctionNode::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FFusionRPCFunctionNodeDetails::MakeInstance)
	);

	PropertyModule.NotifyCustomizationModuleChanged();

	FFusionStyle::Initialize();

	// --- Fusion Debug Tools (bandwidth monitor + string heap viewer) ---
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		DebugToolsTabId,
		FOnSpawnTab::CreateRaw(this, &FPhotonFusionEditorModule::SpawnDebugToolsTab))
		.SetDisplayName(LOCTEXT("FusionToolsTabTitle", "Fusion Debug Tools"))
		.SetTooltipText(LOCTEXT("FusionToolsTabTooltip", "Monitor Bandwidth or Debug String Allocations"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.StatsViewer"));

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPhotonFusionEditorModule::RegisterDebugToolsMenus));
}

void FPhotonFusionEditorModule::ShutdownModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.UnregisterCustomPropertyTypeLayout(FFusionComponentRef::StaticStruct()->GetFName());

	PropertyModule.UnregisterCustomClassLayout(UFusionRPCFunctionNode::StaticClass()->GetFName());

	// --- Fusion Debug Tools ---
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DebugToolsTabId);
}

TSharedRef<SDockTab> FPhotonFusionEditorModule::SpawnDebugToolsTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(NomadTab)
		[
			SNew(SFusionDebugTabManager)
		];
}

void FPhotonFusionEditorModule::RegisterDebugToolsMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	FToolMenuSection& Section = Menu->FindOrAddSection("Photon");
	Section.Label = LOCTEXT("PhotonMenuSection", "Photon");

	Section.AddMenuEntry(
		"FusionDebugTools",
		LOCTEXT("FusionToolsMenuEntry", "Fusion Debug Tools"),
		LOCTEXT("FusionToolsMenuTooltip", "Open Fusion Debug window"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.StatsViewer"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(DebugToolsTabId);
		}))
	);
}


#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPhotonFusionEditorModule, PhotonFusionEditor)