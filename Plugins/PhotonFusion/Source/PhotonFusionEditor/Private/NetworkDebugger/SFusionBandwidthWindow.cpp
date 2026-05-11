// Copyright Exit Games GmbH. All Rights Reserved.

#include "NetworkDebugger/SFusionBandwidthWindow.h"

#include "Rendering/DrawElements.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "FusionBandwidthWindow"

// ─────────────────────────────────────────────────────────────────────────────
// SFusionBandwidthWindow
// ─────────────────────────────────────────────────────────────────────────────

void SFusionBandwidthWindow::Construct(const FArguments& InArgs)
{
	DataCollector = InArgs._DataCollector;
	check(DataCollector.IsValid());

	DataCollector->OnDataUpdated.AddRaw(this, &SFusionBandwidthWindow::OnDataUpdated);

	ChildSlot
	[
		SNew(SVerticalBox)

		// ── Toolbar ──
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked(DataCollector->IsRecording() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged(this, &SFusionBandwidthWindow::OnRecordToggled)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Record", "Record"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Clear", "Clear"))
				.OnClicked_Lambda([this]()
				{
					OnClearClicked();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(LOCTEXT("NotConnected", "Not connected"))
			]
		]

		// ── Splitter: List + Graph ──
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.35f)
			[
				SNew(SVerticalBox)

				// ── Receive section ──
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f, 4.0f, 4.0f, 0.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ReceiveHeader", "Receive"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.HAlign(HAlign_Right)
					[
						SAssignNew(ReceiveTotalText, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					]
				]

				+ SVerticalBox::Slot()
				.FillHeight(0.5f)
				[
					SAssignNew(ReceiveTreeView, STreeView<TSharedPtr<FBandwidthTreeItem>>)
					.TreeItemsSource(&ReceiveTreeItems)
					.OnGenerateRow(this, &SFusionBandwidthWindow::GenerateTreeRow)
					.OnGetChildren(this, &SFusionBandwidthWindow::OnGetChildren)
					.OnSelectionChanged(this, &SFusionBandwidthWindow::OnSelectionChanged)
					.SelectionMode(ESelectionMode::Multi)
					.HeaderRow(MakeObjectListHeaderRow())
				]

				// ── Divider ──
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					SNew(SSeparator)
				]

				// ── Send section ──
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f, 4.0f, 4.0f, 0.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SendHeader", "Send"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.HAlign(HAlign_Right)
					[
						SAssignNew(SendTotalText, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					]
				]

				+ SVerticalBox::Slot()
				.FillHeight(0.5f)
				[
					SAssignNew(SendTreeView, STreeView<TSharedPtr<FBandwidthTreeItem>>)
					.TreeItemsSource(&SendTreeItems)
					.OnGenerateRow(this, &SFusionBandwidthWindow::GenerateTreeRow)
					.OnGetChildren(this, &SFusionBandwidthWindow::OnGetChildren)
					.OnSelectionChanged(this, &SFusionBandwidthWindow::OnSelectionChanged)
					.SelectionMode(ESelectionMode::Multi)
					.HeaderRow(MakeObjectListHeaderRow())
				]
			]

			+ SSplitter::Slot()
			.Value(0.65f)
			[
				SAssignNew(GraphWidget, SFusionBandwidthGraph)
				.DataCollector(DataCollector)
			]
		]
	];
}

SFusionBandwidthWindow::~SFusionBandwidthWindow()
{
	if (DataCollector.IsValid())
	{
		DataCollector->OnDataUpdated.RemoveAll(this);
	}
}

// ─── Row widget ──────────────────────────────────────────────────────────────

