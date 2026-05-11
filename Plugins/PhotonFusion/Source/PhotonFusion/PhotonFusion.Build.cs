// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class PhotonFusion : ModuleRules {
	
	public PhotonFusion(ReadOnlyTargetRules Target) : base(Target) {
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bWarningsAsErrors = true;
		
		string fusionConfig;
		switch (Target.Configuration)
		{
			case UnrealTargetConfiguration.Unknown:
			case UnrealTargetConfiguration.Debug:
			case UnrealTargetConfiguration.DebugGame:
				fusionConfig = "debug";
				break;

			case UnrealTargetConfiguration.Development:
			case UnrealTargetConfiguration.Test:
			case UnrealTargetConfiguration.Shipping:
				fusionConfig = "release";
				break;

			default:
				throw new ArgumentOutOfRangeException();
		}

		if (Target.Configuration == UnrealTargetConfiguration.Debug ||
		    Target.Configuration == UnrealTargetConfiguration.DebugGame ||
		    Target.Configuration == UnrealTargetConfiguration.Development)
		{
			PublicDefinitions.Add("PHOTON_DEBUG=1");
		}

		List<string> fusionArchs = new List<string>();
		
		string fusionPlatform;
		string libPrefix = "";
		string libExtension = "";
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			fusionPlatform = "windows";
			libExtension = ".lib";
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			fusionPlatform = "android";
			libPrefix = "lib";
			libExtension = ".a";
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			fusionPlatform = "darwin";
			libPrefix = "lib";
			libExtension = ".a";
			fusionArchs.Add("universal");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			fusionPlatform = "ios";
			libPrefix = "lib";
			libExtension = ".a";
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			fusionPlatform = "unix";
			libPrefix = "lib";
			libExtension = ".a";
		}
		else
		{
			throw new Exception("\nTarget platform not yet supported: " + Target.Platform);
		}

		if (fusionArchs.Count == 0)
		{
			for (int i = 0; i < Target.Architectures.Architectures.Count; i++)
			{
				if (Target.Architectures.Architectures[i] == UnrealArch.X64)
				{
					fusionArchs.Add("x86_64");
				}
				else if (Target.Architectures.Architectures[i] == UnrealArch.Arm64)
				{
					fusionArchs.Add("arm64-v8a");
				}
				else
				{
					throw new Exception("\nTarget architecture not yet supported: " + Target.Architecture);
				}
			}
		}

		List<string> photonLibraries = new List<string>() {"fusion", "photon_common", "photon_matchmaking"};
		
		foreach (string fusionArch in fusionArchs)
		{
			foreach (string library in photonLibraries)
			{
				string libSuffix = (fusionPlatform == "windows") ? "_md" : "";
				string libraryName = libPrefix + library + "_" + fusionPlatform + "_" + fusionArch + "_" + fusionConfig + libSuffix + libExtension;
				string libraryPath =
					Path.Combine(ModuleDirectory, "Fusion", "lib", fusionPlatform, fusionArch, libraryName);

				if (!File.Exists(libraryPath))
				{
					throw new Exception($"\nFusion library not found at: {libraryPath}");
				}

				PublicAdditionalLibraries.Add(libraryPath);
			}
		}
		
		LoadPhoton(Target);

		PublicIncludePaths.AddRange(
			new string[] {
				Path.Combine(ModuleDirectory)
			}
		);
		
		PrivateIncludePaths.AddRange(
			new string[] {
				Path.Combine(ModuleDirectory),
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"DeveloperSettings",
				"PhysicsCore"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"NetCore",
				"Json"
			}
		);
		
		if (Target.bBuildEditor) {
			PublicDependencyModuleNames.Add("UnrealEd");
		}

		DynamicallyLoadedModuleNames.AddRange(
			new string[0]
		);
	}
	
	private string PhotonPath
	{
		get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "Fusion", "deps", "realtime")); }
	}
	
	private void AddPhotonLibPathWin(ReadOnlyTargetRules Target, string name)
	{
		PublicAdditionalLibraries.Add(Path.Combine(PhotonPath, "lib", "windows", "x86_64", name + "-cpp_vc17_release_windows_md_x64.lib"));
	}

	private void AddPhotonLibPathAndroid(ReadOnlyTargetRules Target, string name)
	{
		for (int i = 0; i < Target.Architectures.Architectures.Count; i++)
		{
			if (Target.Architectures.Architectures[i] == UnrealArch.X64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PhotonPath, "lib", "android", "x86_64", "lib" + name + "_release_android_x86_64_no-rtti.a"));
			} 
			else if (Target.Architectures.Architectures[i] == UnrealArch.Arm64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PhotonPath, "lib", "android", "arm64-v8a", "lib" + name + "_release_android_arm64-v8a_no-rtti.a"));
			}
			else
			{
				throw new Exception("\nTarget architecture not yet supported: " + Target.Architecture);
			}
		}
	}

	private void AddPhotonLibPathIOS(ReadOnlyTargetRules Target, string name)
	{
		string archStr = (Target.Architecture == UnrealArch.IOSSimulator) ? "iphonesimulator" : "iphoneos";

		PublicAdditionalLibraries.Add(Path.Combine(PhotonPath, "lib", "ios", "arm64-v8a", "lib" + name + "_release_" + archStr + ".a"));
	}

	private void AddPhotonLibPathMac(ReadOnlyTargetRules Target, string name)
	{
		PublicAdditionalLibraries.Add(Path.Combine(PhotonPath, "lib", "darwin", "universal", "lib" + name + "_release_macosx.a"));
	}

	private void AddPhotonLibPathLinux(ReadOnlyTargetRules Target, string name)
	{
		PublicAdditionalLibraries.Add(Path.Combine(PhotonPath, "lib", "unix", "x86_64", "lib" + name + "Release64.a"));
	}
	
	public bool LoadPhoton(ReadOnlyTargetRules Target)
	{
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("_EG_WINDOWS_PLATFORM");
			AddPhotonLibPathWin(Target, "Common");
			AddPhotonLibPathWin(Target, "Photon");
			AddPhotonLibPathWin(Target, "LoadBalancing");
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicDefinitions.Add("_EG_ANDROID_PLATFORM");
			AddPhotonLibPathAndroid(Target, "common-cpp-static");
			AddPhotonLibPathAndroid(Target, "photon-cpp-static");
			AddPhotonLibPathAndroid(Target, "loadbalancing-cpp-static");
			AddPhotonLibPathAndroid(Target, "ssl");
			AddPhotonLibPathAndroid(Target, "websockets");
			AddPhotonLibPathAndroid(Target, "crypto");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicDefinitions.Add("_EG_IPHONE_PLATFORM");
			AddPhotonLibPathIOS(Target, "Common-cpp");
			AddPhotonLibPathIOS(Target, "Photon-cpp");
			AddPhotonLibPathIOS(Target, "LoadBalancing-cpp");
			AddPhotonLibPathIOS(Target, "crypto");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicDefinitions.Add("_EG_IMAC_PLATFORM");
			AddPhotonLibPathMac(Target, "Common-cpp");
			AddPhotonLibPathMac(Target, "Photon-cpp");
			AddPhotonLibPathMac(Target, "LoadBalancing-cpp");
			AddPhotonLibPathMac(Target, "crypto");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicDefinitions.Add("_EG_LINUX_PLATFORM");
			AddPhotonLibPathLinux(Target, "Common");
			AddPhotonLibPathLinux(Target, "Photon");
			AddPhotonLibPathLinux(Target, "LoadBalancing");
		}
		else
		{
			throw new Exception("\nTarget platform not supported: " + Target.Platform);
		}

		// Include path
		PublicIncludePaths.Add(Path.Combine(PhotonPath, "."));

		return true;
	}
}