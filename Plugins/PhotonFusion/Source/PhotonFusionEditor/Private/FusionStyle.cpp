// Copyright 2026 Exit Games GmbH. All Rights Reserved.


#include "FusionStyle.h"

#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Framework/Application/SlateApplication.h"

TSharedPtr<FSlateStyleSet> FFusionStyle::StyleInstance = nullptr;

void FFusionStyle::Initialize()
{
	if (StyleInstance.IsValid())
	{
		return;
	}

	StyleInstance = MakeShareable(new FSlateStyleSet("FusionStyle"));

	FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("PhotonFusion"))->GetBaseDir() / TEXT("Resources");
	StyleInstance->SetContentRoot(ContentDir);
	
	const FString ClassIconPath = ContentDir / TEXT("Icon16.png");
	const FVector2D ClassIconSize(16, 16);

	StyleInstance->Set("Fusion.Icon128", new FSlateImageBrush(ContentDir / TEXT("Icon128.png"), FVector2D(16,16)));
	StyleInstance->Set("ClassIcon.FusionActorComponent", new FSlateImageBrush(ClassIconPath, ClassIconSize));
	StyleInstance->Set("ClassIcon.FusionPhysicsReplicationComponent", new FSlateImageBrush(ClassIconPath, ClassIconSize));
	StyleInstance->Set("ClassIcon.FusionSpatialHashInterestComponent", new FSlateImageBrush(ClassIconPath, ClassIconSize));
	const FString ClassThumbnailPath = ContentDir / TEXT("Icon64.png");
	const FVector2D ClassThumbnailSize(64, 64);

	StyleInstance->Set("ClassThumbnail.FusionActorComponent", new FSlateImageBrush(ClassThumbnailPath, ClassThumbnailSize));
	StyleInstance->Set("ClassThumbnail.FusionPhysicsReplicationComponent", new FSlateImageBrush(ClassThumbnailPath, ClassThumbnailSize));
	StyleInstance->Set("ClassThumbnail.FusionSpatialHashInterestComponent", new FSlateImageBrush(ClassThumbnailPath, ClassThumbnailSize));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
}

void FFusionStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensure(StyleInstance.IsUnique());
		StyleInstance.Reset();
	}
}

void FFusionStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FFusionStyle::Get()
{
	return *StyleInstance;
}

FName FFusionStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("FusionStyle"));
	return StyleSetName;
}