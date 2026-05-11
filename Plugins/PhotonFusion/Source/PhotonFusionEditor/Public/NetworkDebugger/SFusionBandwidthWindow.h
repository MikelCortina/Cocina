// Copyright Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "FusionBandwidthDataCollector.h"

// Forward declarations
class SFusionBandwidthGraph;

/** Item used by the bandwidth tree views — represents a root object, a sub-object, or a property. */
struct FBandwidthTreeItem
{
	uint64 RootPackedId = 0;
	uint64 SubObjectPackedId = 0; // 0 = root context (or property of root)
	int32 PropertyIndex = INDEX_NONE;
	TArray<TSharedPtr<FBandwidthTreeItem>> Children;

	bool IsProperty() const { return PropertyIndex != INDEX_NONE; }
	bool IsSubObject() const { return SubObjectPackedId != 0 && !IsProperty(); }
};

/**
 * Main window widget for the Fusion Bandwidth Monitor.
 * Contains a toolbar, object list (left), and graph (right).
 */
class SFusionBandwidthWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFusionBandwidthWindow) {}
		SLATE_ARGUMENT(TSharedPtr<FFusionBandwidthDataCollector>, DataCollector)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFusionBandwidthWindow() override;

private:
	// Tree view callbacks
	TSharedRef<ITableRow> GenerateTreeRow(TSharedPtr<FBandwidthTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FBandwidthTreeItem> Item, TArray<TSharedPtr<FBandwidthTreeItem>>& OutChildren);
	void OnSelectionChanged(TSharedPtr<FBandwidthTreeItem> Item, ESelectInfo::Type SelectInfo);
	void OnSortColumnHeader(EColumnSortPriority::Type SortPriority, const FName& ColumnId, EColumnSortMode::Type SortMode);

	// Toolbar actions
	void OnRecordToggled(ECheckBoxState NewState);
	void OnClearClicked();

	// Data update callback
	void OnDataUpdated();

	void RebuildObjectList();
	TSharedRef<SHeaderRow> MakeObjectListHeaderRow();

	TSharedPtr<FFusionBandwidthDataCollector> DataCollector;
	TSharedPtr<STreeView<TSharedPtr<FBandwidthTreeItem>>> ReceiveTreeView;
	TArray<TSharedPtr<FBandwidthTreeItem>> ReceiveTreeItems;
	TSharedPtr<STreeView<TSharedPtr<FBandwidthTreeItem>>> SendTreeView;
	TArray<TSharedPtr<FBandwidthTreeItem>> SendTreeItems;
	TSharedPtr<SFusionBandwidthGraph> GraphWidget;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> ReceiveTotalText;
	TSharedPtr<STextBlock> SendTotalText;

	FName SortColumn = TEXT("Bytes");
	EColumnSortMode::Type SortMode = EColumnSortMode::Descending;
	int CachedObjectCount = 0;
	int CachedSendCount = 0;
	int CachedSubObjectCount = 0;
	int CachedPropertyCount = 0;
};

/**
 * Custom Slate leaf widget that draws bandwidth line graphs.
 */
class SFusionBandwidthGraph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFusionBandwidthGraph) {}
		SLATE_ARGUMENT(TSharedPtr<FFusionBandwidthDataCollector>, DataCollector)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	size_t ComputeMaxBytes() const;
	static size_t RoundToNiceNumber(size_t Value);

	TSharedPtr<FFusionBandwidthDataCollector> DataCollector;
};