class SBandwidthObjectRow : public SMultiColumnTableRow<TSharedPtr<FBandwidthTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SBandwidthObjectRow) {}
		SLATE_ARGUMENT(TSharedPtr<FBandwidthTreeItem>, TreeItem)
		SLATE_ARGUMENT(TSharedPtr<FFusionBandwidthDataCollector>, DataCollector)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
	{
		TreeItem = InArgs._TreeItem;
		DataCollector = InArgs._DataCollector;
		SMultiColumnTableRow::Construct(FSuperRowType::FArguments(), InOwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (TreeItem.IsValid() && TreeItem->IsProperty())
		{
			return GeneratePropertyWidget(ColumnName);
		}
		if (TreeItem.IsValid() && TreeItem->IsSubObject())
		{
			return GenerateSubObjectWidget(ColumnName);
		}
		return GenerateRootObjectWidget(ColumnName);
	}

private:
	TSharedRef<SWidget> GenerateRootObjectWidget(const FName& ColumnName)
	{
		auto GetData = [this]() -> const FObjectBandwidthData*
		{
			return DataCollector.IsValid() && TreeItem.IsValid()
				? DataCollector->GetObjectData().Find(TreeItem->RootPackedId)
				: nullptr;
		};

		auto GetAlphaColor = [GetData]() -> FSlateColor
		{
			const FObjectBandwidthData* Data = GetData();
			const float Alpha = (Data && Data->bIsAlive) ? 1.0f : 0.35f;
			return FSlateColor(FLinearColor(1, 1, 1, Alpha));
		};

		if (ColumnName == TEXT("Color"))
		{
			return SNew(SColorBlock)
				.Color_Lambda([GetData]()
				{
					const FObjectBandwidthData* Data = GetData();
					return Data ? Data->GraphColor : FLinearColor::White;
				})
				.Size(FVector2D(12.0f, 12.0f));
		}

		if (ColumnName == TEXT("Name"))
		{
			return SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([GetData]()
					{
						const FObjectBandwidthData* Data = GetData();
						return Data ? FText::FromString(Data->ObjectLabel) : FText::GetEmpty();
					})
					.ColorAndOpacity_Lambda(GetAlphaColor)
				];
		}

		if (ColumnName == TEXT("ObjectId"))
		{
			return SNew(STextBlock)
				.Text_Lambda([GetData]()
				{
					const FObjectBandwidthData* Data = GetData();
					return Data
						? FText::FromString(FString::Printf(TEXT("%u:%u"), Data->Origin, Data->Counter))
						: FText::GetEmpty();
				})
				.ColorAndOpacity_Lambda(GetAlphaColor);
		}

		if (ColumnName == TEXT("Bytes"))
		{
			return SNew(STextBlock)
				.Text_Lambda([GetData, this]()
				{
					const FObjectBandwidthData* Data = GetData();
					if (!Data) return FText::GetEmpty();
					size_t LastBytes = 0;
					if (Data->bIsSending)
					{
						const int32 Count = Data->GetSendSampleCount();
						LastBytes = Count > 0 ? Data->GetSendSample(Count - 1).Bytes : 0;
					}
					else
					{
						const int32 Count = Data->GetReceiveSampleCount();
						LastBytes = Count > 0 ? Data->GetReceiveSample(Count - 1).Bytes : 0;
					}

					// When expanded, subtract sub-object bytes to show root-only data
					if (IsItemExpanded())
					{
						size_t SubTotal = 0;
						for (const auto& SubPair : Data->SubObjects)
						{
							if (!SubPair.Value.bIsAlive) continue;
							const int32 SubCount = SubPair.Value.GetSampleCount();
							if (SubCount > 0)
							{
								SubTotal += SubPair.Value.GetSample(SubCount - 1).Bytes;
							}
						}
						LastBytes = (SubTotal <= LastBytes) ? (LastBytes - SubTotal) : 0;
					}

					return FText::AsNumber(static_cast<int64>(LastBytes));
				})
				.ColorAndOpacity_Lambda(GetAlphaColor);
		}

		return SNew(STextBlock).Text(FText::GetEmpty());
	}

	TSharedRef<SWidget> GenerateSubObjectWidget(const FName& ColumnName)
	{
		auto GetSubData = [this]() -> const FSubObjectBandwidthData*
		{
			if (!DataCollector.IsValid() || !TreeItem.IsValid())
			{
				return nullptr;
			}
			const FObjectBandwidthData* RootData = DataCollector->GetObjectData().Find(TreeItem->RootPackedId);
			if (!RootData)
			{
				return nullptr;
			}
			return RootData->SubObjects.Find(TreeItem->SubObjectPackedId);
		};

		auto GetSubAlphaColor = [GetSubData]() -> FSlateColor
		{
			const FSubObjectBandwidthData* E = GetSubData();
			const float Alpha = (E && E->bIsAlive) ? 0.7f : 0.3f;
			return FSlateColor(FLinearColor(1, 1, 1, Alpha));
		};

		if (ColumnName == TEXT("Color"))
		{
			return SNew(SColorBlock)
				.Color_Lambda([GetSubData]()
				{
					const FSubObjectBandwidthData* E = GetSubData();
					return E ? E->GraphColor : FLinearColor::White;
				})
				.Size(FVector2D(12.0f, 12.0f));
		}

		if (ColumnName == TEXT("Name"))
		{
			return SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([GetSubData]()
					{
						const FSubObjectBandwidthData* E = GetSubData();
						return E ? FText::FromString(E->Label) : FText::GetEmpty();
					})
					.ColorAndOpacity_Lambda(GetSubAlphaColor)
				];
		}

		if (ColumnName == TEXT("ObjectId"))
		{
			return SNew(STextBlock)
				.Text_Lambda([GetSubData]()
				{
					const FSubObjectBandwidthData* E = GetSubData();
					return E
						? FText::FromString(FString::Printf(TEXT("%u:%u"), E->Origin, E->Counter))
						: FText::GetEmpty();
				})
				.ColorAndOpacity_Lambda(GetSubAlphaColor);
		}

		if (ColumnName == TEXT("Bytes"))
		{
			return SNew(STextBlock)
				.Text_Lambda([GetSubData]()
				{
					const FSubObjectBandwidthData* E = GetSubData();
					if (!E) return FText::GetEmpty();
					const int32 Count = E->GetSampleCount();
					const size_t LastBytes = Count > 0 ? E->GetSample(Count - 1).Bytes : 0;
					return FText::AsNumber(static_cast<int64>(LastBytes));
				})
				.ColorAndOpacity_Lambda(GetSubAlphaColor);
		}

		return SNew(STextBlock).Text(FText::GetEmpty());
	}

	TSharedRef<SWidget> GeneratePropertyWidget(const FName& ColumnName)
	{
		auto GetProperty = [this]() -> const FPropertyBandwidthData*
		{
			if (!DataCollector.IsValid() || !TreeItem.IsValid())
			{
				return nullptr;
			}
			const FObjectBandwidthData* RootData = DataCollector->GetObjectData().Find(TreeItem->RootPackedId);
			if (!RootData)
			{
				return nullptr;
			}
			const TArray<FPropertyBandwidthData>* Props = nullptr;
			if (TreeItem->SubObjectPackedId != 0)
			{
				if (const FSubObjectBandwidthData* SubData = RootData->SubObjects.Find(TreeItem->SubObjectPackedId))
				{
					Props = &SubData->Properties;
				}
			}
			else
			{
				Props = &RootData->Properties;
			}
			if (!Props || !Props->IsValidIndex(TreeItem->PropertyIndex))
			{
				return nullptr;
			}
			return &(*Props)[TreeItem->PropertyIndex];
		};

		const FLinearColor PropertyTint(0.75f, 0.75f, 0.9f, 0.85f);

		if (ColumnName == TEXT("Color"))
		{
			return SNullWidget::NullWidget;
		}

		if (ColumnName == TEXT("Name"))
		{
			return SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([GetProperty]()
					{
						const FPropertyBandwidthData* P = GetProperty();
						return P ? FText::FromString(P->Name) : FText::GetEmpty();
					})
					.ColorAndOpacity(FSlateColor(PropertyTint))
				];
		}

		if (ColumnName == TEXT("ObjectId"))
		{
			return SNew(STextBlock)
				.Text(LOCTEXT("PropertyTag", "property"))
				.ColorAndOpacity(FSlateColor(PropertyTint));
		}

		if (ColumnName == TEXT("Bytes"))
		{
			return SNew(STextBlock)
				.Text_Lambda([GetProperty]()
				{
					const FPropertyBandwidthData* P = GetProperty();
					if (!P) return FText::GetEmpty();
					// SharedMode::Word is int32 — convert word counts to bytes for display.
					constexpr int32 BytesPerWord = 4;
					return FText::FromString(FString::Printf(TEXT("%d / %d B"),
						P->ChangedWordCount * BytesPerWord,
						P->WordCount * BytesPerWord));
				})
				.ColorAndOpacity(FSlateColor(PropertyTint));
		}

		return SNew(STextBlock).Text(FText::GetEmpty());
	}

	TSharedPtr<FBandwidthTreeItem> TreeItem;
	TSharedPtr<FFusionBandwidthDataCollector> DataCollector;
};

