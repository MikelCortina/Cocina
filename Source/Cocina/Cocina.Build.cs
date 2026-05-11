// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Cocina : ModuleRules
{
	public Cocina(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
            "PhotonFusion"
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Cocina",
			"Cocina/Variant_Platforming",
			"Cocina/Variant_Platforming/Animation",
			"Cocina/Variant_Combat",
			"Cocina/Variant_Combat/AI",
			"Cocina/Variant_Combat/Animation",
			"Cocina/Variant_Combat/Gameplay",
			"Cocina/Variant_Combat/Interfaces",
			"Cocina/Variant_Combat/UI",
			"Cocina/Variant_SideScrolling",
			"Cocina/Variant_SideScrolling/AI",
			"Cocina/Variant_SideScrolling/Gameplay",
			"Cocina/Variant_SideScrolling/Interfaces",
			"Cocina/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
