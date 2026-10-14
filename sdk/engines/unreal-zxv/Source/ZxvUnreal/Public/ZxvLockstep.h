// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZxvLockstep.h — UZxvLockstep, one emu-state lockstep sim as a UObject:
// rational and hexagram slots, per-player inputs per tick, a SHA3-256 hash
// per tick, rollback, desync checks, per-tick budgets, a fixed-step clock
// fed integer microseconds, and state transfer.
//
// The step (the game rules for one tick) is a Blueprint delegate or, from
// C++, any callable passed to Core().advance(). It must be a pure function
// of the sim's state and inputs: no float math on game state, no
// FMath::Rand, no world time, no physics results.
//
// Not yet compiled inside Unreal (written for UE 5.4; see README.md).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ZxvTypes.h"

#include "ZxvLockstep.generated.h"

class UZxvLockstep;

// The game rules for one tick. Return Ok to commit; anything else aborts
// the tick (the state is restored and the tick does not advance).
DECLARE_DYNAMIC_DELEGATE_RetVal_TwoParams(EZxvResult, FZxvStep, UZxvLockstep*, Sim, int64, Tick);

UCLASS(BlueprintType)
class ZXVUNREAL_API UZxvLockstep : public UObject
{
	GENERATED_BODY()

public:
	// ---- setup ----

	// Creates the sim (replacing any previous one).
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep")
	EZxvResult Setup(const FZxvLockstepConfig& Config);

	UFUNCTION(BlueprintPure, Category = "ZXV|Lockstep")
	bool IsValidSim() const { return Sim.valid(); }

	// An exact per-tick budget; Charge() inside a step consumes from it. The
	// sim owns a private libzxv context and a capability narrowed to BUDGET.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep")
	EZxvResult SetTickBudget(const FZxvRational& Limit);

	// Freezes the initial state as tick 0.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep")
	EZxvResult Start();

	// ---- the fixed-step clock (integer time only) ----

	// One tick lasts Seconds (e.g. 1/60); at most MaxCatchup ticks per feed.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Clock")
	EZxvResult SetStep(const FZxvRational& Seconds, int32 MaxCatchup);

	// Elapsed wall time in integer microseconds; never the float DeltaTime.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Clock")
	EZxvResult FeedMicroseconds(int64 Microseconds, int32& Due, int64& Dropped);

	// Reads the monotonic clock (integer microseconds) and feeds the time
	// since the previous call; the first call only sets the origin. The
	// subsystem calls this for sims with auto-pacing on.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Clock")
	EZxvResult PumpClock(int32& Due, int64& Dropped);

	// The fraction of a tick accumulated, in [0, 1): interpolate drawing
	// with ToFloatLossy(Alpha); it never feeds back.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Clock")
	EZxvResult ClockAlpha(FZxvRational& Alpha) const;

	// ---- inputs ----

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Input")
	EZxvResult SubmitInput(int32 Player, int64 Tick, const TArray<uint8>& Bytes);

	// Inside a step: the input Player contributes to this tick.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Input")
	EZxvResult GetInput(int32 Player, TArray<uint8>& Bytes, bool& bPredicted) const;

	UFUNCTION(BlueprintPure, Category = "ZXV|Lockstep")
	bool IsReady() const { return Sim.ready(); }

	// The last committed tick (0 right after Start).
	UFUNCTION(BlueprintPure, Category = "ZXV|Lockstep")
	int64 CurrentTick() const { return static_cast<int64>(Sim.tick()); }

	// ---- ticks ----

	// Begin, Step, then commit (Ok) or abort (anything else).
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep")
	EZxvResult Advance(const FZxvStep& Step);

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep")
	EZxvResult BeginTick();

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep")
	EZxvResult CommitTick();

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep")
	EZxvResult AbortTick();

	// ---- state (setters during setup or inside a step) ----

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|State")
	EZxvResult GetRational(int32 Slot, FZxvRational& Value) const;

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|State")
	EZxvResult SetRational(int32 Slot, const FZxvRational& Value);

	// Slot += Value exactly; OutOfRange leaves the slot unchanged.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|State")
	EZxvResult AddRational(int32 Slot, const FZxvRational& Value);

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|State")
	EZxvResult MultiplyRational(int32 Slot, const FZxvRational& Value);

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|State")
	EZxvResult GetHexagram(int32 Slot, FZxvHexagram& Value) const;

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|State")
	EZxvResult SetHexagram(int32 Slot, const FZxvHexagram& Value);

	// Inside a step: OverBudget once the tick's work would pass the limit.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|State")
	EZxvResult Charge(const FZxvRational& Amount);

	// ---- hashes, desync, rollback ----

	// The 32-byte SHA3-256 hash of a kept tick; bFinal when it can no
	// longer change. Only final hashes are worth sending to peers.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Sync")
	EZxvResult StateHash(int64 Tick, TArray<uint8>& Hash, bool& bFinal) const;

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Sync")
	int64 ConfirmedTick();

	// True (equal), False (a desync, recorded) or Neither (no verdict yet).
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Sync")
	EZxvResult CompareHash(int64 Tick, const TArray<uint8>& PeerHash, EZxvTruth& Verdict);

	UFUNCTION(BlueprintPure, Category = "ZXV|Lockstep|Sync")
	bool HasDesync(int64& Tick) const;

	UFUNCTION(BlueprintPure, Category = "ZXV|Lockstep|Sync")
	bool HasPendingRollback(int64& Tick) const;

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Sync")
	EZxvResult Rollback(int64 ToTick);

	// If a rollback is pending, roll back and re-run Step up to the current
	// tick; Ok and nothing done otherwise.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Sync")
	EZxvResult RollbackReplay(const FZxvStep& Step);

	// ---- state transfer (late join, resync) ----

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Sync")
	EZxvResult ExportState(TArray<uint8>& Bytes) const;

	UFUNCTION(BlueprintCallable, Category = "ZXV|Lockstep|Sync")
	EZxvResult ImportState(const TArray<uint8>& Bytes);

	// ---- C++ ----

	// The engine-neutral sim, for C++ steps: Core().advance([](zxvue::Lockstep& S, uint64_t T) {...}).
	zxvue::Lockstep& Core() { return Sim; }
	const zxvue::Lockstep& Core() const { return Sim; }

	// Auto-pacing: UZxvSubsystem calls PumpClock() every frame for this sim
	// and broadcasts OnTicksDue.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ZXV|Lockstep|Clock")
	bool bAutoPace = false;

	virtual void BeginDestroy() override;

protected:
	// Runs a Blueprint step through the core: the delegate's result becomes
	// the step's status.
	zxvue::Lockstep::Step WrapStep(const FZxvStep& Step);

private:
	zxvue::Lockstep Sim;
	zxvue::Pacer Pacer;
};
