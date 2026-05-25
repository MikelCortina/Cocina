#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RandomPlacementManager.generated.h"

UCLASS()
class COCINA_API ARandomPlacementManager : public AActor
{
	GENERATED_BODY()

public:
	ARandomPlacementManager();

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Seed();

	void ApplyPlacement();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random Placement")
	TArray<TObjectPtr<AActor>> ObjectsToPlace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random Placement")
	TArray<TObjectPtr<AActor>> Points;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random Placement")
	bool bUniquePoints = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random Placement")
	bool bCopyRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random Placement")
	bool bCopyScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random Placement")
	bool bRandomYawIfNoRotation = false;

	UPROPERTY(ReplicatedUsing = OnRep_Seed, EditAnywhere, BlueprintReadWrite, Category = "Random Placement")
	int32 Seed = 12345;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};