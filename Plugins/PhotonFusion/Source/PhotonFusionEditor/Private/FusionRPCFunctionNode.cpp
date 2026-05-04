// Copyright 2026 Exit Games GmbH. All Rights Reserved.


#include "FusionRPCFunctionNode.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintEditor.h"
#include "BlueprintNodeSpawner.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "FusionHelpers.h"
#include "FusionOnlineSubsystem.h"
#include "FusionStyle.h"
#include "K2Node_CustomEvent.h"
#include "PhotonFusionEditor.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "K2Node_StructMemberSet.h"
#include "K2Node_VariableGet.h"
#include "KismetCompiler.h"
#include "SMyBlueprint.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "FusionCustomRPCEvent"

void UFusionRPCFunctionNode::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(this);
	if (!Blueprint)
	{
		return;
	}
	
	if (ReferencedEventName.IsNone() || ReferencedEventName == "None")
	{
		return;
	}
	
	UK2Node_Event* SourceEventNode = FindEventNode(Blueprint, ReferencedEventName);
	if (!SourceEventNode)
	{
		return;
	}
	
	FName FunctionName = FName(*FString::Printf(TEXT("%s_Call"), *ReferencedEventName.ToString()));
	
	auto GeneratedFunction = CreateOrUpdateFunction(Blueprint, CompilerContext.NewClass, FunctionName, SourceEventNode);
	auto SkeletonFunction = CreateOrUpdateFunction(Blueprint, Blueprint->SkeletonGeneratedClass, FunctionName, SourceEventNode);

	if (!GeneratedFunction)
		return;
	
	FunctionReference.SetExternalMember(FunctionName, CompilerContext.NewClass);

	UEdGraph* FuncGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == FunctionName)
		{
			FuncGraph = Graph;
			break;
		}
	}
	
	if (!FuncGraph)
	{
		MakeFunctionGraph(Blueprint, GeneratedFunction, FunctionName, SourceEventNode);
	}
	else if (IsOutOfSync(GeneratedFunction, SourceEventNode))
	{
		FBlueprintEditorUtils::RemoveGraph(Blueprint, FuncGraph, EGraphRemoveFlags::Recompile);
		MakeFunctionGraph(Blueprint, GeneratedFunction, FunctionName, SourceEventNode);
	}

	AddRPCMarkup(Blueprint);

	Super::ExpandNode(CompilerContext, SourceGraph);
}

void UFusionRPCFunctionNode::DestroyNode()
{
	Super::DestroyNode();
	
	RemoveRPCMarkup(FBlueprintEditorUtils::FindBlueprintForNode(this));
	DestroyState();
}

void UFusionRPCFunctionNode::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* NodeClass = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(NodeClass))
		return;
		
	UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(NodeClass);
	
	ActionRegistrar.AddBlueprintAction(NodeClass, NodeSpawner);
}

FText UFusionRPCFunctionNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!ReferencedEventName.IsNone())
	{
		FName FunctionName(*FString::Printf(TEXT("%s_Call"), *ReferencedEventName.ToString()));
		
		return FText::FromName(FunctionName);
	}
	
	return NSLOCTEXT("Fusion", "Call Fusion RPC", "Custom Fusion RPC");
}

FLinearColor UFusionRPCFunctionNode::GetNodeTitleColor() const
{
	return FLinearColor(0.16f, 0.2f, 0.44f);
}

FSlateIcon UFusionRPCFunctionNode::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	
	return FSlateIcon(FFusionStyle::GetStyleSetName(), "Fusion.Icon128");
}

FText UFusionRPCFunctionNode::GetMenuCategory() const
{
	return LOCTEXT("CallFunction", "Call Function");
}

void UFusionRPCFunctionNode::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();
}

void UFusionRPCFunctionNode::SetEventName(FName Name)
{
	DestroyState();

	UBlueprint* Blueprint = GetBlueprint();
	
	RemoveRPCMarkup(Blueprint);
	
	ReferencedEventName = Name;
	
	AddRPCMarkup(Blueprint);
	
	if (Blueprint && !ReferencedEventName.IsNone())
	{
		Rebuild();
	}

	ReconstructNode();
}

void UFusionRPCFunctionNode::RemoveRPCMarkup(UBlueprint* Blueprint)
{
	if (!Blueprint)
		return;
	
	FName VarName(*FString::Printf(TEXT("__FUSIONRPCEVENT_%s"), *ReferencedEventName.ToString()));
	FGuid guid = FBlueprintEditorUtils::FindMemberVariableGuidByName(Blueprint, VarName);

	if (guid.IsValid() )
	{
		// Find the variable index in NewVariables
		int32 VarIndex = INDEX_NONE;
		for (int32 i = 0; i < Blueprint->NewVariables.Num(); ++i)
		{
			if (Blueprint->NewVariables[i].VarName == VarName)
			{
				VarIndex = i;
				break;
			}
		}

		if (VarIndex != INDEX_NONE)
		{
			Blueprint->Modify();

			// Remove from the list
			Blueprint->NewVariables.RemoveAt(VarIndex);

			// Clean up pins/graph references
			FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, VarName);

			// Validate and update the blueprint
			FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, VarName);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

			UE_LOG(LogTemp, Warning, TEXT("Removed hidden RPC markup variable: %s"), *VarName.ToString());
		}
	}
}

