using UnrealBuildTool;

public class LiveMap : ModuleRules
{
	public LiveMap(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bLegacyPublicIncludePaths = false;

		// If you hit unresolved FactoryGame symbols, copy the full dependency
		// list from the Starter Project's ExampleMod Build.cs.
		PublicDependencyModuleNames.AddRange(new[] {
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
			"ChaosVehiclesCore",
			"ChaosVehicles",
			"FactoryGame",
			"SML",
		});

		// TODO(M2): vendor uWebSockets under Source/ThirdParty and add it here
		// (headers + uSockets/libuv/zlib static libs per platform: Win64, Linux).
	}
}
