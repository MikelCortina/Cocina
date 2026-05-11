// Copyright Exit Games GmbH. All Rights Reserved.

#include "NetworkDebugger/SFusionStringHeapWindow.h"

#include "FusionClient.h"
#include "FusionOnlineSubsystem.h"
#include "FusionUtils.h"
#include "FusionObjectActorPair.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

#include "Fusion/Client.h"
#include "Fusion/Types.h"

#define LOCTEXT_NAMESPACE "FusionStringHeapWindow"

// ─────────────────────────────────────────────────────────────────────────────
// SFusionStringHeapBar
// ─────────────────────────────────────────────────────────────────────────────

void SFusionStringHeapBar::Construct(const FArguments& InArgs)
{
}

void SFusionStringHeapBar::SetSnapshot(const FStringHeapSnapshot& InSnapshot)
{
	Snapshot = InSnapshot;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SFusionStringHeapBar::ComputeDesiredSize(float) const
{
	return FVector2D(400.0f, 160.0f);
}

FLinearColor SFusionStringHeapBar::GetSegmentColor(uint32 Index)
{
	constexpr float GoldenAngle = 137.508f;
	const float Hue = FMath::Fmod(Index * GoldenAngle, 360.0f);
	return FLinearColor::MakeFromHSV8(
		static_cast<uint8>(Hue / 360.0f * 255.0f),
		180,
		220
	);
}

int32 SFusionStringHeapBar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float Width = Size.X;
	const float Height = Size.Y;

	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);

	constexpr float LeftMargin = 10.0f;
	constexpr float RightMargin = 10.0f;
	constexpr float TopMargin = 24.0f;
	constexpr float BottomMargin = 30.0f;
	constexpr float BarHeight = 40.0f;

	const float BarWidth = FMath::Max(1.0f, Width - LeftMargin - RightMargin);
	const float BarTop = TopMargin;

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

	if (Snapshot.HeapSize == 0)
	{
		// "No data" text
		const FGeometry TextGeo = AllottedGeometry.MakeChild(
			FVector2D(Width, 20.0f),
			FSlateLayoutTransform(FVector2D(0.0f, Height * 0.5f - 10.0f))
		);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId, TextGeo.ToPaintGeometry(),
			TEXT("Select an object to visualize its string heap"),
			Font, ESlateDrawEffect::None,
			FLinearColor(0.4f, 0.4f, 0.4f, 1.0f)
		);
		return LayerId + 1;
	}

	// Title
	{
		const FString Title = FString::Printf(TEXT("Heap: %u bytes  |  %u entries  |  %u used  |  %u free  |  %.1f%% utilization"),
			Snapshot.HeapSize, Snapshot.EntryCount, Snapshot.UsedBytes, Snapshot.FreeBytes,
			Snapshot.HeapSize > 0 ? (Snapshot.UsedBytes * 100.0f / Snapshot.HeapSize) : 0.0f);
		const FGeometry TitleGeo = AllottedGeometry.MakeChild(
			FVector2D(BarWidth, 16.0f),
			FSlateLayoutTransform(FVector2D(LeftMargin, 4.0f))
		);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId, TitleGeo.ToPaintGeometry(),
			Title, Font, ESlateDrawEffect::None,
			FLinearColor(0.7f, 0.7f, 0.7f, 1.0f)
		);
	}

	// Bar background (represents total heap)
	{
		const FGeometry BarBgGeo = AllottedGeometry.MakeChild(
			FVector2D(BarWidth, BarHeight),
			FSlateLayoutTransform(FVector2D(LeftMargin, BarTop))
		);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId, BarBgGeo.ToPaintGeometry(),
			WhiteBrush, ESlateDrawEffect::None,
			FLinearColor(0.12f, 0.12f, 0.12f, 1.0f) // Dark gray = unallocated
		);
	}
	++LayerId;

	// Draw segments
	const float ByteToPixel = BarWidth / FMath::Max(1u, Snapshot.HeapSize);
	uint32 AliveIndex = 0;

	for (const FHeapSegment& Seg : Snapshot.Segments)
	{
		const float SegX = LeftMargin + Seg.Offset * ByteToPixel;
		const float SegW = FMath::Max(1.0f, Seg.Size * ByteToPixel);

		FLinearColor Color;
		if (Seg.bAlive)
		{
			Color = GetSegmentColor(AliveIndex);
			++AliveIndex;
		}
		else
		{
			// Free segment: hatched gray pattern
			Color = FLinearColor(0.25f, 0.22f, 0.18f, 1.0f);
		}

		const FGeometry SegGeo = AllottedGeometry.MakeChild(
			FVector2D(SegW, BarHeight),
			FSlateLayoutTransform(FVector2D(SegX, BarTop))
		);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId, SegGeo.ToPaintGeometry(),
			WhiteBrush, ESlateDrawEffect::None,
			Color
		);

		// For free segments, draw diagonal lines to indicate fragmentation
		if (!Seg.bAlive && SegW > 4.0f)
		{
			const float LineSpacing = 6.0f;
			for (float Lx = 0.0f; Lx < SegW + BarHeight; Lx += LineSpacing)
			{
				float X0 = SegX + Lx;
				float Y0 = BarTop;
				float X1 = SegX + Lx - BarHeight;
				float Y1 = BarTop + BarHeight;

				// Clip to segment bounds
				if (X1 < SegX)
				{
					Y1 = BarTop + (Lx);
					X1 = SegX;
					if (Y1 > BarTop + BarHeight) continue;
				}
				if (X0 > SegX + SegW)
				{
					Y0 = BarTop + (X0 - (SegX + SegW));
					X0 = SegX + SegW;
					if (Y0 > BarTop + BarHeight) continue;
				}

				TArray<FVector2D> LinePoints;
				LinePoints.Add(FVector2D(X0, Y0));
				LinePoints.Add(FVector2D(X1, Y1));
				FSlateDrawElement::MakeLines(
					OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(),
					LinePoints, ESlateDrawEffect::None,
					FLinearColor(0.35f, 0.30f, 0.22f, 0.6f),
					true, 1.0f
				);
			}
		}
	}
	LayerId += 2;

	// Draw segment borders (thin lines between segments)
	for (const FHeapSegment& Seg : Snapshot.Segments)
	{
		const float SegX = LeftMargin + Seg.Offset * ByteToPixel;
		const float SegW = FMath::Max(1.0f, Seg.Size * ByteToPixel);

		// Left border
		{
			TArray<FVector2D> LinePoints;
			LinePoints.Add(FVector2D(SegX, BarTop));
			LinePoints.Add(FVector2D(SegX, BarTop + BarHeight));
			FSlateDrawElement::MakeLines(
				OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
				LinePoints, ESlateDrawEffect::None,
				FLinearColor(0.0f, 0.0f, 0.0f, 0.5f),
				true, 1.0f
			);
		}
	}
	++LayerId;

	// Draw offset labels below the bar
	{
		// Start label
		const FGeometry StartGeo = AllottedGeometry.MakeChild(
			FVector2D(60.0f, 14.0f),
			FSlateLayoutTransform(FVector2D(LeftMargin, BarTop + BarHeight + 2.0f))
		);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId, StartGeo.ToPaintGeometry(),
			TEXT("0"), SmallFont, ESlateDrawEffect::None,
			FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)
		);

		// End label
		const FString EndLabel = FString::Printf(TEXT("%u"), Snapshot.HeapSize);
		const FGeometry EndGeo = AllottedGeometry.MakeChild(
			FVector2D(60.0f, 14.0f),
			FSlateLayoutTransform(FVector2D(LeftMargin + BarWidth - 40.0f, BarTop + BarHeight + 2.0f))
		);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId, EndGeo.ToPaintGeometry(),
			EndLabel, SmallFont, ESlateDrawEffect::None,
			FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)
		);
	}
	++LayerId;

	// Draw segment detail labels below the offset axis
	const float DetailTop = BarTop + BarHeight + 16.0f;
	const float DetailRowHeight = 14.0f;
	AliveIndex = 0;
	float CurrentDetailY = DetailTop;

	for (const FHeapSegment& Seg : Snapshot.Segments)
	{
		if (CurrentDetailY + DetailRowHeight > Height)
		{
			break;
		}

		FString Label;
		FLinearColor LabelColor;

		if (Seg.bAlive)
		{
			const FString Preview = Seg.ResolvedString.Len() > 64
				? Seg.ResolvedString.Left(64) + TEXT("...")
				: Seg.ResolvedString;
			Label = FString::Printf(TEXT("[%u] @%u +%u \"%s\""), Seg.EntryId, Seg.Offset, Seg.Size, *Preview);
			LabelColor = GetSegmentColor(AliveIndex);
			++AliveIndex;
		}
		else
		{
			Label = FString::Printf(TEXT("FREE @%u +%u"), Seg.Offset, Seg.Size);
			LabelColor = FLinearColor(0.5f, 0.45f, 0.35f, 1.0f);
		}

		// Color swatch
		const FGeometry SwatchGeo = AllottedGeometry.MakeChild(
			FVector2D(8.0f, 8.0f),
			FSlateLayoutTransform(FVector2D(LeftMargin, CurrentDetailY + 2.0f))
		);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId, SwatchGeo.ToPaintGeometry(),
			WhiteBrush, ESlateDrawEffect::None,
			Seg.bAlive ? LabelColor : FLinearColor(0.25f, 0.22f, 0.18f, 1.0f)
		);

		// Label text
		const FGeometry LabelGeo = AllottedGeometry.MakeChild(
			FVector2D(BarWidth - 16.0f, DetailRowHeight),
			FSlateLayoutTransform(FVector2D(LeftMargin + 12.0f, CurrentDetailY))
		);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId, LabelGeo.ToPaintGeometry(),
			Label, SmallFont, ESlateDrawEffect::None,
			LabelColor
		);

		CurrentDetailY += DetailRowHeight;
	}

	return LayerId + 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// SFusionStringHeapWindow
