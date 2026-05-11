// Copyright Exit Games GmbH. All Rights Reserved.

#include "NetworkDebugger/SFusionDebugTabManager.h"
#include "NetworkDebugger/FusionBandwidthDataCollector.h"
#include "NetworkDebugger/SFusionBandwidthWindow.h"
#include "NetworkDebugger/SFusionStringHeapWindow.h"
#include "FusionOnlineSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "FusionDebugTabManager"

void SFusionDebugTabManager::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(RootBox, SVerticalBox)
	];

	RebuildTabs();

	RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SFusionDebugTabManager::ScanForWorlds));
}

EActiveTimerReturnType SFusionDebugTabManager::ScanForWorlds(double InCurrentTime, float InDeltaTime)
{
	if (!GEngine)
	{
		return EActiveTimerReturnType::Continue;
	}

	// Collect current set of valid worlds with a Fusion subsystem
	TArray<UWorld*> CurrentWorlds;
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
		if (Subsystem)
		{
			CurrentWorlds.Add(World);
		}
	}

	// Check if anything changed
	bool bChanged = CurrentWorlds.Num() != WorldTabs.Num();
	if (!bChanged)
	{
		for (int32 i = 0; i < WorldTabs.Num(); ++i)
		{
			if (!WorldTabs[i].World.IsValid() || WorldTabs[i].World.Get() != CurrentWorlds[i])
			{
				bChanged = true;
				break;
			}
		}
	}

	if (bChanged)
	{
		RebuildTabs();
	}

	return EActiveTimerReturnType::Continue;
}

