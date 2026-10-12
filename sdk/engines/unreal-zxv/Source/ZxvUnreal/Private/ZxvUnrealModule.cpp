// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
#include "ZxvUnrealModule.h"

#include "Modules/ModuleManager.h"
#include "ZxvCore/zxv_ue_core.hpp"

DEFINE_LOG_CATEGORY(LogZxv);

namespace
{
bool GZxvLibraryUsable = false;
}

void FZxvUnrealModule::StartupModule()
{
	const int32 Status = zxvue::check_library();
	GZxvLibraryUsable = Status == ZXV_OK;
	if (GZxvLibraryUsable)
	{
		uint32_t Major = 0, Minor = 0, Patch = 0;
		zxv_version(&Major, &Minor, &Patch);
		UE_LOG(LogZxv, Log, TEXT("ZxvUnreal %d.%d over libzxv %u.%u.%u"), zxvue::kVersionMajor,
			zxvue::kVersionMinor, Major, Minor, Patch);
	}
	else
	{
		UE_LOG(LogZxv, Error, TEXT("ZxvUnreal: libzxv is not usable: %s"),
			UTF8_TO_TCHAR(zxvue::last_error().c_str()));
	}
}

void FZxvUnrealModule::ShutdownModule()
{
	GZxvLibraryUsable = false;
}

bool FZxvUnrealModule::IsLibraryUsable()
{
	return GZxvLibraryUsable;
}

IMPLEMENT_MODULE(FZxvUnrealModule, ZxvUnreal)