// ─────────────────────────────────────────────────────────────────────────────

void SFusionStringHeapWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Status bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SAssignNew(StatusText, STextBlock)
			.Text(LOCTEXT("NotConnected", "Not connected"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f)
		[
			SNew(SSeparator)
		]

		// Splitter: Object list + Heap visualization
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Left: Object list
			+ SSplitter::Slot()
			.Value(0.30f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ObjectsHeader", "Objects"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(ObjectListView, SListView<TSharedPtr<FStringHeapObjectItem>>)
					.ListItemsSource(&ObjectItems)
					.OnGenerateRow(this, &SFusionStringHeapWindow::GenerateObjectRow)
					.OnSelectionChanged(this, &SFusionStringHeapWindow::OnObjectSelected)
					.SelectionMode(ESelectionMode::Single)
					.HeaderRow(
						SNew(SHeaderRow)

						+ SHeaderRow::Column(TEXT("Name"))
						.DefaultLabel(LOCTEXT("NameCol", "Actor"))
						.FillWidth(140.0f)

						+ SHeaderRow::Column(TEXT("ObjectId"))
						.DefaultLabel(LOCTEXT("ObjectIdCol", "ObjectId"))
						.FillWidth(90.0f)

						+ SHeaderRow::Column(TEXT("Heap"))
						.DefaultLabel(LOCTEXT("HeapCol", "Heap"))
						.FillWidth(60.0f)

						+ SHeaderRow::Column(TEXT("Used"))
						.DefaultLabel(LOCTEXT("UsedCol", "Used"))
						.FillWidth(60.0f)

						+ SHeaderRow::Column(TEXT("Entries"))
						.DefaultLabel(LOCTEXT("EntriesCol", "Entries"))
						.FillWidth(50.0f)
					)
				]
			]

			// Right: Heap bar visualization
			+ SSplitter::Slot()
			.Value(0.70f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(HeapBar, SFusionStringHeapBar)
				]
			]
		]
	];

	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SFusionStringHeapWindow::TickCollect));
}