void SFusionDebugTabManager::RebuildTabs()
{
	// Collect worlds
	TArray<UWorld*> CurrentWorlds;
	if (GEngine)
	{
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
			if (Subsystem)
			{
				CurrentWorlds.Add(World);
			}
		}
	}

	WorldTabs.Empty();
	RootBox->ClearChildren();

	if (CurrentWorlds.Num() == 0)
	{
		// No instances — show placeholder
		RootBox->AddSlot()
			.FillHeight(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoInstances", "No active Fusion instances"))
				.Justification(ETextJustify::Center)
			];
		return;
	}

	// Determine client numbering: count the client-mode worlds sequentially
	int32 ClientCounter = 0;
	for (UWorld* World : CurrentWorlds)
	{
		int32 ClientIndex = 0;
		if (World->GetNetMode() == NM_Client)
		{
			++ClientCounter;
			ClientIndex = ClientCounter;
		}

		FWorldTab Tab;
		Tab.World = World;
		Tab.Label = MakeTabLabel(World, ClientIndex);
		Tab.Collector = MakeShared<FFusionBandwidthDataCollector>();
		Tab.Collector->SetTargetWorld(World);

		WorldTabs.Add(MoveTemp(Tab));
	}

	// Build tab bar
	TabBar = SNew(SHorizontalBox);
	for (int32 i = 0; i < WorldTabs.Num(); ++i)
	{
		TabBar->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(WorldTabs[i].Label))
				.OnClicked_Lambda([this, i]()
				{
					SetActiveTab(i);
					return FReply::Handled();
				})
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
			];
	}

	// Build content switcher
	ContentSwitcher = SNew(SWidgetSwitcher);
	for (int32 i = 0; i < WorldTabs.Num(); ++i)
	{
		TSharedRef<SFusionBandwidthWindow> BandwidthWindow = SNew(SFusionBandwidthWindow)
			.DataCollector(WorldTabs[i].Collector);

		TSharedPtr<SFusionStringHeapWindow> StringHeapWindow;
		SAssignNew(StringHeapWindow, SFusionStringHeapWindow);
		StringHeapWindow->SetTargetWorld(WorldTabs[i].World.Get());
		WorldTabs[i].StringHeapWidget = StringHeapWindow;

		// Build tool switcher first so we can capture it in lambdas
		TSharedPtr<SWidgetSwitcher> ToolSwitcher = SNew(SWidgetSwitcher)
			+ SWidgetSwitcher::Slot() [ BandwidthWindow ]
			+ SWidgetSwitcher::Slot() [ StringHeapWindow.ToSharedRef() ];

		// Build tool tab bar with captured switcher
		TSharedPtr<SHorizontalBox> ToolTabBar;
		SAssignNew(ToolTabBar, SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("BandwidthTool", "Bandwidth"))
				.OnClicked_Lambda([ToolSwitcher, ToolTabBar]()
				{
					if (ToolSwitcher.IsValid())
					{
						ToolSwitcher->SetActiveWidgetIndex(0);
					}
					if (ToolTabBar.IsValid() && ToolTabBar->NumSlots() >= 2)
					{
						static_cast<SButton*>(&ToolTabBar->GetSlot(0).GetWidget().Get())->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"));
						static_cast<SButton*>(&ToolTabBar->GetSlot(1).GetWidget().Get())->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"));
					}
					return FReply::Handled();
				})
				.ButtonStyle(FAppStyle::Get(), "Button")
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("StringHeapTool", "String Heap"))
				.OnClicked_Lambda([ToolSwitcher, ToolTabBar]()
				{
					if (ToolSwitcher.IsValid())
					{
						ToolSwitcher->SetActiveWidgetIndex(1);
					}
					if (ToolTabBar.IsValid() && ToolTabBar->NumSlots() >= 2)
					{
						static_cast<SButton*>(&ToolTabBar->GetSlot(0).GetWidget().Get())->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"));
						static_cast<SButton*>(&ToolTabBar->GetSlot(1).GetWidget().Get())->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"));
					}
					return FReply::Handled();
				})
				.ButtonStyle(FAppStyle::Get(), "FlatButton")
			];

		// Assemble content widget
		WorldTabs[i].ContentWidget = SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f, 2.0f)
			[
				ToolTabBar.ToSharedRef()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				ToolSwitcher.ToSharedRef()
			];

		ContentSwitcher->AddSlot()
			[
				WorldTabs[i].ContentWidget.ToSharedRef()
			];
	}

	// Tab bar row
	RootBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 0.0f)
		[
			TabBar.ToSharedRef()
		];

	// Separator
	RootBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 0.0f)
		[
			SNew(SSeparator)
		];

	// Content
	RootBox->AddSlot()
		.FillHeight(1.0f)
		[
			ContentSwitcher.ToSharedRef()
		];

	// Clamp and apply active tab
	ActiveTabIndex = FMath::Clamp(ActiveTabIndex, 0, FMath::Max(0, WorldTabs.Num() - 1));
	SetActiveTab(ActiveTabIndex);
}

void SFusionDebugTabManager::SetActiveTab(int32 Index)
{
	if (WorldTabs.Num() == 0)
	{
		return;
	}

	ActiveTabIndex = FMath::Clamp(Index, 0, WorldTabs.Num() - 1);

	if (ContentSwitcher.IsValid())
	{
		ContentSwitcher->SetActiveWidgetIndex(ActiveTabIndex);
	}

	// Update button styles to show active state
	if (TabBar.IsValid())
	{
		for (int32 i = 0; i < TabBar->NumSlots(); ++i)
		{
			TSharedRef<SWidget> Child = TabBar->GetSlot(i).GetWidget();
			if (SButton* Button = static_cast<SButton*>(&Child.Get()))
			{
				const FName StyleName = (i == ActiveTabIndex)
					? FName("Button")
					: FName("FlatButton");
				Button->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(StyleName));
			}
		}
	}
}

FString SFusionDebugTabManager::MakeTabLabel(UWorld* World, int32 ClientIndex)
{
	if (!World)
	{
		return TEXT("Unknown");
	}

	switch (World->GetNetMode())
	{
	case NM_DedicatedServer:
		return TEXT("Dedicated Server");
	case NM_ListenServer:
		return TEXT("Listen Server");
	case NM_Client:
		return FString::Printf(TEXT("Client %d"), ClientIndex);
	case NM_Standalone:
		return TEXT("Standalone");
	default:
		return TEXT("Unknown");
	}
}

#undef LOCTEXT_NAMESPACE
