using UnrealBuildTool;

public class ContextRetargetingCore : ModuleRules
{
    public ContextRetargetingCore(ReadOnlyTargetRules target) : base(target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        bEnableExceptions = true;

        PublicDependencyModuleNames.Add("Core");
    }
}
