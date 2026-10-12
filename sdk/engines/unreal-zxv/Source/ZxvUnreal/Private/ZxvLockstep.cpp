// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
#include "ZxvLockstep.h"

#include <vector>

namespace
{
EZxvResult R(int32 Status)
{
	return ZxvUnreal::ToResult(Status);
}

bool ValidIndex(int32 V)
{
	return V >= 0;
}

std::vector<uint8_t> ToVector(const TArray<uint8>& A)
{
	return std::vector<uint8_t>(A.GetData(), A.GetData() + A.Num());
}

void ToArray(const std::vector<uint8_t>& V, TArray<uint8>& Out)
{
	Out.SetNumUninitialized(static_cast<int32>(V.size()));
	if (!V.empty()) FMemory::Memcpy(Out.GetData(), V.data(), V.size());
}
} // namespace

EZxvResult UZxvLockstep::Setup(const FZxvLockstepConfig& Config)
{
	zxvue::LockstepConfig C;
	int32 Status = Config.ToCore(&C);
	if (Status == ZXV_OK) Status = Sim.create(C);
	if (Status == ZXV_OK) Pacer.reset();
	return R(Status);
}

EZxvResult UZxvLockstep::SetTickBudget(const FZxvRational& Limit)
{
	zxvue::Rational L;
	int32 Status = Limit.ToCore(&L);
	if (Status == ZXV_OK) Status = Sim.set_tick_budget(L);
	return R(Status);
}

EZxvResult UZxvLockstep::Start()
{
	return R(Sim.start());
}

EZxvResult UZxvLockstep::SetStep(const FZxvRational& Seconds, int32 MaxCatchup)
{
	zxvue::Rational S;
	if (MaxCatchup < 0) return EZxvResult::InvalidArgument;
	int32 Status = Seconds.ToCore(&S);
	if (Status == ZXV_OK) Status = Sim.clock_set_step(S, static_cast<uint32_t>(MaxCatchup));
	return R(Status);
}

EZxvResult UZxvLockstep::FeedMicroseconds(int64 Microseconds, int32& Due, int64& Dropped)
{
	uint32_t D = 0;
	uint64_t X = 0;
	const int32 Status = Sim.clock_feed_usec(Microseconds, &D, &X);
	if (Status == ZXV_OK)
	{
		Due = static_cast<int32>(D);
		Dropped = static_cast<int64>(X);
	}
	return R(Status);
}

EZxvResult UZxvLockstep::PumpClock(int32& Due, int64& Dropped)
{
	uint32_t D = 0;
	uint64_t X = 0;
	const int32 Status = Pacer.pump(Sim, zxvue::monotonic_usec(), &D, &X);
	if (Status == ZXV_OK)
	{
		Due = static_cast<int32>(D);
		Dropped = static_cast<int64>(X);
	}
	return R(Status);
}

EZxvResult UZxvLockstep::ClockAlpha(FZxvRational& Alpha) const
{
	zxvue::Rational A;
	const int32 Status = Sim.clock_alpha(&A);
	if (Status == ZXV_OK) Alpha = FZxvRational::FromCore(A);
	return R(Status);
}

EZxvResult UZxvLockstep::SubmitInput(int32 Player, int64 Tick, const TArray<uint8>& Bytes)
{
	if (!ValidIndex(Player) || Tick < 0) return EZxvResult::InvalidArgument;
	return R(Sim.submit_input(static_cast<uint32_t>(Player), static_cast<uint64_t>(Tick), ToVector(Bytes)));
}

EZxvResult UZxvLockstep::GetInput(int32 Player, TArray<uint8>& Bytes, bool& bPredicted) const
{
	if (!ValidIndex(Player)) return EZxvResult::InvalidArgument;
	std::vector<uint8_t> V;
	bool P = false;
	const int32 Status = Sim.input(static_cast<uint32_t>(Player), &V, &P);
	if (Status == ZXV_OK)
	{
		ToArray(V, Bytes);
		bPredicted = P;
	}
	return R(Status);
}

zxvue::Lockstep::Step UZxvLockstep::WrapStep(const FZxvStep& Step)
{
	return [this, Step](zxvue::Lockstep&, uint64_t Tick) -> int32_t {
		if (!Step.IsBound()) return ZXV_E_ARG;
		return ZxvUnreal::ToStatus(Step.Execute(this, static_cast<int64>(Tick)));
	};
}

EZxvResult UZxvLockstep::Advance(const FZxvStep& Step)
{
	if (!Step.IsBound()) return EZxvResult::InvalidArgument;
	return R(Sim.advance(WrapStep(Step)));
}

EZxvResult UZxvLockstep::BeginTick()
{
	return R(Sim.begin());
}

EZxvResult UZxvLockstep::CommitTick()
{
	return R(Sim.commit());
}

EZxvResult UZxvLockstep::AbortTick()
{
	return R(Sim.abort());
}

EZxvResult UZxvLockstep::GetRational(int32 Slot, FZxvRational& Value) const
{
	if (!ValidIndex(Slot)) return EZxvResult::InvalidArgument;
	zxvue::Rational V;
	const int32 Status = Sim.get_rational(static_cast<uint32_t>(Slot), &V);
	if (Status == ZXV_OK) Value = FZxvRational::FromCore(V);
	return R(Status);
}

