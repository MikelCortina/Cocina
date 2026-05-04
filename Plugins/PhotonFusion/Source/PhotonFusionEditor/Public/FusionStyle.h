// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * Defines and manages custom Slate styles (icons, brushes, etc.) for the Fusion plugin.
 */
class FFusionStyle
{
public:

	static void Initialize();

	static void Shutdown();

	static void ReloadTextures();

	static const ISlateStyle& Get();

	static FName GetStyleSetName();

private:
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};