TSharedRef<SHeaderRow> SFusionBandwidthWindow::MakeObjectListHeaderRow()
{
	return SNew(SHeaderRow)

		+ SHeaderRow::Column(TEXT("Color"))
		.DefaultLabel(FText::GetEmpty())
		.FixedWidth(20.0f)

		+ SHeaderRow::Column(TEXT("Name"))
		.DefaultLabel(LOCTEXT("NameColumn", "Actor"))
		.FillWidth(200.0f)
		.SortMode_Lambda([this]() -> EColumnSortMode::Type
		{
			return SortColumn == TEXT("Name") ? SortMode : EColumnSortMode::None;
		})
		.OnSort(this, &SFusionBandwidthWindow::OnSortColumnHeader)

		+ SHeaderRow::Column(TEXT("ObjectId"))
		.DefaultLabel(LOCTEXT("ObjectIdColumn", "ObjectId"))
		.FillWidth(90.0f)

		+ SHeaderRow::Column(TEXT("Bytes"))
		.DefaultLabel(LOCTEXT("BytesColumn", "B/Frame"))
		.FillWidth(70.0f)
		.SortMode_Lambda([this]() -> EColumnSortMode::Type
		{
			return SortColumn == TEXT("Bytes") ? SortMode : EColumnSortMode::None;
		})
		.OnSort(this, &SFusionBandwidthWindow::OnSortColumnHeader);
}