UFusionClient* SFusionStringHeapWindow::FindFusionClient() const
{
	if (!GEngine)
	{
		return nullptr;
	}

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

EActiveTimerReturnType SFusionStringHeapWindow::TickCollect(double InCurrentTime, float InDeltaTime)
{
	UFusionClient* FusionClient = FindFusionClient();
	if (!FusionClient || !FusionClient->GetClient())
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(LOCTEXT("NotConnected", "Not connected"));
		}
		ObjectItems.Empty();
		if (ObjectListView.IsValid())
		{
			ObjectListView->RequestListRefresh();
		}
		return EActiveTimerReturnType::Continue;
	}

	RebuildObjectList();

	if (SelectedPackedId != 0)
	{
		CollectSnapshot(FusionClient, SelectedPackedId);
		if (HeapBar.IsValid())
		{
			HeapBar->SetSnapshot(CurrentSnapshot);
		}
	}

	if (StatusText.IsValid())
	{
		const int32 Count = ObjectItems.Num();
		StatusText->SetText(FText::Format(
			LOCTEXT("ConnectedFmt", "Connected \u2014 {0} objects with string heaps"),
			FText::AsNumber(Count)));
	}

	return EActiveTimerReturnType::Continue;
}

void SFusionStringHeapWindow::RebuildObjectList()
{
	UFusionClient* FusionClient = FindFusionClient();
	if (!FusionClient || !FusionClient->GetClient())
	{
		return;
	}

	// Collect current packed IDs in order
	TArray<uint64> CurrentIds;
	auto& RootObjects = FusionClient->GetClient()->AllRootObjects();
	for (auto& [ObjectId, RootObject] : RootObjects)
	{
		if (!RootObject || !RootObject->GetHasValidData())
		{
			continue;
		}
		CurrentIds.Add(static_cast<uint64>(ObjectId));

		if (FusionClient->GetClient()->HasSubObjects(RootObject))
		{
			for (const auto& SubId : FusionClient->GetClient()->GetSubObject(RootObject))
			{
				auto* SubObj = FusionClient->GetClient()->FindObject(SubId);
				if (SubObj && SubObj->GetHasValidData())
				{
					CurrentIds.Add(static_cast<uint64>(SubId));
				}
			}
		}
	}

	// Check if the set of IDs changed
	bool bSetChanged = CurrentIds.Num() != ObjectItems.Num();
	if (!bSetChanged)
	{
		for (int32 i = 0; i < CurrentIds.Num(); ++i)
		{
			if (ObjectItems[i]->PackedId != CurrentIds[i])
			{
				bSetChanged = true;
				break;
			}
		}
	}

	// Build a lookup for existing items so we can reuse shared pointers
	TMap<uint64, TSharedPtr<FStringHeapObjectItem>> ExistingItems;
	if (bSetChanged)
	{
		for (const auto& Item : ObjectItems)
		{
			ExistingItems.Add(Item->PackedId, Item);
		}
		ObjectItems.Empty();
	}

	// Helper to update an item's stats from a heap
	auto UpdateItemStats = [&](TSharedPtr<FStringHeapObjectItem>& Item, const FusionCore::NetworkedStringHeap& Heap)
	{
		Item->HeapSize = Heap.HeapSize;
		Item->EntryCount = Heap.entryCount;
		uint32 UsedBytes = 0;
		for (uint32 i = 0; i < Heap.entryCount; ++i)
		{
			if (Heap.entries[i].alive)
			{
				UsedBytes += Heap.entries[i].size;
			}
		}
		Item->UsedBytes = UsedBytes;
	};

	if (bSetChanged)
	{
		// Rebuild the list, reusing existing TSharedPtrs where possible
		for (auto& [ObjectId, RootObject] : RootObjects)
		{
			if (!RootObject || !RootObject->GetHasValidData())
			{
				continue;
			}

			const uint64 PackedId = static_cast<uint64>(ObjectId);
			TSharedPtr<FStringHeapObjectItem>* Existing = ExistingItems.Find(PackedId);
			TSharedPtr<FStringHeapObjectItem> Item = Existing ? *Existing : MakeShared<FStringHeapObjectItem>();
			Item->PackedId = PackedId;
			Item->Origin = ObjectId.Origin;
			Item->Counter = ObjectId.Counter;

			FFusionObjectActorPair Pair = FusionClient->FindObjectPair(ObjectId);
			Item->Label = Pair.EngineObject ? Pair.EngineObject->GetName()
				: FString::Printf(TEXT("(%u:%u)"), ObjectId.Origin, ObjectId.Counter);

			UpdateItemStats(Item, RootObject->GetStringHeap());
			ObjectItems.Add(Item);

			if (FusionClient->GetClient()->HasSubObjects(RootObject))
			{
				for (const auto& SubId : FusionClient->GetClient()->GetSubObject(RootObject))
				{
					auto* SubObj = FusionClient->GetClient()->FindObject(SubId);
					if (!SubObj || !SubObj->GetHasValidData())
					{
						continue;
					}

					const uint64 SubPackedId = static_cast<uint64>(SubId);
					TSharedPtr<FStringHeapObjectItem>* SubExisting = ExistingItems.Find(SubPackedId);
					TSharedPtr<FStringHeapObjectItem> SubItem = SubExisting ? *SubExisting : MakeShared<FStringHeapObjectItem>();
					SubItem->PackedId = SubPackedId;
					SubItem->Origin = SubId.Origin;
					SubItem->Counter = SubId.Counter;

					FFusionObjectActorPair SubPair = FusionClient->FindObjectPair(SubId);
					SubItem->Label = SubPair.EngineObject
						? TEXT("  \u2514 ") + SubPair.EngineObject->GetName()
						: FString::Printf(TEXT("  \u2514 (%u:%u)"), SubId.Origin, SubId.Counter);

					UpdateItemStats(SubItem, SubObj->GetStringHeap());
					ObjectItems.Add(SubItem);
				}
			}
		}

		if (ObjectListView.IsValid())
		{
			ObjectListView->RequestListRefresh();

			// Restore selection without triggering OnSelectionChanged
			if (SelectedPackedId != 0)
			{
				for (const auto& Item : ObjectItems)
				{
					if (Item->PackedId == SelectedPackedId)
					{
						ObjectListView->SetSelection(Item, ESelectInfo::Direct);
						break;
					}
				}
			}
		}
	}
	else
	{
		// Same set of objects — just update stats in-place, no list rebuild
		int32 Idx = 0;
		for (auto& [ObjectId, RootObject] : RootObjects)
		{
			if (!RootObject || !RootObject->GetHasValidData())
			{
				continue;
			}
			if (Idx < ObjectItems.Num())
			{
				UpdateItemStats(ObjectItems[Idx], RootObject->GetStringHeap());
			}
			++Idx;

			if (FusionClient->GetClient()->HasSubObjects(RootObject))
			{
				for (const auto& SubId : FusionClient->GetClient()->GetSubObject(RootObject))
				{
					auto* SubObj = FusionClient->GetClient()->FindObject(SubId);
					if (!SubObj || !SubObj->GetHasValidData())
					{
						continue;
					}
					if (Idx < ObjectItems.Num())
					{
						UpdateItemStats(ObjectItems[Idx], SubObj->GetStringHeap());
					}
					++Idx;
				}
			}
		}

		// Just repaint rows, don't touch selection
		if (ObjectListView.IsValid())
		{
			ObjectListView->RequestListRefresh();
		}
	}
}

