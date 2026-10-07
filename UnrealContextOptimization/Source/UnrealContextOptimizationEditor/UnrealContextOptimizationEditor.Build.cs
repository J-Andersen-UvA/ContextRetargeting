using UnrealBuildTool;

public class UnrealContextOptimizationEditor : ModuleRules
{
    public UnrealContextOptimizationEditor(ReadOnlyTargetRules target) : base(target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        bEnableExceptions = true;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            });

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "UnrealEd",
                "PropertyEditor",
                "Slate",
                "SlateCore",
                "Json",
                "Projects",
                "DesktopPlatform",
                "IKRig",
                "IKRigEditor",
                "AssetRegistry",
                "AnimationDataController",
                "ContextRetargetingCore",
                "UnrealContextOptimization"
            });
    }
}
