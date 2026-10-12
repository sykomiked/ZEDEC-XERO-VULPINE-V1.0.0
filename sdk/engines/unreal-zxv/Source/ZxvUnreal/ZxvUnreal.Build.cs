// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZxvUnreal.Build.cs — the ZxvUnreal runtime module.
//
// Not yet compiled inside Unreal: this file is written for UE 5.4's
// UnrealBuildTool but this repository's CI has no Unreal Engine. The C++
// layer it compiles (ZxvCore/) is built and tested with CMake instead.
//
// The native libraries come from Source/ThirdParty/Zxv (git-ignored), which
// tools/stage_thirdparty.sh fills from `make -C sdk/libzxv` and
// `make -C sdk/engines/emu-state dist`:
//
//   Source/ThirdParty/Zxv/include/        zxv_api.h, zxv_emu.h
//   Source/ThirdParty/Zxv/lib/Linux/      libzxv.so, libzxv_emu.a
//   Source/ThirdParty/Zxv/lib/Mac/        libzxv.dylib, libzxv_emu.a
//
// Other plugins may depend on this module (PublicDependencyModuleNames
// "ZxvUnreal"): its public headers, including ZxvCore/zxv_ue_core.hpp and
// the libzxv / emu-state C headers, are exported.
using System.IO;
using UnrealBuildTool;

public class ZxvUnreal : ModuleRules
{
	public ZxvUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// The C++ layer never throws (it returns status codes), so the module
		// keeps Unreal's default of no C++ exceptions.
		bEnableExceptions = false;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		string ThirdPartyDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty", "Zxv"));
		string IncludeDir = Path.Combine(ThirdPartyDir, "include");
		if (!File.Exists(Path.Combine(IncludeDir, "zxv_api.h")) || !File.Exists(Path.Combine(IncludeDir, "zxv_emu.h")))
		{
			throw new BuildException("ZxvUnreal: " + IncludeDir + " is missing zxv_api.h / zxv_emu.h; run tools/stage_thirdparty.sh first");
		}
		PublicSystemIncludePaths.Add(IncludeDir);

		string LibDir;
		string SharedLib;
		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			LibDir = Path.Combine(ThirdPartyDir, "lib", "Linux");
			SharedLib = "libzxv.so";
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			LibDir = Path.Combine(ThirdPartyDir, "lib", "Mac");
			SharedLib = "libzxv.dylib";
		}
		else
		{
			// libzxv has no Windows, Android, iOS or console build in this
			// repository yet; refuse rather than link something untested.
			throw new BuildException("ZxvUnreal: no libzxv build for " + Target.Platform + " yet (Linux and Mac only)");
		}

		// emu-state is linked statically; libzxv is a shared library shipped
		// next to the module binary.
		PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libzxv_emu.a"));
		PublicAdditionalLibraries.Add(Path.Combine(LibDir, SharedLib));
		RuntimeDependencies.Add(Path.Combine("$(BinaryOutputDir)", SharedLib), Path.Combine(LibDir, SharedLib));
	}
}