void SFusionStringHeapWindow::OnObjectSelected(TSharedPtr<FStringHeapObjectItem> Item, ESelectInfo::Type SelectInfo)
{
	if (Item.IsValid())
	{
		SelectedPackedId = Item->PackedId;
	}
	else
	{
		SelectedPackedId = 0;
		CurrentSnapshot = FStringHeapSnapshot();
		if (HeapBar.IsValid())
		{
			HeapBar->SetSnapshot(CurrentSnapshot);
		}
	}
}

void SFusionStringHeapWindow::CollectSnapshot(UFusionClient* FusionClient, uint64 PackedId)
{
	if (!FusionClient || !FusionClient->GetClient())
	{
		return;
	}

	FusionCore::ObjectId ObjId(PackedId);
	FusionCore::Object* Obj = FusionClient->GetClient()->FindObject(ObjId);
	if (!Obj)
	{
		CurrentSnapshot = FStringHeapSnapshot();
		return;
	}

	const FusionCore::NetworkedStringHeap& Heap = Obj->GetStringHeap();

	CurrentSnapshot.PackedObjectId = PackedId;
	CurrentSnapshot.HeapSize = Heap.HeapSize;
	CurrentSnapshot.EntryCount = Heap.entryCount;
	CurrentSnapshot.FreeSegmentCount = Heap.freeSegmentCount;
	CurrentSnapshot.Segments.Empty();
	CurrentSnapshot.UsedBytes = 0;
	CurrentSnapshot.AliveCount = 0;
	CurrentSnapshot.FreeBytes = 0;

	// Build sorted list of alive entries
	struct SortedEntry
	{
		uint32 Offset;
		uint32 Size;
		uint32 EntryIndex;
	};
	TArray<SortedEntry> AliveEntries;
	for (uint32 i = 0; i < Heap.entryCount; ++i)
	{
		if (Heap.entries[i].alive)
		{
			AliveEntries.Add({Heap.entries[i].offset, Heap.entries[i].size, i});
			CurrentSnapshot.UsedBytes += Heap.entries[i].size;
			CurrentSnapshot.AliveCount++;
		}
	}
	AliveEntries.Sort([](const SortedEntry& A, const SortedEntry& B) { return A.Offset < B.Offset; });

	CurrentSnapshot.FreeBytes = Heap.HeapSize - CurrentSnapshot.UsedBytes;

	// Build interleaved segment list (free gaps + alive entries)
	uint32 CurrentOffset = 0;
	for (const SortedEntry& Entry : AliveEntries)
	{
		// Insert free gap if there's space before this entry
		if (Entry.Offset > CurrentOffset)
		{
			FHeapSegment FreeSeg;
			FreeSeg.Offset = CurrentOffset;
			FreeSeg.Size = Entry.Offset - CurrentOffset;
			FreeSeg.bAlive = false;
			CurrentSnapshot.Segments.Add(FreeSeg);
		}

		// Insert alive entry
		FHeapSegment AliveSeg;
		AliveSeg.Offset = Entry.Offset;
		AliveSeg.Size = Entry.Size;
		AliveSeg.bAlive = true;
		AliveSeg.EntryId = Entry.EntryIndex + 1; // 1-based ID

		// Try to resolve the string for preview
		FusionCore::StringHandle Handle;
		Handle.id = Entry.EntryIndex + 1;
		Handle.generation = Heap.entries[Entry.EntryIndex].generation;
		FusionCore::StringMessage Status;
		const auto* Str = const_cast<FusionCore::NetworkedStringHeap&>(Heap).resolve_string(Handle, Status);
		if (Status == FusionCore::StringMessage::Valid && Str)
		{
			AliveSeg.ResolvedString = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Str)));
		}

		CurrentSnapshot.Segments.Add(AliveSeg);
		CurrentOffset = Entry.Offset + Entry.Size;
	}

	// Trailing free space
	if (CurrentOffset < Heap.HeapSize)
	{
		FHeapSegment TrailSeg;
		TrailSeg.Offset = CurrentOffset;
		TrailSeg.Size = Heap.HeapSize - CurrentOffset;
		TrailSeg.bAlive = false;
		CurrentSnapshot.Segments.Add(TrailSeg);
	}

	// Get label
	FFusionObjectActorPair Pair = FusionClient->FindObjectPair(ObjId);
	if (Pair.EngineObject)
	{
		CurrentSnapshot.ObjectLabel = Pair.EngineObject->GetName();
	}
	else
	{
		CurrentSnapshot.ObjectLabel = FString::Printf(TEXT("(%u:%u)"), ObjId.Origin, ObjId.Counter);
	}
}

