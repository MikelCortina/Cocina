// Copyright Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"

class FFusionBandwidthDataCollector;
class SFusionStringHeapWindow;
class SWidgetSwitcher;
class UWorld;

/**
 * Wrapper widget that manages per-subsystem tabs for the Fusion debug tools.
 * Scans for PIE/Game worlds with active UFusionOnlineSubsystem instances and
 * creates a tab + bandwidth window + collector for each one.
 */
class SFusionDebugTabManager : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFusionDebugTabManager) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	struct FWorldTab
	{
		TWeakObjectPtr<UWorld> World;
		FString Label;
		TSharedPtr<FFusionBandwidthDataCollector> Collector;
		TSharedPtr<SWidget> ContentWidget;
		TSharedPtr<SFusionStringHeapWindow> StringHeapWidget;
	};

	EActiveTimerReturnType ScanForWorlds(double InCurrentTime, float InDeltaTime);
	void RebuildTabs();
	void SetActiveTab(int32 Index);
	static FString MakeTabLabel(UWorld* World, int32 ClientIndex);

	TArray<FWorldTab> WorldTabs;
	int32 ActiveTabIndex = 0;

	TSharedPtr<SHorizontalBox> TabBar;
	TSharedPtr<SWidgetSwitcher> ContentSwitcher;
	TSharedPtr<SVerticalBox> RootBox;
};
