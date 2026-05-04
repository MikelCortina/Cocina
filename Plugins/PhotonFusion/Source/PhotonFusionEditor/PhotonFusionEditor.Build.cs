// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using UnrealBuildTool;

public class PhotonFusionEditor : ModuleRules
{
	public PhotonFusionEditor(ReadOnlyTargetRules Target) : base(Target)
	{

		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		
		bWarningsAsErrors = true;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"BlueprintGraph", 
			"PhotonFusion",
		});
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"KismetWidgets",
				"PropertyEditor",
				"InputCore",
				"UnrealEd",
				"KismetCompiler", 
				"Kismet",
				"GraphEditor",
				"PhotonFusion",
				"Projects"
			}
		);
		
		PrivateIncludePathModuleNames.AddRange(new string[] {
			"BlueprintGraph",
			"KismetCompiler"
		});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.Add("EditorStyle");
		}
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