TSharedRef<ITableRow> SFusionBandwidthWindow::GenerateTreeRow(TSharedPtr<FBandwidthTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SBandwidthObjectRow, OwnerTable)
		.TreeItem(Item)
		.DataCollector(DataCollector);
}

void SFusionBandwidthWindow::OnGetChildren(TSharedPtr<FBandwidthTreeItem> Item, TArray<TSharedPtr<FBandwidthTreeItem>>& OutChildren)
{
	if (Item.IsValid() && !Item->IsProperty())
	{
		OutChildren = Item->Children;
	}
}

void SFusionBandwidthWindow::OnSelectionChanged(TSharedPtr<FBandwidthTreeItem> Item, ESelectInfo::Type SelectInfo)
{
	if (!DataCollector.IsValid())
	{
		return;
	}

	// Clear all selection flags
	for (auto& Pair : DataCollector->GetObjectDataMutable())
	{
		Pair.Value.bIsSelected = false;
		for (auto& SubPair : Pair.Value.SubObjects)
		{
			SubPair.Value.bIsSelected = false;
		}
	}

	// Gather selections from both tree views
	TArray<TSharedPtr<FBandwidthTreeItem>> SelectedItems;
	if (ReceiveTreeView.IsValid())
	{
		TArray<TSharedPtr<FBandwidthTreeItem>> Temp;
		ReceiveTreeView->GetSelectedItems(Temp);
		SelectedItems.Append(Temp);
	}
	if (SendTreeView.IsValid())
	{
		TArray<TSharedPtr<FBandwidthTreeItem>> Temp;
		SendTreeView->GetSelectedItems(Temp);
		SelectedItems.Append(Temp);
	}

	for (const TSharedPtr<FBandwidthTreeItem>& Selected : SelectedItems)
	{
		if (!Selected.IsValid() || Selected->IsProperty())
		{
			continue;
		}

		FObjectBandwidthData* Data = DataCollector->GetObjectDataMutable().Find(Selected->RootPackedId);
		if (!Data)
		{
			continue;
		}

		// Always mark the root as selected (for viewport highlighting)
		Data->bIsSelected = true;

		if (Selected->IsSubObject())
		{
			FSubObjectBandwidthData* SubData = Data->SubObjects.Find(Selected->SubObjectPackedId);
			if (SubData)
			{
				SubData->bIsSelected = true;
			}
		}
	}
}

void SFusionBandwidthWindow::OnSortColumnHeader(EColumnSortPriority::Type SortPriority, const FName& ColumnId, EColumnSortMode::Type InSortMode)
{
	SortColumn = ColumnId;
	SortMode = InSortMode;
	RebuildObjectList();
}

void SFusionBandwidthWindow::OnRecordToggled(ECheckBoxState NewState)
{
	if (DataCollector.IsValid())
	{
		DataCollector->SetRecording(NewState == ECheckBoxState::Checked);
	}
}

void SFusionBandwidthWindow::OnClearClicked()
{
	if (DataCollector.IsValid())
	{
		DataCollector->ClearAllData();
		ReceiveTreeItems.Empty();
		SendTreeItems.Empty();
		if (ReceiveTreeView.IsValid())
		{
			ReceiveTreeView->RequestTreeRefresh();
		}
		if (SendTreeView.IsValid())
		{
			SendTreeView->RequestTreeRefresh();
		}
	}
}

