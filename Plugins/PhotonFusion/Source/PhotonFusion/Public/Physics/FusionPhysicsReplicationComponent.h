// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Physics/FusionPhysicsReplication.h"
#include "FusionPhysicsReplicationComponent.generated.h"

class USphereComponent;

UCLASS(Blueprintable, ClassGroup=(Fusion), meta=(BlueprintSpawnableComponent))
class PHOTONFUSION_API UFusionPhysicsReplicationComponent : public UActorComponent
{
	GENERATED_BODY()

	int32 TeleportKey = 0;

public:
	UFusionPhysicsReplicationComponent();

	UPROPERTY(Replicated)
	FFusionBodyState BodyState;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	

	UFUNCTION()
	void OnHitCallback(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
public:

	void OnTransformUpdated(USceneComponent* InRootComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	TObjectPtr<UPrimitiveComponent> Primitive{nullptr};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fusion")
	bool OverrideDefaultProjection{false};


	bool ShouldPredictPhysics(const FFusionReplicatedPhysicsTarget& Target, FBodyInstance*& BodyInstance);

	UFUNCTION(BlueprintNativeEvent, Category="Fusion")
	bool UpdatePhysicsPrediction(UPARAM(ref) FBodyInstance& BodyInstance);
	virtual bool UpdatePhysicsPrediction_Implementation(FBodyInstance& BodyInstance);
	
	virtual bool OnComponentProject(float DeltaSeconds, double TimeDifference, FBodyInstance* BodyInstance, FFusionReplicatedPhysicsTarget& ReplicatedTarget);

	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const override;
	
	virtual void PostRepNotifies() override;
};
