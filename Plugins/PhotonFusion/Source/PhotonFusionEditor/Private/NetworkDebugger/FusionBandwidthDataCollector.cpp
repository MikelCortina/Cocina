// Copyright Exit Games GmbH. All Rights Reserved.

#include "NetworkDebugger/FusionBandwidthDataCollector.h"

#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionObjectActorPair.h"
#include "DrawDebugHelpers.h"
#include "FusionUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Fusion/Client.h"
#include "Fusion/Types.h"

FFusionBandwidthDataCollector::FFusionBandwidthDataCollector()
{
}

FFusionBandwidthDataCollector::~FFusionBandwidthDataCollector()
{
}

DECLARE_STATS_GROUP(TEXT("FusionDebugTools"), STATGROUP_FusionDebugTools, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("BandwidthCollector Tick"), STAT_BandwidthCollectorTick, STATGROUP_FusionDebugTools);

TStatId FFusionBandwidthDataCollector::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FFusionBandwidthDataCollector, STATGROUP_FusionDebugTools);
}

UFusionClient* FFusionBandwidthDataCollector::FindActiveFusionClient() const
{
	if (!GEngine)
	{
		return nullptr;
	}

	// When bound to a specific world, only check that world
	if (TargetWorld.IsValid())
	{
		UWorld* World = TargetWorld.Get();
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance)
		{
			return nullptr;
		}

		UFusionOnlineSubsystem* Subsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
		if (Subsystem && Subsystem->GFusionClient)
		{
			return Subsystem->GFusionClient;
		}

		return nullptr;
	}

	// Fallback: iterate all world contexts
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
		{
			continue;
		}

		UGameInstance* GameInstance = World->GetGameInstance();
		if (!GameInstance)
		{
			continue;
		}

		UFusionOnlineSubsystem* Subsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>();
		if (Subsystem && Subsystem->GFusionClient)
		{
			return Subsystem->GFusionClient;
		}
	}

	return nullptr;
}