void SFusionBandwidthWindow::OnDataUpdated()
{
	if (!DataCollector.IsValid())
	{
		return;
	}

	// Update status text
	if (StatusText.IsValid())
	{
		if (DataCollector->IsConnected())
		{
			StatusText->SetText(FText::Format(
				LOCTEXT("ConnectedStatus", "Connected \u2014 {0} objects"),
				FText::AsNumber(DataCollector->GetObjectCount())));
		}
		else
		{
			StatusText->SetText(LOCTEXT("NotConnected", "Not connected"));
		}
	}

	// Update total bytes text
	auto FormatBytes = [](uint64 Bytes) -> FString
	{
		if (Bytes < 1024)
		{
			return FString::Printf(TEXT("%llu B"), Bytes);
		}
		if (Bytes < 1024 * 1024)
		{
			return FString::Printf(TEXT("%.1f KB"), Bytes / 1024.0);
		}
		if (Bytes < 1024ull * 1024 * 1024)
		{
			return FString::Printf(TEXT("%.1f MB"), Bytes / (1024.0 * 1024.0));
		}
		return FString::Printf(TEXT("%.2f GB"), Bytes / (1024.0 * 1024.0 * 1024.0));
	};

	if (ReceiveTotalText.IsValid())
	{
		ReceiveTotalText->SetText(FText::FromString(
			FString::Printf(TEXT("Total: %s"), *FormatBytes(DataCollector->GetTotalBytesReceived()))));
	}
	if (SendTotalText.IsValid())
	{
		SendTotalText->SetText(FText::FromString(
			FString::Printf(TEXT("Total: %s"), *FormatBytes(DataCollector->GetTotalBytesSent()))));
	}

	// Ensure graph repaints with latest data
	if (GraphWidget.IsValid())
	{
		GraphWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}

	// Rebuild when total count, send/receive split, or alive sub-object/property counts change
	const int32 NewCount = DataCollector->GetObjectCount();
	int32 NewSendCount = 0;
	int32 NewSubObjectCount = 0;
	int32 NewPropertyCount = 0;
	for (const auto& Pair : DataCollector->GetObjectData())
	{
		if (Pair.Value.bIsSending)
		{
			++NewSendCount;
		}
		NewPropertyCount += Pair.Value.Properties.Num();
		for (const auto& SubPair : Pair.Value.SubObjects)
		{
			if (SubPair.Value.bIsAlive)
			{
				++NewSubObjectCount;
				NewPropertyCount += SubPair.Value.Properties.Num();
			}
		}
	}

	if (NewCount != CachedObjectCount || NewSendCount != CachedSendCount
		|| NewSubObjectCount != CachedSubObjectCount || NewPropertyCount != CachedPropertyCount)
	{
		CachedObjectCount = NewCount;
		CachedSendCount = NewSendCount;
		CachedSubObjectCount = NewSubObjectCount;
		CachedPropertyCount = NewPropertyCount;
		RebuildObjectList();
	}
	else
	{
		if (ReceiveTreeView.IsValid())
		{
			ReceiveTreeView->RequestTreeRefresh();
		}
		if (SendTreeView.IsValid())
		{
			SendTreeView->RequestTreeRefresh();
		}
	}
}

