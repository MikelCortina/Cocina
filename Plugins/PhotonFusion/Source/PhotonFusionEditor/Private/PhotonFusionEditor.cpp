// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "PhotonFusionEditor.h"

#include "BlueprintCompilationManager.h"
#include "EdGraphSchema_K2.h"
#include "FusionActorComponent.h"
#include "K2Node.h"
#include "Engine/UserDefinedStruct.h"
#include "FusionComponentRefCustomization.h"
#include "FusionRPCFunctionNode.h"
#include "FusionStyle.h"
#include "RPCCompilerExtension.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyEditorModule.h"

DEFINE_LOG_CATEGORY(PhotonFusionEditorLog);

#define LOCTEXT_NAMESPACE "FPhotonFusionEditorModule"

void FPhotonFusionEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomPropertyTypeLayout(
		FFusionComponentRef::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FFusionComponentRefCustomization::MakeInstance)
	);

	FBlueprintCompilationManager::RegisterCompilerExtension(
	 UBlueprint::StaticClass(),
	NewObject<URPCCompilerExtension>()
	);

	PropertyModule.RegisterCustomClassLayout(
		UFusionRPCFunctionNode::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FFusionRPCFunctionNodeDetails::MakeInstance)
	);
	
	PropertyModule.NotifyCustomizationModuleChanged();

	FFusionStyle::Initialize();
}

void FPhotonFusionEditorModule::ShutdownModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.UnregisterCustomPropertyTypeLayout(FFusionComponentRef::StaticStruct()->GetFName());
	
	PropertyModule.UnregisterCustomClassLayout(UFusionRPCFunctionNode::StaticClass()->GetFName());
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FPhotonFusionEditorModule, PhotonFusionEditor)