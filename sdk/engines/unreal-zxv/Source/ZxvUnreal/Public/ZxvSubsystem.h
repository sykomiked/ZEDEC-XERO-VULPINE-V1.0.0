// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZxvSubsystem.h — UZxvSubsystem, one per game instance: it owns named
// lockstep sims and, for sims with bAutoPace, reads the monotonic clock in
// integer microseconds every frame and broadcasts OnTicksDue with how many
// fixed-step ticks are due. It does not advance sims itself: the game
// gathers inputs and calls Advance for each due tick.
//
// Extending it: the class is not final and its hooks are virtual, and the
// module exports it (ZXVUNREAL_API). A plugin that layers on ZxvUnreal adds
// "ZxvUnreal" to its PublicDependencyModuleNames and either derives from
// UZxvSubsystem or, better, adds its own subsystem that calls this one
// (GetGameInstance()->GetSubsystem<UZxvSubsystem>()). This kit holds no
// payment, checkout or voucher logic; that belongs in the layer above.
//
// Not yet compiled inside Unreal (written for UE 5.4; see README.md).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ZxvLockstep.h"
#include "ZxvTypes.h"

#include "ZxvSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FZxvTicksDue, FName, Name, UZxvLockstep*, Sim, int32, Due, int64, Dropped);
DECLARE_MULTICAST_DELEGATE_TwoParams(FZxvSimEvent, FName /*Name*/, UZxvLockstep* /*Sim*/);

UCLASS()
class ZXVUNREAL_API UZxvSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// ---- USubsystem ----
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- sims ----

	// Creates a sim under Name (Setup with Config); a name in use is
	// Duplicate. Returns null unless Result is Ok.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Subsystem")
	UZxvLockstep* CreateLockstep(FName Name, const FZxvLockstepConfig& Config, EZxvResult& Result);

	UFUNCTION(BlueprintPure, Category = "ZXV|Subsystem")
	UZxvLockstep* FindLockstep(FName Name) const;

	UFUNCTION(BlueprintCallable, Category = "ZXV|Subsystem")
	bool DestroyLockstep(FName Name);

	UFUNCTION(BlueprintPure, Category = "ZXV|Subsystem")
	TArray<FName> GetLockstepNames() const;

	// True when libzxv passed the version check at module startup.
	UFUNCTION(BlueprintPure, Category = "ZXV|Subsystem")
	static bool IsLibraryUsable();

	// Broadcast each frame for every auto-paced sim with Due > 0.
	UPROPERTY(BlueprintAssignable, Category = "ZXV|Subsystem")
	FZxvTicksDue OnTicksDue;

	// C++ hooks for layers built on this kit.
	FZxvSimEvent OnSimCreated;
	FZxvSimEvent OnSimDestroyed;

	// ---- FTickableGameObject ----
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UZxvSubsystem, STATGROUP_Tickables); }

protected:
	// The class of sim CreateLockstep makes; a derived subsystem may return
	// a subclass of UZxvLockstep.
	virtual TSubclassOf<UZxvLockstep> GetLockstepClass() const;

	// Called after a sim was created and set up / before it is released.
	virtual void HandleSimCreated(FName Name, UZxvLockstep* Sim);
	virtual void HandleSimDestroyed(FName Name, UZxvLockstep* Sim);

	// Pumps one auto-paced sim's clock and broadcasts; override to add
	// behaviour (keep the step itself deterministic).
	virtual void PaceSim(FName Name, UZxvLockstep* Sim);

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UZxvLockstep>> Sims;

private:
	bool bInitialized = false;
};
