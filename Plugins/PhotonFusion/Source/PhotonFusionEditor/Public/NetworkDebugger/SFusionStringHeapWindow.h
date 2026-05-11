// Copyright Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

class UFusionClient;
class UWorld;

/** Data for a single heap segment (allocated or free). */
struct FHeapSegment
{
	uint32 Offset = 0;
	uint32 Size = 0;
	bool bAlive = false;
	uint32 EntryId = 0; // Only valid when bAlive
	FString ResolvedString; // Preview of the string content
};

/** Snapshot of a single object's string heap state. */
struct FStringHeapSnapshot
{
	uint64 PackedObjectId = 0;
	FString ObjectLabel;
	uint32 HeapSize = 0;
	uint32 EntryCount = 0;
	uint32 AliveCount = 0;
	uint32 FreeSegmentCount = 0;
	uint32 UsedBytes = 0;
	uint32 FreeBytes = 0;
	TArray<FHeapSegment> Segments; // Sorted by offset, interleaved alive + free
};

/** Item for the object list. */
struct FStringHeapObjectItem
{
	uint64 PackedId = 0;
	uint32 Origin = 0;
	uint32 Counter = 0;
	FString Label;
	uint32 HeapSize = 0;
	uint32 UsedBytes = 0;
	uint32 EntryCount = 0;
};

/**
 * Custom Slate leaf widget that draws the string heap as a linear segment bar.
 * Allocated segments are colored, free/fragmented gaps are gray.
 */
class SFusionStringHeapBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFusionStringHeapBar) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetSnapshot(const FStringHeapSnapshot& InSnapshot);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	FStringHeapSnapshot Snapshot;
	static FLinearColor GetSegmentColor(uint32 Index);
};

/**
 * Main window widget for the Fusion String Heap debugger.
 * Left: object list. Right: heap visualization bar + stats.
 */
class SFusionStringHeapWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFusionStringHeapWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetTargetWorld(UWorld* InWorld) { TargetWorld = InWorld; }

private:
	EActiveTimerReturnType TickCollect(double InCurrentTime, float InDeltaTime);
	void RebuildObjectList();
	void OnObjectSelected(TSharedPtr<FStringHeapObjectItem> Item, ESelectInfo::Type SelectInfo);
	TSharedRef<ITableRow> GenerateObjectRow(TSharedPtr<FStringHeapObjectItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void CollectSnapshot(UFusionClient* FusionClient, uint64 PackedId);
	UFusionClient* FindFusionClient() const;

	TWeakObjectPtr<UWorld> TargetWorld;
	TArray<TSharedPtr<FStringHeapObjectItem>> ObjectItems;
	TSharedPtr<SListView<TSharedPtr<FStringHeapObjectItem>>> ObjectListView;
	TSharedPtr<SFusionStringHeapBar> HeapBar;
	TSharedPtr<STextBlock> StatsText;
	TSharedPtr<STextBlock> StatusText;

	uint64 SelectedPackedId = 0;
	FStringHeapSnapshot CurrentSnapshot;
};
