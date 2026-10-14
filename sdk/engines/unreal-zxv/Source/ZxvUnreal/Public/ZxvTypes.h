// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZxvTypes.h — the Blueprint value types: an exact rational, the 64-state
// hexagram tag, a lockstep shape, and result / truth enums. Every rule
// lives in ZxvCore/zxv_ue_core.hpp; these are thin wrappers.
//
// Not yet compiled inside Unreal (written for UE 5.4; see README.md).
#pragma once

#include "CoreMinimal.h"
#include "ZxvCore/zxv_ue_core.hpp"

#include <type_traits>

#include "ZxvTypes.generated.h"

// The outcome of a call. Ok is success; the rest name the libzxv status
// (ZXV_E_*). Other carries any status this enum does not list; the exact
// number and text are in UZxvLibrary::LastError().
UENUM(BlueprintType)
enum class EZxvResult : uint8
{
	Ok,
	InvalidArgument UMETA(ToolTip = "ZXV_E_ARG: null, out of range or zero denominator"),
	OutOfRange UMETA(ToolTip = "ZXV_E_RANGE: the exact value does not fit (overflow is refused, never wrapped)"),
	WrongState UMETA(ToolTip = "ZXV_E_STATE: wrong phase (setup vs ticking, inputs missing, ...)"),
	OverBudget UMETA(ToolTip = "ZXV_E_BUDGET: the tick's work would pass its budget"),
	BadFormat UMETA(ToolTip = "ZXV_E_FORMAT: malformed number text or state encoding"),
	Duplicate UMETA(ToolTip = "ZXV_E_DUPLICATE: a confirmed input resent with different bytes"),
	NotFound UMETA(ToolTip = "ZXV_E_NOT_FOUND: no such tick in the history"),
	Capacity UMETA(ToolTip = "ZXV_E_CAPACITY: the configuration needs too much memory"),
	OutOfMemory UMETA(ToolTip = "ZXV_E_NOMEM"),
	InvalidBudget UMETA(ToolTip = "ZXV_E_RMAG_INVALID: a negative or malformed budget"),
	Other UMETA(ToolTip = "any other ZXV_E_* status; see LastError")
};

// The four-valued Truth (ZXV_LP_*). Never cast to or from bool.
UENUM(BlueprintType)
enum class EZxvTruth : uint8
{
	Neither = 0 UMETA(ToolTip = "unknown or no verdict yet"),
	True = 1,
	False = 2,
	Both = 3 UMETA(ToolTip = "contradictory evidence, held")
};

namespace ZxvUnreal
{
// Status <-> result. ToStatus(Other) is ZXV_E_INTERNAL.
ZXVUNREAL_API EZxvResult ToResult(int32 Status);
ZXVUNREAL_API int32 ToStatus(EZxvResult Result);
} // namespace ZxvUnreal

// An exact rational: Num / Den. Stored as the two int64 the engine uses;
// a value typed into the editor is checked (and reduced) when it is used,
// so 2/4 works and 1/0 is refused with InvalidArgument.
USTRUCT(BlueprintType)
struct ZXVUNREAL_API FZxvRational
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ZXV")
	int64 Num = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ZXV", meta = (ClampMin = "1"))
	int64 Den = 1;

	FZxvRational() = default;

	// A float or double never converts: build from integers or exact text.
	template <typename F, typename = std::enable_if_t<std::is_floating_point<F>::value>>
	FZxvRational(F) = delete;

	static FZxvRational FromCore(const zxvue::Rational& V)
	{
		FZxvRational R;
		R.Num = V.num();
		R.Den = V.den();
		return R;
	}

	// Validates and reduces; InvalidArgument / OutOfRange on a bad pair.
	int32 ToCore(zxvue::Rational* Out) const { return zxvue::Rational::from_ints(Num, Den, Out); }

	bool operator==(const FZxvRational& B) const { return Num == B.Num && Den == B.Den; }
	bool operator!=(const FZxvRational& B) const { return !(*this == B); }
};

// The 64-state hexagram tag (base, polarity, phase); see UZxvLibrary.
USTRUCT(BlueprintType)
struct ZXVUNREAL_API FZxvHexagram
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ZXV", meta = (ClampMin = "0", ClampMax = "63"))
	uint8 Tag = 0;

	static FZxvHexagram FromCore(const zxvue::Hexagram& H)
	{
		FZxvHexagram R;
		R.Tag = static_cast<uint8>(H.tag());
		return R;
	}

	int32 ToCore(zxvue::Hexagram* Out) const { return zxvue::Hexagram::from_tag(Tag, Out); }

	bool operator==(const FZxvHexagram& B) const { return Tag == B.Tag; }
};

// The shape of one lockstep sim (emu-state limits: players 1..64, slots
// 0..65536 and not both 0, input bytes 1..256, history 2..4096).
USTRUCT(BlueprintType)
struct ZXVUNREAL_API FZxvLockstepConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ZXV", meta = (ClampMin = "1", ClampMax = "64"))
	int32 Players = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ZXV", meta = (ClampMin = "0"))
	int32 RationalSlots = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ZXV", meta = (ClampMin = "0"))
	int32 HexagramSlots = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ZXV", meta = (ClampMin = "1", ClampMax = "256"))
	int32 InputBytes = 1;

	// Ticks of snapshots kept (rollback depth and input window).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ZXV", meta = (ClampMin = "2", ClampMax = "4096"))
	int32 History = 32;

	// Predict missing inputs and roll back when they arrive (rollback
	// netcode); off is strict lockstep.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ZXV")
	bool bPredict = false;

	// InvalidArgument for a negative field.
	int32 ToCore(zxvue::LockstepConfig* Out) const;
};
