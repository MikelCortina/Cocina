// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FusionSpatialHashInterestComponent.generated.h"

/**
 * Opaque wrapper around a spatial hash area interest key.
 * Blueprint can pass this value around without being able to read or modify the raw uint64.
 */
USTRUCT(BlueprintType)
struct PHOTONFUSION_API FFusionInterestKey
{
	GENERATED_BODY()

	FFusionInterestKey() = default;
	explicit FFusionInterestKey(uint64 InKey) : Key(InKey) {}

	uint64 GetKey() const { return Key; }
	bool IsGlobal() const { return Key == 0; }

private:
	// Not a UPROPERTY — intentionally hidden from Blueprint
	uint64 Key = 0;
};

/**
 * Example area-of-interest / interest management component using a 3D spatial hash.
 *
 * Add this component to a PlayerController (or Pawn) to enable position-based interest management:
 *   - Subscriber side: each frame the component subscribes this client to the spatial hash cells
 *     surrounding the actor's position (or Pawn position when on a PlayerController).
 *   - Publisher side: each frame the component assigns a spatial hash area interest key to every
 *     locally-owned networked root actor, telling the server which spatial bucket it belongs to.
 *
 * The server uses these keys to cull object updates — clients only receive updates for objects whose
 * area key matches one of the client's subscribed keys, plus any objects with key 0 (global).
 *
 * Set bEnabled = false to disable interest management; all objects fall back to global interest
 * and are visible to all clients regardless of position.
 *
 * Key encoding: the 64-bit area key packs three 21-bit cell coordinates (X, Y, Z) as:
 *   Key = (CellZ << 42) | (CellY << 21) | CellX
 * where each cell coordinate is clamped to [0, WorldHalfExtent*2). Key=0 is reserved for global
 * interest and is remapped to 1 for the exact world origin cell.
 *
 * WorldHalfExtent and CellSize must match the server's AOI configuration.
 */
UCLASS(Blueprintable, ClassGroup=(Fusion), meta=(BlueprintSpawnableComponent))
class PHOTONFUSION_API UFusionSpatialHashInterestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFusionSpatialHashInterestComponent();

	/** Master toggle. When false, all area subscriptions are cleared and owned objects use global interest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fusion|Interest")
	bool bEnabled = true;

	/**
	 * Size of each spatial hash cell in Unreal units (cm).
	 * Larger cells mean fewer total cells and broader subscription regions.
	 * Must match the server's cell size configuration.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fusion|Interest", meta = (ClampMin = "100.0"))
	float CellSize = 3200.0f;

	/**
	 * World half-extent: number of cells from the origin in each +/- direction per axis.
	 * Total world coverage per axis = WorldHalfExtent * CellSize * 2.
	 * Must match the server's world size configuration.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fusion|Interest")
	FIntVector WorldHalfExtent = FIntVector(1024, 1024, 1024);

	/**
	 * Subscription radius: how many neighboring cells to subscribe to in each axis direction.
	 * (1,1,1) = 3x3x3 = 27 cells; (2,0,2) = 5x1x5 = 25 cells (flat top-down game).
	 * Higher values increase the visible range but also the number of received object updates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fusion|Interest")
	FIntVector SubscribeRadius = FIntVector(1, 1, 1);

	/** Compute the spatial hash key for a world-space position using the current parameters. */
	UFUNCTION(BlueprintPure, Category = "Fusion|Interest")
	FFusionInterestKey ComputeKeyForPosition(const FVector& Position) const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Returns the position used to center the subscription region.
	 *  When the owning actor is a PlayerController the Pawn's location is used instead. */
	bool GetSubscriberPosition(FVector& OutPosition) const;
};
