#include "RandomPlacementManager.h"
#include "Net/UnrealNetwork.h"
#include "Math/RandomStream.h"

ARandomPlacementManager::ARandomPlacementManager()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void ARandomPlacementManager::BeginPlay()
{
	Super::BeginPlay();

	ApplyPlacement();
}

void ARandomPlacementManager::OnRep_Seed()
{
	ApplyPlacement();
}

void ARandomPlacementManager::ApplyPlacement()
{
	TArray<AActor*> CleanObjects;
	for (AActor* Actor : ObjectsToPlace)
	{
		if (IsValid(Actor))
		{
			CleanObjects.Add(Actor);
		}
	}

	TArray<AActor*> CleanPoints;
	for (AActor* Actor : Points)
	{
		if (IsValid(Actor))
		{
			CleanPoints.Add(Actor);
		}
	}

	if (CleanObjects.Num() == 0 || CleanPoints.Num() == 0)
	{
		return;
	}

	FRandomStream Stream(Seed);
	TArray<AActor*> AvailablePoints = CleanPoints;

	if (bUniquePoints)
	{
		for (int32 i = AvailablePoints.Num() - 1; i > 0; --i)
		{
			const int32 SwapIndex = Stream.RandRange(0, i);
			AvailablePoints.Swap(i, SwapIndex);
		}
	}

	for (int32 i = 0; i < CleanObjects.Num(); ++i)
	{
		AActor* Object = CleanObjects[i];
		if (!IsValid(Object))
		{
			continue;
		}

		AActor* TargetPoint = nullptr;

		if (bUniquePoints)
		{
			if (i >= AvailablePoints.Num())
			{
				break;
			}
			TargetPoint = AvailablePoints[i];
		}
		else
		{
			const int32 RandomIndex = Stream.RandRange(0, CleanPoints.Num() - 1);
			TargetPoint = CleanPoints[RandomIndex];
		}

		if (!IsValid(TargetPoint))
		{
			continue;
		}

		FTransform NewTransform = Object->GetActorTransform();
		const FTransform TargetTransform = TargetPoint->GetActorTransform();

		NewTransform.SetLocation(TargetTransform.GetLocation());

		if (bCopyRotation)
		{
			NewTransform.SetRotation(TargetTransform.GetRotation());
		}
		else if (bRandomYawIfNoRotation)
		{
			const float RandomYaw = Stream.FRandRange(0.f, 360.f);
			NewTransform.SetRotation(FQuat(FRotator(0.f, RandomYaw, 0.f)));
		}

		if (bCopyScale)
		{
			NewTransform.SetScale3D(TargetTransform.GetScale3D());
		}

		Object->SetActorTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ARandomPlacementManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARandomPlacementManager, Seed);
}