EZxvResult UZxvLockstep::SetRational(int32 Slot, const FZxvRational& Value)
{
	if (!ValidIndex(Slot)) return EZxvResult::InvalidArgument;
	zxvue::Rational V;
	int32 Status = Value.ToCore(&V);
	if (Status == ZXV_OK) Status = Sim.set_rational(static_cast<uint32_t>(Slot), V);
	return R(Status);
}

EZxvResult UZxvLockstep::AddRational(int32 Slot, const FZxvRational& Value)
{
	if (!ValidIndex(Slot)) return EZxvResult::InvalidArgument;
	zxvue::Rational V;
	int32 Status = Value.ToCore(&V);
	if (Status == ZXV_OK) Status = Sim.add_rational(static_cast<uint32_t>(Slot), V);
	return R(Status);
}

EZxvResult UZxvLockstep::MultiplyRational(int32 Slot, const FZxvRational& Value)
{
	if (!ValidIndex(Slot)) return EZxvResult::InvalidArgument;
	zxvue::Rational V;
	int32 Status = Value.ToCore(&V);
	if (Status == ZXV_OK) Status = Sim.mul_rational(static_cast<uint32_t>(Slot), V);
	return R(Status);
}

EZxvResult UZxvLockstep::GetHexagram(int32 Slot, FZxvHexagram& Value) const
{
	if (!ValidIndex(Slot)) return EZxvResult::InvalidArgument;
	zxvue::Hexagram H;
	const int32 Status = Sim.get_hexagram(static_cast<uint32_t>(Slot), &H);
	if (Status == ZXV_OK) Value = FZxvHexagram::FromCore(H);
	return R(Status);
}

EZxvResult UZxvLockstep::SetHexagram(int32 Slot, const FZxvHexagram& Value)
{
	if (!ValidIndex(Slot)) return EZxvResult::InvalidArgument;
	zxvue::Hexagram H;
	int32 Status = Value.ToCore(&H);
	if (Status == ZXV_OK) Status = Sim.set_hexagram(static_cast<uint32_t>(Slot), H);
	return R(Status);
}

EZxvResult UZxvLockstep::Charge(const FZxvRational& Amount)
{
	zxvue::Rational A;
	int32 Status = Amount.ToCore(&A);
	if (Status == ZXV_OK) Status = Sim.charge(A);
	return R(Status);
}

EZxvResult UZxvLockstep::StateHash(int64 Tick, TArray<uint8>& Hash, bool& bFinal) const
{
	if (Tick < 0) return EZxvResult::InvalidArgument;
	std::vector<uint8_t> H;
	bool F = false;
	const int32 Status = Sim.hash(static_cast<uint64_t>(Tick), &H, &F);
	if (Status == ZXV_OK)
	{
		ToArray(H, Hash);
		bFinal = F;
	}
	return R(Status);
}

int64 UZxvLockstep::ConfirmedTick()
{
	return static_cast<int64>(Sim.confirmed_tick());
}

EZxvResult UZxvLockstep::CompareHash(int64 Tick, const TArray<uint8>& PeerHash, EZxvTruth& Verdict)
{
	if (Tick < 0) return EZxvResult::InvalidArgument;
	int Lp = ZXV_LP_NEITHER;
	const int32 Status = Sim.compare(static_cast<uint64_t>(Tick), ToVector(PeerHash), &Lp);
	if (Status == ZXV_OK) Verdict = static_cast<EZxvTruth>(Lp);
	return R(Status);
}

bool UZxvLockstep::HasDesync(int64& Tick) const
{
	uint64_t T = 0;
	if (!Sim.desync(&T)) return false;
	Tick = static_cast<int64>(T);
	return true;
}

bool UZxvLockstep::HasPendingRollback(int64& Tick) const
{
	uint64_t T = 0;
	if (!Sim.rollback_pending(&T)) return false;
	Tick = static_cast<int64>(T);
	return true;
}

EZxvResult UZxvLockstep::Rollback(int64 ToTick)
{
	if (ToTick < 0) return EZxvResult::InvalidArgument;
	return R(Sim.rollback(static_cast<uint64_t>(ToTick)));
}

EZxvResult UZxvLockstep::RollbackReplay(const FZxvStep& Step)
{
	if (!Step.IsBound()) return EZxvResult::InvalidArgument;
	return R(Sim.rollback_replay(WrapStep(Step)));
}

EZxvResult UZxvLockstep::ExportState(TArray<uint8>& Bytes) const
{
	std::vector<uint8_t> V;
	const int32 Status = Sim.export_state(&V);
	if (Status == ZXV_OK) ToArray(V, Bytes);
	return R(Status);
}

EZxvResult UZxvLockstep::ImportState(const TArray<uint8>& Bytes)
{
	return R(Sim.import_state(ToVector(Bytes)));
}

void UZxvLockstep::BeginDestroy()
{
	// Releases the sim, then its private budget context (R6 order).
	Sim = zxvue::Lockstep();
	Super::BeginDestroy();
}