void UFusionRPCFunctionNode::AddRPCMarkup(UBlueprint* Blueprint)
{
	FName VarName(*FString::Printf(TEXT("__FUSIONRPCEVENT_%s"), *ReferencedEventName.ToString()));
	FGuid guid = FBlueprintEditorUtils::FindMemberVariableGuidByName(Blueprint, VarName);

	if (!guid.IsValid() )
	{
		FEdGraphPinType HiddenPinType;
		HiddenPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		HiddenPinType.ContainerType = EPinContainerType::None;
		
		// First we need to see if there is already a variable with that name, in this blueprint or parent class
		TSet<FName> CurrentVars;
		FBlueprintEditorUtils::GetClassVariableList(Blueprint, CurrentVars);
		if(CurrentVars.Contains(VarName))
		{
			return;
		}

		Blueprint->Modify();
		
		FBPVariableDescription NewVar;
		NewVar.VarName = VarName;
		NewVar.VarGuid = FGuid::NewGuid();
		NewVar.FriendlyName = FName::NameToDisplayString( VarName.ToString(), (HiddenPinType.PinCategory == UEdGraphSchema_K2::PC_Boolean) ? true : false );
		NewVar.VarType = HiddenPinType;

		NewVar.ReplicationCondition = COND_None;
		NewVar.Category = UEdGraphSchema_K2::VR_DefaultCategory;
		
		// user created variables should be none of these things
		NewVar.VarType.bIsConst       = false;
		NewVar.VarType.bIsWeakPointer = false;
		NewVar.VarType.bIsReference   = false;

		Blueprint->NewVariables.Add(NewVar);
		
		FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, VarName);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
}

void UFusionRPCFunctionNode::Rebuild()
{
	if (UBlueprint* Blueprint = GetBlueprint())
	{
		if (UK2Node_Event* SourceEvent = FindEventNode(Blueprint, ReferencedEventName))
		{
			if (auto FuncGraph = GetFunctionGraph(Blueprint)) {
				FBlueprintEditorUtils::RemoveGraph(Blueprint, FuncGraph, EGraphRemoveFlags::Recompile);
			}
			
			FName FunctionName(*FString::Printf(TEXT("%s_Call"), *ReferencedEventName.ToString()));
			auto GeneratedFunction = CreateOrUpdateFunction(Blueprint, Blueprint->GeneratedClass, FunctionName, SourceEvent);
			auto SkeletonFunction = CreateOrUpdateFunction(Blueprint, Blueprint->SkeletonGeneratedClass, FunctionName, SourceEvent);
			
			MakeFunctionGraph(Blueprint, SkeletonFunction, FunctionName, SourceEvent);
		}
		
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	
	// Rebuild pins in editor
	ReconstructNode();
}

UFusionRPCFunctionNode::UFusionRPCFunctionNode(): FunctionToCall(nullptr)
{
}

void UFusionRPCFunctionNode::DestroyState() const
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(this);
	if (!Blueprint)
		return;

	FName functionName = FName(*FString::Printf(TEXT("%s_Call"), *ReferencedEventName.ToString()));
	
	if (UClass* SkelClass = Blueprint->SkeletonGeneratedClass)
	{
		if (UFunction* OldFunc = SkelClass->FindFunctionByName(functionName))
		{
			OldFunc->MarkAsGarbage();
		}
	}
	if (UClass* GenClass = Blueprint->GeneratedClass)
	{
		if (UFunction* OldFunc = GenClass->FindFunctionByName(functionName))
		{
			OldFunc->MarkAsGarbage();
		}
	}
	
	if (UEdGraph* FuncGraph = GetFunctionGraph(Blueprint))
	{
		FBlueprintEditorUtils::RemoveGraph(Blueprint, FuncGraph, EGraphRemoveFlags::Recompile);
	}
}

UEdGraph* UFusionRPCFunctionNode::GetFunctionGraph(UBlueprint* Blueprint) const
{
	FName functionName = FName(*FString::Printf(TEXT("%s_Call"), *ReferencedEventName.ToString()));
	
	UEdGraph* FuncGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == functionName)
		{
			FuncGraph = Graph;
			break;
		}
	}

	return FuncGraph;
}

