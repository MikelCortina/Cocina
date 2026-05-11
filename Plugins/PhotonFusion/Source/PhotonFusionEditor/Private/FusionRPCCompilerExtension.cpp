// Copyright 2026 Exit Games GmbH. All Rights Reserved.


#include "FusionRPCCompilerExtension.h"

#include "FusionHelpers.h"
#include "FusionEditor.h"
#include "K2Node_CallFunction.h"  // <-- add this

void UFusionRPCCompilerExtension::ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext,
                                                     const FBlueprintCompiledData& Data)
{
	UBlueprint* BP = CompilationContext.Blueprint;
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
			{
				UFunction* TargetFunction = CallNode->GetTargetFunction();

				if (TargetFunction)
				{
					// Check if this is an RPC
					if (TargetFunction->HasAnyFunctionFlags(FUNC_Net))
					{
						UE_LOG(LogTemp, Log, TEXT("Function %s is an RPC"), *TargetFunction->GetName());

						if (TargetFunction->HasAnyFunctionFlags(FUNC_NetReliable))
						{
							UE_LOG(LogTemp, Log, TEXT("   - Reliable"));
						}
						if (TargetFunction->HasAnyFunctionFlags(FUNC_NetMulticast))
						{
							UE_LOG(LogTemp, Log, TEXT("   - Multicast"));

							// // Insert your own function call *before* this RPC call
							// UK2Node_CallFunction* ExtraNode = CompilationContext.SpawnIntermediateNode<UK2Node_CallFunction>(CallNode, Graph);
							// ExtraNode->SetFromFunction(UFusionHelpers::StaticClass()->FindFunctionByName(TEXT("InvokeCustomRPC")));
							// ExtraNode->AllocateDefaultPins();
							//
							// // Wire it into the execution flow
							// CompilationContext.MovePinLinksToIntermediate(*CallNode->GetExecPin(), *ExtraNode->GetExecPin());
							// CompilationContext.GetSchema()->TryCreateConnection(ExtraNode->GetThenPin(), CallNode->GetExecPin());
						}
						if (TargetFunction->HasAnyFunctionFlags(FUNC_NetClient))
						{
							UE_LOG(LogTemp, Log, TEXT("   - Client"));
						}
						if (TargetFunction->HasAnyFunctionFlags(FUNC_NetServer))
						{
							UE_LOG(LogTemp, Log, TEXT("   - Server"));
						}
					}
				}
			}
		}
	}
}