void SFusionBandwidthWindow::RebuildObjectList()
{
	if (!DataCollector.IsValid())
	{
		return;
	}

	// Preserve selection for both root and sub-object items
	TSet<uint64> SelectedRootSet;
	TSet<uint64> SelectedSubSet;
	{
		TArray<TSharedPtr<FBandwidthTreeItem>> SelectedItems;
		if (ReceiveTreeView.IsValid())
		{
			ReceiveTreeView->GetSelectedItems(SelectedItems);
		}
		if (SendTreeView.IsValid())
		{
			SendTreeView->GetSelectedItems(SelectedItems);
		}
		for (const auto& Sel : SelectedItems)
		{
			if (!Sel.IsValid()) continue;
			if (Sel->IsSubObject())
			{
				SelectedSubSet.Add(Sel->SubObjectPackedId);
			}
			else
			{
				SelectedRootSet.Add(Sel->RootPackedId);
			}
		}
	}

	// Preserve expansion state (for both root items and sub-object items)
	TSet<uint64> ExpandedRootSet;
	TSet<uint64> ExpandedSubSet;
	{
		auto SaveExpansion = [&](const TSharedPtr<STreeView<TSharedPtr<FBandwidthTreeItem>>>& Tree, const TArray<TSharedPtr<FBandwidthTreeItem>>& Items)
		{
			if (!Tree.IsValid()) return;
			for (const auto& Item : Items)
			{
				if (!Item.IsValid()) continue;
				if (Tree->IsItemExpanded(Item))
				{
					ExpandedRootSet.Add(Item->RootPackedId);
				}
				for (const auto& Child : Item->Children)
				{
					if (Child.IsValid() && Child->IsSubObject() && Tree->IsItemExpanded(Child))
					{
						ExpandedSubSet.Add(Child->SubObjectPackedId);
					}
				}
			}
		};
		SaveExpansion(ReceiveTreeView, ReceiveTreeItems);
		SaveExpansion(SendTreeView, SendTreeItems);
	}

	ReceiveTreeItems.Empty();
	SendTreeItems.Empty();

	for (const auto& Pair : DataCollector->GetObjectData())
	{
		TSharedPtr<FBandwidthTreeItem> RootItem = MakeShared<FBandwidthTreeItem>();
		RootItem->RootPackedId = Pair.Key;

		// Build property rows for the root object itself
		for (int32 PropIndex = 0; PropIndex < Pair.Value.Properties.Num(); ++PropIndex)
		{
			TSharedPtr<FBandwidthTreeItem> PropItem = MakeShared<FBandwidthTreeItem>();
			PropItem->RootPackedId = Pair.Key;
			PropItem->PropertyIndex = PropIndex;
			RootItem->Children.Add(PropItem);
		}

		// Build children from alive sub-objects (each sub-object gets its own property rows)
		for (const auto& [SubPackedId, SubData] : Pair.Value.SubObjects)
		{
			if (!SubData.bIsAlive)
			{
				continue;
			}
			TSharedPtr<FBandwidthTreeItem> ChildItem = MakeShared<FBandwidthTreeItem>();
			ChildItem->RootPackedId = Pair.Key;
			ChildItem->SubObjectPackedId = SubPackedId;
			for (int32 PropIndex = 0; PropIndex < SubData.Properties.Num(); ++PropIndex)
			{
				TSharedPtr<FBandwidthTreeItem> PropItem = MakeShared<FBandwidthTreeItem>();
				PropItem->RootPackedId = Pair.Key;
				PropItem->SubObjectPackedId = SubPackedId;
				PropItem->PropertyIndex = PropIndex;
				ChildItem->Children.Add(PropItem);
			}
			RootItem->Children.Add(ChildItem);
		}

		if (Pair.Value.bIsSending)
		{
			SendTreeItems.Add(RootItem);
		}
		else
		{
			ReceiveTreeItems.Add(RootItem);
		}
	}

	// Sort helper
	const auto& Data = DataCollector->GetObjectData();
	auto SortList = [&](TArray<TSharedPtr<FBandwidthTreeItem>>& Items)
	{
		Items.Sort([&](const TSharedPtr<FBandwidthTreeItem>& A, const TSharedPtr<FBandwidthTreeItem>& B)
		{
			const FObjectBandwidthData* DA = Data.Find(A->RootPackedId);
			const FObjectBandwidthData* DB = Data.Find(B->RootPackedId);
			if (!DA || !DB) return false;

			auto GetLastBytes = [](const FObjectBandwidthData* D) -> size_t
			{
				if (D->bIsSending)
				{
					const int32 C = D->GetSendSampleCount();
					return C > 0 ? D->GetSendSample(C - 1).Bytes : 0;
				}
				const int32 C = D->GetReceiveSampleCount();
				return C > 0 ? D->GetReceiveSample(C - 1).Bytes : 0;
			};

			bool bLess = false;
			if (SortColumn == TEXT("Name"))
			{
				bLess = DA->ObjectLabel < DB->ObjectLabel;
			}
			else // "Bytes" or default
			{
				bLess = GetLastBytes(DA) < GetLastBytes(DB);
			}
			return SortMode == EColumnSortMode::Ascending ? bLess : !bLess;
		});
	};
	SortList(ReceiveTreeItems);
	SortList(SendTreeItems);

	// Refresh and restore selection + expansion
	auto RestoreState = [&](const TSharedPtr<STreeView<TSharedPtr<FBandwidthTreeItem>>>& TreeView, const TArray<TSharedPtr<FBandwidthTreeItem>>& Items)
	{
		if (!TreeView.IsValid()) return;
		TreeView->RequestTreeRefresh();

		// First pass: restore root selection and expansion
		for (const TSharedPtr<FBandwidthTreeItem>& Item : Items)
		{
			if (!Item.IsValid()) continue;
			if (SelectedRootSet.Contains(Item->RootPackedId))
			{
				TreeView->SetItemSelection(Item, true, ESelectInfo::Direct);
			}
			if (ExpandedRootSet.Contains(Item->RootPackedId))
			{
				TreeView->SetItemExpansion(Item, true);
			}
		}

		// Second pass: restore sub-object selection + expansion (after root expansion so children are known)
		for (const TSharedPtr<FBandwidthTreeItem>& Item : Items)
		{
			if (!Item.IsValid()) continue;
			for (const TSharedPtr<FBandwidthTreeItem>& Child : Item->Children)
			{
				if (!Child.IsValid() || !Child->IsSubObject()) continue;
				if (SelectedSubSet.Contains(Child->SubObjectPackedId))
				{
					TreeView->SetItemSelection(Child, true, ESelectInfo::Direct);
				}
				if (ExpandedSubSet.Contains(Child->SubObjectPackedId))
				{
					TreeView->SetItemExpansion(Child, true);
				}
			}
		}
	};
	RestoreState(ReceiveTreeView, ReceiveTreeItems);
	RestoreState(SendTreeView, SendTreeItems);
}