bool UFusionRPCFunctionNode::IsOutOfSync(UFunction* Function, UK2Node_Event* SourceEvent) const
{
	bool RebuildFunction = false;
	
	auto FunctionPins = Pins.FilterByPredicate(
	[](const UEdGraphPin* Pin)
	{
		// Must be input
		if (Pin->Direction != EGPD_Input)
		{
			return false;
		}

		// Exclude exec pins
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return false;
		}

		// Exclude self/meta pins
		if (Pin->PinName == UEdGraphSchema_K2::PN_Self ||
			Pin->PinName == UEdGraphSchema_K2::PN_Execute ||
			Pin->PinName == UEdGraphSchema_K2::PN_Then ||
			Pin->PinName == UEdGraphSchema_K2::PN_EntryPoint)
		{
			return false;
		}

		return true;
	});

	auto EventPins = SourceEvent->UserDefinedPins.FilterByPredicate(
	[](TSharedPtr<FUserPinInfo> Pin)
	{
		// Must be input
		if (Pin->DesiredPinDirection != EGPD_Output)
		{
			return false;
		}

		// Exclude exec pins
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return false;
		}

		// Exclude self/meta pins
		if (Pin->PinName == UEdGraphSchema_K2::PN_Self ||
			Pin->PinName == UEdGraphSchema_K2::PN_Execute ||
			Pin->PinName == UEdGraphSchema_K2::PN_Then ||
			Pin->PinName == UEdGraphSchema_K2::PN_EntryPoint ||
			Pin->PinName == UEdGraphSchema_K2::PN_DelegateEntry)
		{
			return false;
		}

		return true;
	});
    
    if (FunctionPins.Num() != EventPins.Num())
    {
        RebuildFunction = true;
    }
	else
	{
		for (int i = 0; i < EventPins.Num(); i++)
		{
			auto EventPin = EventPins[i];
			auto FunctionPin = FunctionPins[i];

			if (EventPin->PinType.PinCategory != FunctionPin->PinType.PinCategory)
			{
				RebuildFunction = true;
			}
		}
	}

	//Ensure function matches the event layout.
	int index = 0;
	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		FProperty* Prop = *It;

		//&UE_LOG(PhotonFusionEditorLog, Warning, TEXT("Property: %s At Index: %d"), *Prop->GetName(), index);

		if (!Prop->HasAnyPropertyFlags(CPF_Parm))
		{
			break;
		}
		
		if (index >= EventPins.Num())
		{
			RebuildFunction = true;
			break;
		}
		
		auto EventPin = EventPins[index];

		FName OutCategory{};
		FName OutSubCategory{};
		UObject* OutSubCategoryObject{nullptr};
		bool bOutIsWeakPointer{};
	
		UEdGraphSchema_K2::GetPropertyCategoryInfo(Prop, OutCategory, OutSubCategory, OutSubCategoryObject, bOutIsWeakPointer);
		if (EventPin->PinType.PinCategory != OutCategory)
		{
			UE_LOG(PhotonFusionEditorLog, Warning, TEXT("Missmatch between eventpin at index: %d:  of type: %s   Property Type is: %s"), index, *EventPin->PinType.PinCategory.ToString(), *OutCategory.ToString());
			
			RebuildFunction = true;
			break;
		}

		index++;
	}
	
	return RebuildFunction;
}

UFunction* UFusionRPCFunctionNode::CreateOrUpdateFunction(UBlueprint* Blueprint, UClass* OwnerClass, FName FunctionName, UK2Node_Event* SourceEvent) const
{
	if (!Blueprint) return nullptr;
	
	UFunction* Func = OwnerClass->FindFunctionByName(FunctionName);
	if (Func)
	{
		if (IsOutOfSync(Func, SourceEvent))
		{
			UE_LOG(PhotonFusionEditorLog, Warning, TEXT("Destroy Previous Function: %s"), *FunctionName.ToString());
			
			if (UFunction* OldFunc = OwnerClass->FindFunctionByName(FunctionName))
			{
				OldFunc->MarkAsGarbage();
				OwnerClass->RemoveFunctionFromFunctionMap(OldFunc);
			}
		}
		else
		{
			return Func;
		}
	}

	Func = NewObject<UFunction>(OwnerClass, FunctionName);
	Func->FunctionFlags |= FUNC_Public | FUNC_BlueprintCallable;
	OwnerClass->AddFunctionToFunctionMap(Func, FunctionName);
	
	Func->Children = nullptr;
	Func->ParmsSize = 0;

	//Add in reverse order since the UFunction stores the parameters in a linked list. And so the last added entry becomes the Head. And would create the pins in that order (reversed)
	//Made a dumb solution for this here.
	for (int32 Index = SourceEvent->UserDefinedPins.Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<FUserPinInfo>& Pin = SourceEvent->UserDefinedPins[Index];
		if (FProperty* Property = MakePropertyFromPin(Func, Pin, RF_Public))
		{
			Property->SetPropertyFlags(CPF_Parm);
			Func->AddCppProperty(Property);
		}
	}

	Func->Bind();
	Func->StaticLink(true);

	return Func;
}

