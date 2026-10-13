using UnrealBuildTool;

public class RiftConnectionManager : ModuleRules
{
    public RiftConnectionManager(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "HTTP"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "Json",
                "JsonUtilities",
                "ImageWrapper"
            }
        );
    }
}