// ─────────────────────────────────────────────────────────────────────────────
// SFusionBandwidthGraph
// ─────────────────────────────────────────────────────────────────────────────

void SFusionBandwidthGraph::Construct(const FArguments& InArgs)
{
	DataCollector = InArgs._DataCollector;
}

FVector2D SFusionBandwidthGraph::ComputeDesiredSize(float) const
{
	return FVector2D(400.0f, 200.0f);
}

size_t SFusionBandwidthGraph::ComputeMaxBytes() const
{
	if (!DataCollector.IsValid())
	{
		return 100;
	}

	size_t MaxBytes = 0;
	for (const auto& Pair : DataCollector->GetObjectData())
	{
		if (!Pair.Value.bIsSelected)
		{
			continue;
		}

		// Check if any sub-objects are selected
		bool bHasSelectedSub = false;
		for (const auto& SubPair : Pair.Value.SubObjects)
		{
			if (SubPair.Value.bIsSelected)
			{
				bHasSelectedSub = true;
				const int32 Count = SubPair.Value.GetSampleCount();
				for (int32 i = 0; i < Count; ++i)
				{
					MaxBytes = FMath::Max(MaxBytes, SubPair.Value.GetSample(i).Bytes);
				}
			}
		}

		// If no sub-objects selected, use root object data
		if (!bHasSelectedSub)
		{
			if (Pair.Value.bIsSending)
			{
				const int32 Count = Pair.Value.GetSendSampleCount();
				for (int32 i = 0; i < Count; ++i)
				{
					MaxBytes = FMath::Max(MaxBytes, Pair.Value.GetSendSample(i).Bytes);
				}
			}
			else
			{
				const int32 Count = Pair.Value.GetReceiveSampleCount();
				for (int32 i = 0; i < Count; ++i)
				{
					MaxBytes = FMath::Max(MaxBytes, Pair.Value.GetReceiveSample(i).Bytes);
				}
			}
		}
	}

	return MaxBytes > 0 ? MaxBytes : 100;
}

size_t SFusionBandwidthGraph::RoundToNiceNumber(size_t Value)
{
	if (Value <= 10) return 10;

	// Round up to next power-of-10 based nice number
	size_t Magnitude = 1;
	while (Magnitude * 10 < Value)
	{
		Magnitude *= 10;
	}

	if (Value <= Magnitude * 2) return Magnitude * 2;
	if (Value <= Magnitude * 5) return Magnitude * 5;
	return Magnitude * 10;
}

// Helper: draw a single history curve
static void DrawHistoryCurve(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FGeometry& AllottedGeometry,
	float LeftMargin, float TopMargin, float GraphWidth, float GraphHeight,
	size_t NiceMax,
	const TArray<FBandwidthSample>& HistoryArray, int32 HistoryHead, int32 SampleCount, int32 MaxHistory,
	FLinearColor CurveColor)
{
	if (SampleCount < 2)
	{
		return;
	}

	auto GetSample = [&](int32 Index) -> FBandwidthSample
	{
		if (SampleCount < MaxHistory)
		{
			return HistoryArray[Index];
		}
		const int32 Actual = (HistoryHead + Index) % MaxHistory;
		return HistoryArray[Actual];
	};

	TArray<FVector2D> LinePoints;
	LinePoints.Reserve(SampleCount);

	for (int32 i = 0; i < SampleCount; ++i)
	{
		const FBandwidthSample Sample = GetSample(i);
		const float XFraction = static_cast<float>(i) / (MaxHistory - 1);
		const float YFraction = NiceMax > 0
			? static_cast<float>(Sample.Bytes) / NiceMax
			: 0.0f;

		const float X = LeftMargin + XFraction * GraphWidth;
		const float Y = TopMargin + GraphHeight * (1.0f - FMath::Clamp(YFraction, 0.0f, 1.0f));
		LinePoints.Add(FVector2D(X, Y));
	}

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		LinePoints,
		ESlateDrawEffect::None,
		CurveColor,
		true,
		2.0f
	);
}