FProperty* UFusionRPCFunctionNode::MakePropertyFromPin(UFunction* Function, const TSharedPtr<FUserPinInfo>& Pin, EObjectFlags Flags) const
{
	const FString& Category = Pin->PinType.PinCategory.ToString();

	if (Category == UEdGraphSchema_K2::PC_Byte)
	{
		return new FByteProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Boolean)
	{
		return new FBoolProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Int)
	{
		return new FIntProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Int64)
	{
		return new FInt64Property(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Float)
	{
		return new FFloatProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Double)
	{
		return new FDoubleProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Real)
	{
		if (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
		{
			return new FDoubleProperty(Function, Pin->PinName, Flags);
		}
		else
		{
			return new FFloatProperty(Function, Pin->PinName, Flags);
		}
	}
	else if (Category == UEdGraphSchema_K2::PC_String)
	{
		return new FStrProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Name)
	{
		return new FNameProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Enum)
	{
		return new FEnumProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Struct)
	{
		return new FStructProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Object)
	{
		return new FObjectProperty(Function, Pin->PinName, Flags);
	}
	else if (Category == UEdGraphSchema_K2::PC_Class)
	{
		return new FClassProperty(Function, Pin->PinName, Flags);
	}

	UE_LOG(PhotonFusionEditorLog, Warning, TEXT("Unable to make property for a pin: '%s'"), *Category);

	return nullptr;
}

UK2Node_Event* UFusionRPCFunctionNode::FindEventNode(UBlueprint* BP, FName EventName) const
{
	UK2Node_Event* SourceEventNode{nullptr};
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		for (UEdGraphNode* CurrentNode : Graph->Nodes)
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(CurrentNode))
			{
				if (EventNode->CustomFunctionName == EventName)
				{
					SourceEventNode = EventNode;
					break;
				}
			}
		}
	}

	return SourceEventNode;
}

void UFusionRPCFunctionNode::ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	UBlueprint* Blueprint = GetBlueprint();
	if (Blueprint && ReferencedEventName.IsNone())
		return;

	UK2Node_Event* SourceEvent = FindEventNode(Blueprint, ReferencedEventName);
	if (!SourceEvent)
		return;

	FName FunctionName(*FString::Printf(TEXT("%s_Call"), *ReferencedEventName.ToString()));

	// // Update skeleton + generated
	CreateOrUpdateFunction(Blueprint, Blueprint->SkeletonGeneratedClass, FunctionName, SourceEvent);
	CreateOrUpdateFunction(Blueprint, Blueprint->GeneratedClass, FunctionName, SourceEvent);
}

