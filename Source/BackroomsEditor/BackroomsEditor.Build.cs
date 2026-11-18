using UnrealBuildTool;

public class BackroomsEditor : ModuleRules
{
	public BackroomsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Backrooms" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "BlueprintGraph", "KismetCompiler", "MeshDescription", "StaticMeshDescription", "AssetRegistry" });
	}
}
