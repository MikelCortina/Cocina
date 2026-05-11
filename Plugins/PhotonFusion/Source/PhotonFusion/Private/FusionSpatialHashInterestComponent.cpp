// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "FusionSpatialHashInterestComponent.h"

#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

UFusionSpatialHashInterestComponent::UFusionSpatialHashInterestComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

FFusionInterestKey UFusionSpatialHashInterestComponent::ComputeKeyForPosition(const FVector& Position) const
{
	const int32 CellX = FMath::Clamp(FMath::FloorToInt32(Position.X / CellSize) + WorldHalfExtent.X, 0, WorldHalfExtent.X * 2 - 1);
	const int32 CellY = FMath::Clamp(FMath::FloorToInt32(Position.Y / CellSize) + WorldHalfExtent.Y, 0, WorldHalfExtent.Y * 2 - 1);
	const int32 CellZ = FMath::Clamp(FMath::FloorToInt32(Position.Z / CellSize) + WorldHalfExtent.Z, 0, WorldHalfExtent.Z * 2 - 1);

	// Pack into uint64: 21 bits per axis (supports up to 2097152 cells per axis)
	uint64 Key = (static_cast<uint64>(CellZ) << 42)
	           | (static_cast<uint64>(CellY) << 21)
	           | static_cast<uint64>(CellX);

	// Key=0 is reserved for global interest; remap the origin cell to 1
	if (Key == 0) Key = 1;

	return FFusionInterestKey(Key);
}

bool UFusionSpatialHashInterestComponent::GetSubscriberPosition(FVector& OutPosition) const
{
	const AActor* Owner = GetOwner();
	if (!Owner) return false;

	// When attached to a PlayerController use the possessed Pawn's location
	if (const APlayerController* PC = Cast<APlayerController>(Owner))
	{
		const APawn* Pawn = PC->GetPawn();
		if (!Pawn) return false;
		OutPosition = Pawn->GetActorLocation();
		return true;
	}

	OutPosition = Owner->GetActorLocation();
	return true;
}

void UFusionSpatialHashInterestComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UFusionSpatialHashInterestComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	// Clear area subscriptions so the client falls back to global interest
	const UWorld* World = GetWorld();
	if (!World) { Super::EndPlay(Reason); return; }

	const UGameInstance* GI = World->GetGameInstance();
	if (!GI) { Super::EndPlay(Reason); return; }

	if (const UFusionOnlineSubsystem* Subsystem = GI->GetSubsystem<UFusionOnlineSubsystem>())
	{
		if (UFusionClient* FusionClient = Subsystem->GetFusionClient())
		{
			if (FusionCore::Client* Client = FusionClient->GetClient())
			{
				Client->GetLocalInterestKeys().ClearAreaKeys();
				//Client->ClearAreaKeys();
			}
			FusionClient->UpdateOwnedActorAreaInterestKeys([](const AActor*) -> uint64 { return 0; });
		}
	}

	Super::EndPlay(Reason);
}

void UFusionSpatialHashInterestComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UWorld* World = GetWorld();
	if (!World) return;

	const UGameInstance* GI = World->GetGameInstance();
	if (!GI) return;

	const UFusionOnlineSubsystem* Subsystem = GI->GetSubsystem<UFusionOnlineSubsystem>();
	if (!Subsystem || !Subsystem->IsInRoom()) return;

	UFusionClient* FusionClient = Subsystem->GetFusionClient();
	if (!FusionClient) return;

	FusionCore::Client* Client = FusionClient->GetClient();
	if (!Client) return;

	if (!bEnabled)
	{
		Client->GetLocalInterestKeys().ClearAreaKeys();
		FusionClient->UpdateOwnedActorAreaInterestKeys([](const AActor*) -> uint64 { return 0; });
		return;
	}

	// --- Subscriber side: subscribe to the neighborhood of cells around our position ---
	FVector Position;
	if (GetSubscriberPosition(Position))
	{
		const int32 CenterX = FMath::Clamp(FMath::FloorToInt32(Position.X / CellSize) + WorldHalfExtent.X, 0, WorldHalfExtent.X * 2 - 1);
		const int32 CenterY = FMath::Clamp(FMath::FloorToInt32(Position.Y / CellSize) + WorldHalfExtent.Y, 0, WorldHalfExtent.Y * 2 - 1);
		const int32 CenterZ = FMath::Clamp(FMath::FloorToInt32(Position.Z / CellSize) + WorldHalfExtent.Z, 0, WorldHalfExtent.Z * 2 - 1);

		std::vector<std::tuple<uint64_t, uint8_t>> Keys;
		Keys.reserve(
			static_cast<size_t>(2 * SubscribeRadius.X + 1) *
			static_cast<size_t>(2 * SubscribeRadius.Y + 1) *
			static_cast<size_t>(2 * SubscribeRadius.Z + 1));

		for (int32 dz = -SubscribeRadius.Z; dz <= SubscribeRadius.Z; ++dz)
		for (int32 dy = -SubscribeRadius.Y; dy <= SubscribeRadius.Y; ++dy)
		for (int32 dx = -SubscribeRadius.X; dx <= SubscribeRadius.X; ++dx)
		{
			const int32 cx = FMath::Clamp(CenterX + dx, 0, WorldHalfExtent.X * 2 - 1);
			const int32 cy = FMath::Clamp(CenterY + dy, 0, WorldHalfExtent.Y * 2 - 1);
			const int32 cz = FMath::Clamp(CenterZ + dz, 0, WorldHalfExtent.Z * 2 - 1);

			uint64 Key = (static_cast<uint64>(cz) << 42)
			           | (static_cast<uint64>(cy) << 21)
			           | static_cast<uint64>(cx);
			if (Key == 0) Key = 1;

			Keys.emplace_back(Key, uint8_t(0));
		}
		Client->GetLocalInterestKeys().SetAreaKeys(Keys);
	}

	// --- Publisher side: assign area interest keys to all locally-owned root actors ---
	FusionClient->UpdateOwnedActorAreaInterestKeys([this](const AActor* Actor) -> uint64
	{
		return ComputeKeyForPosition(Actor->GetActorLocation()).GetKey();
	});
}
