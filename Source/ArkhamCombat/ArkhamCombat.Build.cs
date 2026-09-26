// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ArkhamCombat : ModuleRules
{
	public ArkhamCombat(ReadOnlyTargetRules Target) : base(Target)
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
			"MotionWarping",
			"Niagara",
			"Slate"
			
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"ArkhamCombat",
			"ArkhamCombat/Variant_Platforming",
			"ArkhamCombat/Variant_Platforming/Animation",
			"ArkhamCombat/Variant_Combat",
			"ArkhamCombat/Variant_Combat/AI",
			"ArkhamCombat/Variant_Combat/Animation",
			"ArkhamCombat/Variant_Combat/Gameplay",
			"ArkhamCombat/Variant_Combat/Interfaces",
			"ArkhamCombat/Variant_Combat/UI",
			"ArkhamCombat/Variant_SideScrolling",
			"ArkhamCombat/Variant_SideScrolling/AI",
			"ArkhamCombat/Variant_SideScrolling/Gameplay",
			"ArkhamCombat/Variant_SideScrolling/Interfaces",
			"ArkhamCombat/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