// ─── Row widget ──────────────────────────────────────────────────────────────

class SStringHeapObjectRow : public SMultiColumnTableRow<TSharedPtr<FStringHeapObjectItem>>
{
public:
	SLATE_BEGIN_ARGS(SStringHeapObjectRow) {}
		SLATE_ARGUMENT(TSharedPtr<FStringHeapObjectItem>, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
	{
		Item = InArgs._Item;
		SMultiColumnTableRow::Construct(FSuperRowType::FArguments(), InOwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (!Item.IsValid())
		{
			return SNew(STextBlock).Text(FText::GetEmpty());
		}

		if (ColumnName == TEXT("Name"))
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Item->Label));
		}
		if (ColumnName == TEXT("ObjectId"))
		{
			return SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%u:%u"), Item->Origin, Item->Counter)));
		}
		if (ColumnName == TEXT("Heap"))
		{
			return SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%u B"), Item->HeapSize)));
		}
		if (ColumnName == TEXT("Used"))
		{
			const float Pct = Item->HeapSize > 0 ? (Item->UsedBytes * 100.0f / Item->HeapSize) : 0.0f;
			return SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%u B (%.0f%%)"), Item->UsedBytes, Pct)));
		}
		if (ColumnName == TEXT("Entries"))
		{
			return SNew(STextBlock)
				.Text(FText::AsNumber(static_cast<int32>(Item->EntryCount)));
		}

		return SNew(STextBlock).Text(FText::GetEmpty());
	}

private:
	TSharedPtr<FStringHeapObjectItem> Item;
};

TSharedRef<ITableRow> SFusionStringHeapWindow::GenerateObjectRow(TSharedPtr<FStringHeapObjectItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SStringHeapObjectRow, OwnerTable)
		.Item(Item);
}

#undef LOCTEXT_NAMESPACE