int32 SFusionBandwidthGraph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float Width = Size.X;
	const float Height = Size.Y;

	constexpr float LeftMargin = 55.0f;
	constexpr float TopMargin = 10.0f;
	constexpr float BottomMargin = 20.0f;
	constexpr float RightMargin = 10.0f;

	const float GraphWidth = FMath::Max(1.0f, Width - LeftMargin - RightMargin);
	const float GraphHeight = FMath::Max(1.0f, Height - TopMargin - BottomMargin);

	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");

	// Background
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush,
		ESlateDrawEffect::None,
		FLinearColor(0.02f, 0.02f, 0.03f, 1.0f)
	);
	++LayerId;

	const size_t RawMax = ComputeMaxBytes();
	const size_t NiceMax = RoundToNiceNumber(RawMax);
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	// Horizontal grid lines (5 levels)
	constexpr int32 GridLines = 5;
	for (int32 i = 0; i <= GridLines; ++i)
	{
		const float Fraction = static_cast<float>(i) / GridLines;
		const float Y = TopMargin + GraphHeight * (1.0f - Fraction);

		TArray<FVector2D> LinePoints;
		LinePoints.Add(FVector2D(LeftMargin, Y));
		LinePoints.Add(FVector2D(LeftMargin + GraphWidth, Y));

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			FLinearColor(0.15f, 0.15f, 0.15f, 1.0f),
			true,
			1.0f
		);

		// Y-axis label
		const size_t ByteValue = static_cast<size_t>(Fraction * NiceMax);
		const FString Label = FString::Printf(TEXT("%u B"), static_cast<uint32>(ByteValue));
		const FGeometry LabelGeometry = AllottedGeometry.MakeChild(
			FVector2D(LeftMargin - 4.0f, 14.0f),
			FSlateLayoutTransform(FVector2D(2.0f, Y - 7.0f))
		);
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			LabelGeometry.ToPaintGeometry(),
			Label,
			Font,
			ESlateDrawEffect::None,
			FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)
		);
	}
	++LayerId;

	// Draw line curves for selected objects
	if (DataCollector.IsValid())
	{
		for (const auto& Pair : DataCollector->GetObjectData())
		{
			const FObjectBandwidthData& ObjData = Pair.Value;
			if (!ObjData.bIsSelected)
			{
				continue;
			}

			// Check if any sub-objects of this root are selected
			bool bHasSelectedSub = false;
			for (const auto& SubPair : ObjData.SubObjects)
			{
				if (SubPair.Value.bIsSelected)
				{
					bHasSelectedSub = true;

					const FSubObjectBandwidthData& SubData = SubPair.Value;
					FLinearColor SubColor = SubData.GraphColor;
					if (!SubData.bIsAlive)
					{
						SubColor.A = 0.3f;
					}

					DrawHistoryCurve(
						OutDrawElements, LayerId, AllottedGeometry,
						LeftMargin, TopMargin, GraphWidth, GraphHeight, NiceMax,
						SubData.History, SubData.HistoryHead, SubData.GetSampleCount(), FSubObjectBandwidthData::MaxHistory,
						SubColor);
				}
			}

			// If no sub-objects selected, draw root object curve
			if (!bHasSelectedSub)
			{
				const bool bSending = ObjData.bIsSending;
				FLinearColor CurveColor = ObjData.GraphColor;
				if (!ObjData.bIsAlive)
				{
					CurveColor.A = 0.3f;
				}

				if (bSending)
				{
					DrawHistoryCurve(
						OutDrawElements, LayerId, AllottedGeometry,
						LeftMargin, TopMargin, GraphWidth, GraphHeight, NiceMax,
						ObjData.SendHistory, ObjData.SendHistoryHead, ObjData.GetSendSampleCount(), FObjectBandwidthData::MaxHistory,
						CurveColor);
				}
				else
				{
					DrawHistoryCurve(
						OutDrawElements, LayerId, AllottedGeometry,
						LeftMargin, TopMargin, GraphWidth, GraphHeight, NiceMax,
						ObjData.ReceiveHistory, ObjData.ReceiveHistoryHead, ObjData.GetReceiveSampleCount(), FObjectBandwidthData::MaxHistory,
						CurveColor);
				}
			}
		}
	}
	++LayerId;

	return LayerId;
}

#undef LOCTEXT_NAMESPACE