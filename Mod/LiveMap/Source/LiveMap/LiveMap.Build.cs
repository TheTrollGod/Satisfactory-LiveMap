using System.IO;
using UnrealBuildTool;

public class LiveMap : ModuleRules
{
	public LiveMap(ReadOnlyTargetRules Target) : base(Target)
	{
		CppStandard = CppStandardVersion.Cpp20;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bLegacyPublicIncludePaths = false;

		// SML re-exports FactoryGame and its transitive dependencies.
		PublicDependencyModuleNames.AddRange(new[] {
			"Core",
			"CoreUObject",
			"Engine",
			// FGWheeledVehicle.h pulls in Chaos vehicle headers.
			"ChaosVehiclesCore",
			"ChaosVehicles",
			"FactoryGame",
			"SML",
		});

		// Terrain export: render-target readback and JPEG encoding.
		PrivateDependencyModuleNames.AddRange(new[] {
			"RenderCore",
			"ImageWrapper",
		});

		// Engine-free core (Private/Core) and vendored civetweb, compiled from
		// Private/Core/Civetweb.c. ws2_32 is pulled in by a #pragma on Windows.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "Core"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "civetweb", "include"));
	}
}