UEdGraph* UFusionRPCFunctionNode::MakeFunctionGraph(UBlueprint* Blueprint, UFunction* GeneratedFunction, FName FunctionName, UK2Node_Event* SourceEventNode)
{
	UEdGraph* FuncGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FunctionName,UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

	FuncGraph->Modify();
	FuncGraph->bAllowDeletion = false;
	FuncGraph->bEditable = false;
	FuncGraph->bAllowRenaming = false;
	
	AddFunctionGraph<UFunction>(Blueprint, FuncGraph, false, nullptr);
	
	//Find entry node, should only be one.
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : FuncGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode)
			break;
	}

	if (!EntryNode)
		return nullptr;

	EntryNode->bIsEditable = false; 

	auto SourceEvent = FindEventNode(Blueprint, ReferencedEventName);

	if (!SourceEvent)
		return nullptr;
	
	auto EventPins = SourceEvent->UserDefinedPins.FilterByPredicate(
	[](TSharedPtr<FUserPinInfo> Pin)
	{
		// Must be input
		if (Pin->DesiredPinDirection != EGPD_Output)
		{
			return false;
		}

		// Exclude exec pins
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return false;
		}

		// Exclude self/meta pins
		if (Pin->PinName == UEdGraphSchema_K2::PN_Self ||
			Pin->PinName == UEdGraphSchema_K2::PN_Execute ||
			Pin->PinName == UEdGraphSchema_K2::PN_Then ||
			Pin->PinName == UEdGraphSchema_K2::PN_EntryPoint ||
			Pin->PinName == UEdGraphSchema_K2::PN_DelegateEntry)
		{
			return false;
		}

		return true;
	});

	for (TSharedPtr<FUserPinInfo> Pin : EventPins)
	{
		TSharedPtr<FUserPinInfo> Info = MakeShared<FUserPinInfo>();
		Info->PinName = Pin->PinName;
		Info->PinType = Pin->PinType;
		EntryNode->UserDefinedPins.Add(Info);
	}

	//Reconstruct once we have the pins set.
	EntryNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	FName BufferVarName(TEXT("ParamBuffer"));
	
	FEdGraphPinType VarType;
	VarType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	VarType.ContainerType = EPinContainerType::Array;
	
	FBlueprintEditorUtils::AddLocalVariable(Blueprint, FuncGraph, BufferVarName, VarType);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	float StartX = 200.f;
	float StartY = 200.f;
	FVector2D NewNodePos(StartX, StartY);
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	
	static FName InvokeFunctionName = GET_FUNCTION_NAME_CHECKED(UFusionHelpers, InvokeCustomRPC);
	UFunction* InvokeFunction = UFusionHelpers::StaticClass()->FindFunctionByName(InvokeFunctionName);
	if (!InvokeFunction)
		return nullptr;

	static FName GetFunctionDescriptorName = GET_FUNCTION_NAME_CHECKED(UFusionHelpers, GetFunctionDescriptor);
	UFunction* GetFunctionDescriptor = UFusionHelpers::StaticClass()->FindFunctionByName(GetFunctionDescriptorName);
	if (!GetFunctionDescriptor)
		return nullptr;
	
	static FName AddParamFunctionName = GET_FUNCTION_NAME_CHECKED(UFusionHelpers, AddParamToBuffer);
	UFunction* AddParamFunction = UFusionHelpers::StaticClass()->FindFunctionByName(AddParamFunctionName);
	if (!AddParamFunction)
		return nullptr;
	
	static FName IsInRoomMethodName = GET_FUNCTION_NAME_CHECKED(UFusionOnlineSubsystem, IsInRoom);
	UFunction* IsInRoomFunction = UFusionOnlineSubsystem::StaticClass()->FindFunctionByName(IsInRoomMethodName);
	if (!IsInRoomFunction)
		return nullptr;

	UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(FuncGraph);

	FGuid LocalVarGuid = FBlueprintEditorUtils::FindLocalVariableGuidByName(Blueprint, FuncGraph, BufferVarName);
	if (LocalVarGuid.IsValid())
	{
		GetNode->VariableReference.SetLocalMember(BufferVarName, FuncGraph->GetName(), LocalVarGuid);
	}
			
	FuncGraph->AddNode(GetNode, true, false);
	GetNode->CreateNewGuid();
	GetNode->PostPlacedNewNode();
	GetNode->NodePosX = NewNodePos.X;
	GetNode->NodePosY = NewNodePos.Y + 500.f;

	//Since the backing property does not yet exist, hardcode in the pin creation for this node.
	UEdGraphPin* VariablePin = GetNode->CreatePin(EGPD_Output, NAME_None, BufferVarName);
	VariablePin->PinType = VarType;
	if (const UEdGraphSchema_K2* K2_Schema = Cast<const UEdGraphSchema_K2>(FuncGraph->GetSchema()))
	{
		K2_Schema->SetPinAutogeneratedDefaultValueBasedOnType(VariablePin);
	}

	UK2Node_GetSubsystem* GetSubsystemNode = NewObject<UK2Node_GetSubsystem>(FuncGraph);
	FuncGraph->AddNode(GetSubsystemNode, false, false);

	GetSubsystemNode->Initialize(UFusionOnlineSubsystem::StaticClass());
	GetSubsystemNode->AllocateDefaultPins();
	GetSubsystemNode->NodePosX = NewNodePos.X;
	GetSubsystemNode->NodePosY = NewNodePos.Y;
	NewNodePos.X += 400.0f;

	UK2Node_CallFunction* IsInRoomNode = NewObject<UK2Node_CallFunction>(FuncGraph);
	FuncGraph->AddNode(IsInRoomNode, false, false);
	IsInRoomNode->SetFromFunction(IsInRoomFunction);
	IsInRoomNode->AllocateDefaultPins();
	IsInRoomNode->NodePosX = NewNodePos.X;
	IsInRoomNode->NodePosY = NewNodePos.Y;
	NewNodePos.X += 400.0f;

	UEdGraphPin* SubsystemPin = GetSubsystemNode->GetResultPin();
	UEdGraphPin* IsInRoomTargetPin = IsInRoomNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
	Schema->TryCreateConnection(SubsystemPin, IsInRoomTargetPin);

	UK2Node_IfThenElse* IsPhotonBranchNode = NewObject<UK2Node_IfThenElse>(FuncGraph);
	FuncGraph->AddNode(IsPhotonBranchNode, true, false);
	IsPhotonBranchNode->CreateNewGuid();
	IsPhotonBranchNode->PostPlacedNewNode();
	IsPhotonBranchNode->AllocateDefaultPins();
	IsPhotonBranchNode->NodePosX = NewNodePos.X;
	IsPhotonBranchNode->NodePosY = NewNodePos.Y;
	NewNodePos.X += 400.0f;
	NewNodePos.Y += 200.0f;

	Schema->TryCreateConnection(IsInRoomNode->GetReturnValuePin(),IsPhotonBranchNode->GetConditionPin());

	UEdGraphPin* EntryPin = EntryNode->GetThenPin();
	UEdGraphPin* CallPin  = IsPhotonBranchNode->GetExecPin();
	Schema->TryCreateConnection(EntryPin, CallPin);
	
	UK2Node_CallFunction* CallEventNode = NewObject<UK2Node_CallFunction>(FuncGraph);
	FuncGraph->AddNode(CallEventNode, false, false);

	//Since this is called potentially outside expand we only set the reference, the function is not built/valid here.
	CallEventNode->FunctionReference.SetSelfMember(ReferencedEventName);
	CallEventNode->AllocateDefaultPins();
	CallEventNode->NodePosX = NewNodePos.X;
	CallEventNode->NodePosY = NewNodePos.Y + 300.0f;
	NewNodePos.X += 400.0f;

	UEdGraphPin* ElsePin = IsPhotonBranchNode->GetElsePin();
	UEdGraphPin* CallExecPin = CallEventNode->GetExecPin();
	Schema->TryCreateConnection(ElsePin, CallExecPin);

	//Wire the inputs for the none fusion case, make the fusion call node work in singleplayer similar to that of the standard unreal rpc/custom event functionality.
	for (UEdGraphPin* ParameterPin : EntryNode->Pins)
	{
		if (ParameterPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
		{
			UEdGraphPin* CallParamPin =CallEventNode->FindPin(ParameterPin->PinName);

			if (!CallParamPin)
			{
				continue; 
			}

			Schema->TryCreateConnection(ParameterPin, CallParamPin);

			
		}
	}

	
	UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(FuncGraph);
	FuncGraph->AddNode(SelfNode, true, false);
	SelfNode->CreateNewGuid();
	SelfNode->PostPlacedNewNode();
	SelfNode->NodePosX = StartX;
	SelfNode->NodePosY = NewNodePos.Y + 100.f;
	SelfNode->AllocateDefaultPins();
	
	//Just make the positioning in the graph more readable.
	SelfNode->NodePosX = StartX;
	SelfNode->NodePosY = NewNodePos.Y + 200.f;

	//Pin used to wire the source into things, (self)
	UEdGraphPin* SelfOutputPin = SelfNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);

	UK2Node_CallFunction* GetDesciptorNode = NewObject<UK2Node_CallFunction>(FuncGraph);
	FuncGraph->AddNode(GetDesciptorNode, false, false);

	GetDesciptorNode->SetFromFunction(GetFunctionDescriptor);
	GetDesciptorNode->AllocateDefaultPins();
	GetDesciptorNode->NodePosX = NewNodePos.X;
	GetDesciptorNode->NodePosY = NewNodePos.Y;

	if (UEdGraphPin* EventNamePin = GetDesciptorNode->FindPin(TEXT("EventName")))
	{
		EventNamePin->DefaultValue = *ReferencedEventName.ToString();
	}

	UEdGraphPin* GetDesciptorSourcePin = GetDesciptorNode->FindPinChecked(TEXT("Source"));
	Schema->TryCreateConnection(SelfOutputPin, GetDesciptorSourcePin);

	UEdGraphPin* TruePin = IsPhotonBranchNode->GetThenPin();
	UEdGraphPin* PhotonDescriptorPin  = GetDesciptorNode->GetExecPin();
	Schema->TryCreateConnection(TruePin, PhotonDescriptorPin);

	UEdGraphPin* ReturnValuePin = GetDesciptorNode->GetReturnValuePin();

	NewNodePos.X += 400.0f;

	auto validFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("IsValid"));
	UK2Node_CallFunction* IsValidNode = NewObject<UK2Node_CallFunction>(FuncGraph);
	FuncGraph->AddNode(IsValidNode, true, false);
	IsValidNode->SetFromFunction(validFunction);
	IsValidNode->AllocateDefaultPins();
	IsValidNode->NodePosX = NewNodePos.X;
	IsValidNode->NodePosY = NewNodePos.Y;

	NewNodePos.X += 200.0f;
	
	UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(FuncGraph);
	FuncGraph->AddNode(BranchNode, true, false);
	BranchNode->CreateNewGuid();
	BranchNode->PostPlacedNewNode();
	BranchNode->AllocateDefaultPins();
	BranchNode->NodePosX = NewNodePos.X;
	BranchNode->NodePosY = NewNodePos.Y;

	Schema->TryCreateConnection(ReturnValuePin,
								IsValidNode->FindPin(TEXT("Object")));

	Schema->TryCreateConnection(IsValidNode->GetReturnValuePin(),
								BranchNode->GetConditionPin());


	Schema->TryCreateConnection(GetDesciptorNode->GetThenPin(), BranchNode->GetExecPin());

	NewNodePos.X += 700.0f;
	NewNodePos.Y += 200.0f;

	int PropertyIndex = 0;
	UK2Node* previousNode = BranchNode;
	for (UEdGraphPin* ParameterPin : EntryNode->Pins)
	{
		if (ParameterPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
		{
			UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(FuncGraph);
			FuncGraph->AddNode(CallNode, false, false);

			CallNode->SetFromFunction(AddParamFunction);
			CallNode->AllocateDefaultPins();
			
			CallNode->NodePosX = NewNodePos.X;
			CallNode->NodePosY = NewNodePos.Y;
			NewNodePos.X += 200.0f;
			NewNodePos.Y += 100.0f;
			
			//Connect input value pin to AddParam function
			UEdGraphPin* TargetPin = CallNode->FindPin(TEXT("Value"));
			Schema->TryCreateConnection(ParameterPin, TargetPin);

			UEdGraphPin* SourcePin = CallNode->FindPinChecked(TEXT("Source"));
			Schema->TryCreateConnection(SelfOutputPin, SourcePin);

			UEdGraphPin* DesciptorPin = CallNode->FindPin(TEXT("Descriptor"));
			Schema->TryCreateConnection(ReturnValuePin, DesciptorPin);

			UEdGraphPin* PropertyIndexPin = CallNode->FindPin(TEXT("PropertyIndex"));
			PropertyIndexPin->DefaultValue = FString::FromInt(PropertyIndex);

			UEdGraphPin* GetNodeOutputPin = GetNode->GetValuePin();
			UEdGraphPin* ArrayInputPin = CallNode->FindPin(TEXT("Buffer"));
			Schema->TryCreateConnection(GetNodeOutputPin, ArrayInputPin);

			//Wire the execution flow pins.
			UEdGraphPin* PreviousPin = previousNode->GetThenPin();
			UEdGraphPin* NextPin  = CallNode->GetExecPin();
			Schema->TryCreateConnection(PreviousPin, NextPin);

			previousNode = CallNode;
			PropertyIndex++;
		}
	}


	UK2Node_CallFunction* InvokeNode = NewObject<UK2Node_CallFunction>(FuncGraph);
	FuncGraph->AddNode(InvokeNode, false, false);

	InvokeNode->SetFromFunction(InvokeFunction);
	InvokeNode->AllocateDefaultPins();
	InvokeNode->NodePosX = NewNodePos.X;
	InvokeNode->NodePosY = NewNodePos.Y;

	if (UEdGraphPin* EventNamePin = InvokeNode->FindPin(TEXT("EventName")))
	{
		EventNamePin->DefaultValue = *ReferencedEventName.ToString();
	}

	if (UEdGraphPin* IdPin = InvokeNode->FindPin(TEXT("RPCId")))
	{
		//Generate deterministic id from the event name.
		FString NameString = FunctionName.ToString();
		uint32 HashCRC = FCrc::StrCrc32(*NameString);
		int32 HashInt32 = static_cast<int32>(HashCRC & 0x7FFFFFFF);
		
		IdPin->DefaultValue = FString::FromInt(HashInt32);
	}
	
	if (UEdGraphPin* TargetPin = InvokeNode->FindPin(TEXT("Target")))
	{
		const UEnum* EnumClass = StaticEnum<EFusionRPCTarget>();
		if (EnumClass)
		{
			FString EnumString = EnumClass->GetNameStringByValue(static_cast<int64>(RPCTarget));  
			Schema->SetPinDefaultValueAtConstruction(TargetPin, EnumString);
		}
	}

	//Connect the buffer variable as input to our native invoke.
	UEdGraphPin* GetNodeOutputPin = GetNode->GetValuePin();
	UEdGraphPin* ArrayInputPin = InvokeNode->FindPin(TEXT("Buffer"));
	Schema->TryCreateConnection(GetNodeOutputPin, ArrayInputPin);
	


	//Connect current blueprint self as source of RPC.
	UEdGraphPin* SourceInputPin = InvokeNode->FindPinChecked(TEXT("Source"));
	Schema->TryCreateConnection(SelfOutputPin, SourceInputPin);

	//Final connection/pin made so the Invoke RPC call is made using our ParamBuffer as input.
	UEdGraphPin* ThenPin = previousNode->GetThenPin();
	UEdGraphPin* ExecPin  = InvokeNode->GetExecPin();
	Schema->TryCreateConnection(ThenPin, ExecPin);
	
	

	//Haxx, for some reason the blueprint editor not properly show our local variable in the list unless we delay the update here.
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([Blueprint, FuncGraph, BufferVarName, GeneratedFunction](float DeltaTime) -> bool
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			
			if (!Blueprint)
				return false;
			
			if (auto* OpenBP = static_cast<FBlueprintEditor*>(
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, false)))
			{
				// Refresh blueprint panel if editor is open
				OpenBP->RefreshMyBlueprint();
			}

			return false; // return false here, just run this once.
		}),
		0.0f 
	);
	return FuncGraph;
}