void FFusionBandwidthDataCollector::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_BandwidthCollectorTick);

	UFusionClient* FusionClient = FindActiveFusionClient();
	if (!FusionClient || !FusionClient->GetClient())
	{
		bConnected = false;
		for (auto& Pair : ObjectData)
		{
			Pair.Value.bIsAlive = false;
		}
		OnDataUpdated.Broadcast();
		return;
	}

	UWorld* World = FusionClient->GetCurrentWorld();
	if (!World)
	{
		return;
	}
	
	bConnected = true;

	if (IsRecording())
	{
		TotalBytesSent = 0;
		TotalBytesReceived = 0;
		
		for (auto& Pair : ObjectData)
		{
			Pair.Value.bIsAlive = false;
		}

		auto UpdatePropertiesFromStates = [](TArray<FPropertyBandwidthData>& Out, const TArray<FPropertyWordState>& States, int32 RecvBytes) -> void
		{
			if (Out.Num() != States.Num())
			{
				Out.SetNum(States.Num());
			}
			for (int32 i = 0; i < States.Num(); ++i)
			{
				const FPropertyWordState& S = States[i];
				FPropertyBandwidthData& P = Out[i];
				if (P.Name.IsEmpty() || P.WordCount != S.WordCount)
				{
					FString Label = S.EngineProperty ? S.EngineProperty->GetName() : FString::Printf(TEXT("Property %d"), i);

					if (S.SubPropertyStates.Num() > 0)
					{
						if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(S.EngineProperty))
						{
							const int32 ElementCount = S.SubPropertyStates.Num();
							int32 PropsPerElement = 0;
							if (const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
							{
								for (TFieldIterator<FProperty> It(InnerStruct->Struct); It; ++It)
								{
									++PropsPerElement;
								}
							}
							else
							{
								PropsPerElement = 1;
							}
							const int32 PreallocElements = PropsPerElement > 0 ? ElementCount / PropsPerElement : ElementCount;
							Label = FString::Printf(TEXT("%s (Array, %d elements)"), *Label, PreallocElements);
						}
						else if (CastField<FStructProperty>(S.EngineProperty))
						{
							Label = FString::Printf(TEXT("%s (Struct, %d members)"), *Label, S.SubPropertyStates.Num());
						}
					}

					P.Name = Label;
					P.WordCount = S.WordCount;
				}

				if (RecvBytes == -1)
				{
					P.ChangedWordCount = S.ChangedWordCount;
				}
				else
				{
					//if no new state has been received, we need a staleness check here.
					P.ChangedWordCount = RecvBytes > 0 ? S.ChangedWordCount : 0;
				}
			}
		};

		auto& RootObjects = FusionClient->GetClient()->AllRootObjects();

		for (auto& [ObjectId, RootObject] : RootObjects)
		{
			const uint64 PackedId = static_cast<uint64>(ObjectId);

			FObjectBandwidthData* Data = ObjectData.Find(PackedId);
			if (!Data)
			{
				FObjectBandwidthData NewData;
				NewData.PackedId = PackedId;
				NewData.Origin = ObjectId.Origin;
				NewData.Counter = ObjectId.Counter;
				NewData.GraphColor = AssignColor();
				NewData.bIsSelected = true;

				FFusionObjectActorPair ActorPair = FusionClient->FindObjectPair(ObjectId);
				if (ActorPair.EngineObject)
				{
					NewData.ObjectLabel = ActorPair.EngineObject->GetName();
				}
				else
				{
					NewData.ObjectLabel = FString::Printf(TEXT("(%u:%u)"), ObjectId.Origin, ObjectId.Counter);
				}

				Data = &ObjectData.Add(PackedId, MoveTemp(NewData));
			}

			Data->bIsAlive = true;
			Data->bIsSending = FusionClient->GetClient()->CanModify(RootObject);
			
			if (Data->bIsSending)
			{
				size_t SendBytes = RootObject->GetSendReport().TotalAvg;

				// Include sub-object bytes in the root total
				if (FusionClient->GetClient()->HasSubObjects(RootObject))
				{
					for (const auto& SubId : FusionClient->GetClient()->GetSubObject(RootObject))
					{
						if (const auto* SubObj = FusionClient->GetClient()->FindObject(SubId))
						{
							SendBytes += SubObj->GetSendReport().TotalAvg;
						}
					}
				}

				Data->PushSendSample(SendBytes);
				TotalBytesSent += SendBytes;
				
				FFusionObjectActorPair ActorPair = FusionClient->FindObjectPair(ObjectId);
				if (Data->ObjectLabel.StartsWith(TEXT("(")) && ActorPair.EngineObject)
				{
					Data->ObjectLabel = ActorPair.EngineObject->GetName();
				}
				UpdatePropertiesFromStates(Data->Properties, ActorPair.PropertyStates, -1);
			}
			else
			{
				constexpr int32 HoldDuration = 10;
				
				// EMA averages decay slowly, so a stale recv report keeps reporting bytes long
				// after the stream went quiet. Treat anything older than this as zero.
				constexpr double StaleRecvThresholdSeconds = 0.1;
				
				auto FreshRecvBytes = [StaleRecvThresholdSeconds](const auto* Obj) -> size_t
				{
					const auto Report = Obj->GetRecvReport();
					const double TimeSinceUpdate =  std::chrono::duration<double>(std::chrono::steady_clock::now() - Report.LastUpdatedTime).count();
					return TimeSinceUpdate > StaleRecvThresholdSeconds ? 0 : static_cast<size_t>(Report.TotalAvg);
				};

				// Sum root + sub-object received bytes so the collapsed view reflects total traffic
				size_t RecvBytes = FreshRecvBytes(RootObject);
				size_t SubObjectRecvBytes = 0;
				if (FusionClient->GetClient()->HasSubObjects(RootObject))
				{
					for (const auto& SubId : FusionClient->GetClient()->GetSubObject(RootObject))
					{
						if (const auto* SubObj = FusionClient->GetClient()->FindObject(SubId))
						{
							SubObjectRecvBytes += FreshRecvBytes(SubObj);
						}
					}
				}

				if (RecvBytes > 0 || SubObjectRecvBytes > 0)
				{
					Data->HeldReceiveBytes = RecvBytes + SubObjectRecvBytes;
					Data->ReceiveHoldFrames = HoldDuration;
				}
				else if (Data->ReceiveHoldFrames > 0)
				{
					--Data->ReceiveHoldFrames;
				}
				else
				{
					Data->HeldReceiveBytes = 0;
				}
				Data->PushReceiveSample(Data->HeldReceiveBytes);

				TotalBytesReceived += Data->HeldReceiveBytes;

				FFusionObjectActorPair ActorPair = FusionClient->FindObjectPair(ObjectId);
				if (Data->ObjectLabel.StartsWith(TEXT("(")) && ActorPair.EngineObject)
				{
					Data->ObjectLabel = ActorPair.EngineObject->GetName();
				}
				UpdatePropertiesFromStates(Data->Properties, ActorPair.PropertyStates, RecvBytes);
			}

			// Mark all sub-objects as not alive before refresh
			for (auto& SubPair : Data->SubObjects)
			{
				SubPair.Value.bIsAlive = false;
			}

			// Collect sub-object byte data
			if (FusionClient->GetClient()->HasSubObjects(RootObject))
			{
				const auto& SubIds = FusionClient->GetClient()->GetSubObject(RootObject);
				for (const auto& SubId : SubIds)
				{
					const auto* SubObj = FusionClient->GetClient()->FindObject(SubId);
					if (!SubObj)
					{
						continue;
					}

					const uint64 SubPackedId = static_cast<uint64>(SubId);
					FSubObjectBandwidthData* SubData = Data->SubObjects.Find(SubPackedId);
					if (!SubData)
					{
						FSubObjectBandwidthData NewSubData;
						NewSubData.PackedId = SubPackedId;
						NewSubData.Origin = SubId.Origin;
						NewSubData.Counter = SubId.Counter;
						NewSubData.GraphColor = AssignColor();

						FFusionObjectActorPair SubPair = FusionClient->FindObjectPair(SubId);
						if (SubPair.EngineObject)
						{
							NewSubData.Label = SubPair.EngineObject->GetName();
						}
						else
						{
							NewSubData.Label = FString::Printf(TEXT("(%u:%u)"), SubId.Origin, SubId.Counter);
						}

						SubData = &Data->SubObjects.Add(SubPackedId, MoveTemp(NewSubData));
					}

					SubData->bIsAlive = true;
					
					if (Data->bIsSending)
					{
						SubData->PushSample(SubObj->GetSendReport().TotalAvg);
						
						FFusionObjectActorPair SubPair = FusionClient->FindObjectPair(SubId);
						if (SubData->Label.StartsWith(TEXT("(")) && SubPair.EngineObject)
						{
							SubData->Label = SubPair.EngineObject->GetName();
						}
						UpdatePropertiesFromStates(SubData->Properties, SubPair.PropertyStates, -1);
					}
					else
					{
						constexpr int32 HoldDuration = 10;
						constexpr double StaleRecvThresholdSeconds = 0.1;
						const auto SubRecvReport = SubObj->GetRecvReport();
						const double TimeSinceSubObjectUpdate =  std::chrono::duration<double>(std::chrono::steady_clock::now() - SubRecvReport.LastUpdatedTime).count();
						
						const size_t RecvBytes = TimeSinceSubObjectUpdate > StaleRecvThresholdSeconds
							? 0
							: static_cast<size_t>(SubRecvReport.TotalAvg);
						
						if (RecvBytes > 0)
						{
							SubData->HeldBytes = RecvBytes;
							SubData->HoldFrames = HoldDuration;
						}
						else if (SubData->HoldFrames > 0)
						{
							--SubData->HoldFrames;
						}
						else
						{
							SubData->HeldBytes = 0;
						}
						SubData->PushSample(SubData->HeldBytes);
						
						FFusionObjectActorPair SubPair = FusionClient->FindObjectPair(SubId);
						if (SubData->Label.StartsWith(TEXT("(")) && SubPair.EngineObject)
						{
							SubData->Label = SubPair.EngineObject->GetName();
						}
						UpdatePropertiesFromStates(SubData->Properties, SubPair.PropertyStates, RecvBytes);
					}
				}
			}

			// Remove dead sub-objects
			for (auto It = Data->SubObjects.CreateIterator(); It; ++It)
			{
				auto Entry = *It;
				FusionCore::ObjectId SubObjectObjectId(Entry.Value.PackedId);
						
				if (!FusionClient->GetClient()->FindObject(SubObjectObjectId))
				{
					It.RemoveCurrent();
				}
				else if (!It.Value().bIsAlive)
				{
					It.RemoveCurrent();
				}
			}
		}

		// Remove dead root objects
		for (auto It = ObjectData.CreateIterator(); It; ++It)
		{
			auto Entry = *It;
			FusionCore::ObjectId ObjectId(Entry.Value.PackedId);
			
			if (!FusionClient->GetClient()->FindObject(ObjectId))
			{
				It.RemoveCurrent();
			}
			else if (!It.Value().bIsAlive)
			{
				It.RemoveCurrent();
			}
		}
	}

	// Draw debug highlight for selected objects in the viewport
	for (const auto& [PackedId, Data] : ObjectData)
	{
		if (!Data.bIsSelected)
		{
			continue;
		}
		
		if (!Data.bIsAlive)
		{
			continue;
		}

		const FusionCore::ObjectId ObjectId(PackedId);
		FFusionObjectActorPair ActorPair = FusionClient->FindObjectPair(ObjectId);
		if (!ActorPair.Actor)
		{
			continue;
		}

		// Use GetActorBounds with bOnlyCollidingComponents=true and bIncludeFromChildActors=false
		// to avoid huge debug boxes caused by perception spheres, audio attenuation, camera
		// frustums, billboards, etc. on the actor.
		FVector Origin;
		FVector BoxExtent;
		ActorPair.Actor->GetActorBounds(/* bOnlyCollidingComponents */ true, Origin, BoxExtent, /* bIncludeFromChildActors */ false);

		if (BoxExtent.IsNearlyZero())
		{
			// No colliding components — fall back to a small box at actor location
			Origin = ActorPair.Actor->GetActorLocation();
			BoxExtent = FVector(50.0);
		}

		DrawDebugBox(World, Origin, BoxExtent, FQuat::Identity, Data.GraphColor.ToFColor(true), false, 0.0f, 0, 2.0f);
	}

	if (IsRecording())
	{
		OnDataUpdated.Broadcast();
	}
}

void FFusionBandwidthDataCollector::ClearAllData()
{
	ObjectData.Empty();
	ColorIndex = 0;
	TotalBytesSent = 0;
	TotalBytesReceived = 0;
}

FLinearColor FFusionBandwidthDataCollector::AssignColor()
{
	// Golden angle HSV rotation for maximally distinct colors
	constexpr float GoldenAngle = 137.508f;
	const float Hue = FMath::Fmod(ColorIndex * GoldenAngle, 360.0f);
	++ColorIndex;
	return FLinearColor::MakeFromHSV8(
		static_cast<uint8>(Hue / 360.0f * 255.0f),
		200,
		230
	);
}
