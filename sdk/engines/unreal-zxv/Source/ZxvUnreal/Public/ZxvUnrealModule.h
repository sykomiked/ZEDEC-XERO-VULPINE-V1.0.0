// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZxvUnrealModule.h — the ZxvUnreal runtime module. On startup it checks
// the loaded libzxv (ABI 1.3 or later, major 1) and logs the result to
// LogZxv; a module that depends on ZxvUnreal can ask IsLibraryUsable().
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

ZXVUNREAL_API DECLARE_LOG_CATEGORY_EXTERN(LogZxv, Log, All);

class ZXVUNREAL_API FZxvUnrealModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// True when the loaded libzxv passed the version check at startup.
	static bool IsLibraryUsable();
};