void UFusionRPCFunctionNode::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UFusionRPCFunctionNode::AllocateDefaultPins()
{
	//Ensure skeleton class has a function reference to work with.
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(this);
	if (Blueprint && !ReferencedEventName.IsNone())
	{
		const FName FunctionName(*FString::Printf(TEXT("%s_Call"), *ReferencedEventName.ToString()));
		FunctionReference.SetExternalMember(FunctionName, Blueprint->SkeletonGeneratedClass);
	}

	Super::AllocateDefaultPins();
}

bool UFusionRPCFunctionNode::ShouldShowNodeProperties() const
{
	//In order for out details drawer to work this must be enabled.
	return true;
}

TSharedRef<IDetailCustomization> FFusionRPCFunctionNodeDetails::MakeInstance()
{
	return MakeShareable(new FFusionRPCFunctionNodeDetails);
}

void FFusionRPCFunctionNodeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	if (ObjectsBeingCustomized.Num() == 0)
	{
		return;
	}
	
	for (TWeakObjectPtr<UObject> Obj : ObjectsBeingCustomized)
	{
		if (UFusionRPCFunctionNode* Node = Cast<UFusionRPCFunctionNode>(Obj.Get()))
		{
			// Try to resolve owning Blueprint
			UBlueprint* Blueprint = Cast<UBlueprint>(Node->GetOuter());
			if (!Blueprint)
			{
				Blueprint = GetBlueprintFromNode(Node);
			}

			if (!Blueprint)
			{
				continue;
			}

			Options.Empty();
			for (UEdGraph* Graph : Blueprint->UbergraphPages)
			{
				for (UEdGraphNode* CurrentNode : Graph->Nodes)
				{
					if (UK2Node_CustomEvent* EventNode = Cast<UK2Node_CustomEvent>(CurrentNode))
					{
						Options.Add(MakeShared<FName>(EventNode->CustomFunctionName));
					}
				}
			}

			TSharedRef<IPropertyHandle> ReferencedEventNameHandle = DetailBuilder.GetProperty(
				GET_MEMBER_NAME_CHECKED(UFusionRPCFunctionNode, ReferencedEventName),
				UFusionRPCFunctionNode::StaticClass()
			);

			TSharedRef<IPropertyHandle> RPCTargetHandle = DetailBuilder.GetProperty(
				GET_MEMBER_NAME_CHECKED(UFusionRPCFunctionNode, RPCTarget),
				UFusionRPCFunctionNode::StaticClass()
			);

			IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Fusion RPC");

			if (Options.Num() == 0)
			{
				continue;
			}

			Category.AddCustomRow(FText::FromString("Pin Type"))
			.ValueContent()
			[
				SNew(SButton)
				.Text(FText::FromString("Refresh Pins"))
				.OnClicked_Lambda([Node, &DetailBuilder]() -> FReply {
					if (Node)
					{
						Node->Rebuild();

						DetailBuilder.ForceRefreshDetails();

						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
			];
			
			Category.AddProperty(ReferencedEventNameHandle)
				.CustomWidget()
				.NameContent()
				[
					ReferencedEventNameHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				.MinDesiredWidth(200)
				[
					SNew(SComboBox<TSharedPtr<FName>>)
					.OptionsSource(&Options)
					.OnGenerateWidget_Lambda([](TSharedPtr<FName> InItem)
					{
						return SNew(STextBlock).Text(FText::FromName(*InItem));
					})
					.OnSelectionChanged_Lambda([Node, ReferencedEventNameHandle](TSharedPtr<FName> NewValue, ESelectInfo::Type)
					{
						if (NewValue.IsValid())
						{
							Node->SetEventName(*NewValue);
						}
					})
					.InitiallySelectedItem([&]()
					{
						FName CurrentValue;
						ReferencedEventNameHandle->GetValue(CurrentValue);
						for (auto& Option : Options)
						{
							if (*Option == CurrentValue)
							{
								return Option;
							}
						}
						return TSharedPtr<FName>(nullptr);
					}())
					[
						SNew(STextBlock)
						.Text_Lambda([ReferencedEventNameHandle]()
						{
							FName CurrentValue;
							ReferencedEventNameHandle->GetValue(CurrentValue);
							return CurrentValue.IsNone()
								? FText::FromString("Select RPC Node")
								: FText::FromName(CurrentValue);
						})
					]
				];

			Category.AddProperty(RPCTargetHandle);

			RPCTargetHandle->SetOnPropertyValueChanged(
			FSimpleDelegate::CreateLambda([RPCTargetHandle, Node]()
			{
				uint8 ValueAsByte = 0;
				if (RPCTargetHandle->GetValue(ValueAsByte) == FPropertyAccess::Success)
				{
					Node->Rebuild();
				}
			}));
		}
	}
}

UBlueprint* FFusionRPCFunctionNodeDetails::GetBlueprintFromNode(UObject* Object)
{
	if (!Object)
	{
		return nullptr;
	}

	// Case 1: already a node
	if (UK2Node* Node = Cast<UK2Node>(Object))
	{
		if (UEdGraph* Graph = Node->GetGraph())
		{
			return Graph->GetTypedOuter<UBlueprint>();
		}
	}

	// Case 2: it *is* a graph
	if (UEdGraph* Graph = Cast<UEdGraph>(Object))
	{
		return Graph->GetTypedOuter<UBlueprint>();
	}

	// Case 3: maybe directly a Blueprint (rare)
	return Cast<UBlueprint>(Object);
}

#undef LOCTEXT_NAMESPACE
