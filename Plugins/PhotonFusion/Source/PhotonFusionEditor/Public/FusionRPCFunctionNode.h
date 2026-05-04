// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "FusionHelpers.h"
#include "IDetailCustomization.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "FusionRPCFunctionNode.generated.h"

UCLASS(meta=(DisplayName="Call RPC With Hook", Keywords="call function rpc multicast"))
class UFusionRPCFunctionNode : public UK2Node_CallFunction
{
	GENERATED_BODY()

public:

	UPROPERTY()
	UFunction* FunctionToCall;

	UPROPERTY(EditAnywhere, Category="Fusion")
	FName ReferencedEventName;

	UPROPERTY(EditAnywhere, Category="Fusion")
	EFusionRPCTarget RPCTarget;

	void RemoveRPCMarkup(UBlueprint* Blueprint);
	void AddRPCMarkup(UBlueprint* Blueprint);
	
	void Rebuild();

	UFusionRPCFunctionNode();
	
	void DestroyState() const;

	UEdGraph* GetFunctionGraph(UBlueprint* Blueprint) const;

	bool IsOutOfSync(UFunction* Function, UK2Node_Event* SourceEvent) const;
	
	UFunction* CreateOrUpdateFunction(UBlueprint* Blueprint, UClass* OwnerClass, FName FunctionName, UK2Node_Event* SourceEvent) const;
	
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	virtual void DestroyNode() override;

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	virtual FLinearColor GetNodeTitleColor() const override;

	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	virtual FText GetMenuCategory() const override;
	
	virtual void PostPlacedNewNode() override;
	
	void SetEventName(FName Name);

	FProperty* MakePropertyFromPin(UFunction* Function, const TSharedPtr<FUserPinInfo>& Pin, EObjectFlags Flags) const;

	UK2Node_Event* FindEventNode(UBlueprint* BP, FName Name) const;
	
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;

	UEdGraph* MakeFunctionGraph(UBlueprint* Blueprint, UFunction* GeneratedFunction, FName FunctionName, UK2Node_Event* SourceEventNode);

	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;

	template <typename SignatureType>
	static void AddFunctionGraph(UBlueprint* Blueprint, class UEdGraph* Graph, bool bIsUserCreated, SignatureType* SignatureFromObject)
	{
		FBlueprintEditorUtils::CreateFunctionGraph(Blueprint, Graph, bIsUserCreated, SignatureFromObject);
	
		Blueprint->FunctionGraphs.Add(Graph);
	
		// Potentially adjust variable names for any child blueprints
		FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, Graph->GetFName());
	
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

protected:
	/** Called during node construction to set the target function */
	virtual void AllocateDefaultPins() override;
	virtual bool ShouldShowNodeProperties() const override;
};

class FFusionRPCFunctionNodeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();


	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	UBlueprint* GetBlueprintFromNode(UObject* Object);

	TArray<TSharedPtr<FName>> Options;
};
