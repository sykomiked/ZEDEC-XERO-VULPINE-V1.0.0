// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZxvLibrary.h — Blueprint functions for exact rationals and hexagram
// tags. A function that can fail returns EZxvResult and writes its value
// only on Ok; LastError() gives the detail text. There is deliberately no
// "Make Rational from Float": values come from integers or exact text
// ("12.75", "3/4"). ToFloatLossy is the one way out to a float, for
// drawing only.
//
// Not yet compiled inside Unreal (written for UE 5.4; see README.md).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ZxvTypes.h"

#include "ZxvLibrary.generated.h"

UCLASS()
class ZXVUNREAL_API UZxvLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- diagnostics ----

	// Detail text of the last failure on this thread.
	UFUNCTION(BlueprintPure, Category = "ZXV")
	static FString LastError();

	// "ZXV_E_RANGE" for a result.
	UFUNCTION(BlueprintPure, Category = "ZXV")
	static FString ResultName(EZxvResult Result);

	// ---- rationals ----

	UFUNCTION(BlueprintCallable, Category = "ZXV|Rational")
	static EZxvResult MakeRational(int64 Num, int64 Den, FZxvRational& Out);

	// Exact text: "12.75", "-0.1", "3/4", "42". Exponents, "NaN" and spaces
	// are BadFormat; too large is OutOfRange.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Rational")
	static EZxvResult ParseRational(const FString& Text, FZxvRational& Out);

	UFUNCTION(BlueprintPure, Category = "ZXV|Rational")
	static FZxvRational RationalFromInt(int32 Value);

	UFUNCTION(BlueprintCallable, Category = "ZXV|Rational")
	static EZxvResult AddRational(const FZxvRational& A, const FZxvRational& B, FZxvRational& Out);

	UFUNCTION(BlueprintCallable, Category = "ZXV|Rational")
	static EZxvResult SubtractRational(const FZxvRational& A, const FZxvRational& B, FZxvRational& Out);

	UFUNCTION(BlueprintCallable, Category = "ZXV|Rational")
	static EZxvResult MultiplyRational(const FZxvRational& A, const FZxvRational& B, FZxvRational& Out);

	// B = 0 is InvalidArgument.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Rational")
	static EZxvResult DivideRational(const FZxvRational& A, const FZxvRational& B, FZxvRational& Out);

	UFUNCTION(BlueprintCallable, Category = "ZXV|Rational")
	static EZxvResult NegateRational(const FZxvRational& A, FZxvRational& Out);

	// -1, 0 or +1, exact (no epsilon). An invalid operand compares as 0 and
	// sets LastError.
	UFUNCTION(BlueprintPure, Category = "ZXV|Rational")
	static int32 CompareRational(const FZxvRational& A, const FZxvRational& B);

	UFUNCTION(BlueprintPure, Category = "ZXV|Rational", meta = (DisplayName = "Equal (Rational)", CompactNodeTitle = "=="))
	static bool EqualRational(const FZxvRational& A, const FZxvRational& B);

	UFUNCTION(BlueprintPure, Category = "ZXV|Rational", meta = (DisplayName = "Less (Rational)", CompactNodeTitle = "<"))
	static bool LessRational(const FZxvRational& A, const FZxvRational& B);

	// The exact text: a terminating decimal when one exists, else "n/d".
	UFUNCTION(BlueprintPure, Category = "ZXV|Rational", meta = (DisplayName = "To String (Rational)", CompactNodeTitle = "->"))
	static FString RationalToString(const FZxvRational& A);

	// Always "n/d".
	UFUNCTION(BlueprintPure, Category = "ZXV|Rational")
	static FString RationalToFractionString(const FZxvRational& A);

	UFUNCTION(BlueprintPure, Category = "ZXV|Rational")
	static int64 FloorRational(const FZxvRational& A);

	UFUNCTION(BlueprintPure, Category = "ZXV|Rational")
	static int64 CeilRational(const FZxvRational& A);

	// floor(A * Scale) as an integer, e.g. centimetres from metres with
	// Scale 100. Exact; OutOfRange if it does not fit.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Rational")
	static EZxvResult RationalToScaledInt(const FZxvRational& A, int64 Scale, int64& Out);

	// THE FLOAT EDGE. The nearest double, for a transform or a widget only.
	// It is lossy: never store it in game state and never send it to a peer.
	UFUNCTION(BlueprintPure, Category = "ZXV|Rendering", meta = (ToolTip = "Lossy. For drawing only; never feed it back into the simulation or over the network."))
	static double ToFloatLossy(const FZxvRational& A);

	// ---- hexagrams ----

	// Base, polarity and phase are each 0..3 (ZXV_HEX_* constants).
	UFUNCTION(BlueprintCallable, Category = "ZXV|Hexagram")
	static EZxvResult MakeHexagram(int32 Base, int32 Polarity, int32 Phase, FZxvHexagram& Out);

	UFUNCTION(BlueprintCallable, Category = "ZXV|Hexagram")
	static EZxvResult HexagramFromTag(int32 Tag, FZxvHexagram& Out);

	// The base from a four-valued Truth (never a cast: TRUE is base 3).
	UFUNCTION(BlueprintCallable, Category = "ZXV|Hexagram")
	static EZxvResult HexagramFromTruth(EZxvTruth Truth, int32 Polarity, int32 Phase, FZxvHexagram& Out);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static int32 HexagramBase(const FZxvHexagram& H);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static int32 HexagramPolarity(const FZxvHexagram& H);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static int32 HexagramPhase(const FZxvHexagram& H);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static EZxvTruth HexagramToTruth(const FZxvHexagram& H);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static bool HexagramIsForbidden(const FZxvHexagram& H);

	// ZXV_HEX_ACT_*: 0 proceed, 1 wait, 2 refuse, 3 escalate, 4 rollback.
	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static int32 HexagramAction(const FZxvHexagram& H);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static FString HexagramName(const FZxvHexagram& H);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static FString HexagramActionName(const FZxvHexagram& H);

	// The same or the next phase only; a jump is WrongState.
	UFUNCTION(BlueprintCallable, Category = "ZXV|Hexagram")
	static EZxvResult HexagramStep(const FZxvHexagram& H, int32 Phase, FZxvHexagram& Out);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static FZxvHexagram HexagramAdvance(const FZxvHexagram& H);

	// A forbidden tag stays forbidden (WrongState).
	UFUNCTION(BlueprintCallable, Category = "ZXV|Hexagram")
	static EZxvResult HexagramWithState(const FZxvHexagram& H, int32 Base, int32 Polarity, FZxvHexagram& Out);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static FZxvHexagram HexagramForbid(const FZxvHexagram& H);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static FZxvHexagram HexagramAnd(const FZxvHexagram& A, const FZxvHexagram& B);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static FZxvHexagram HexagramOr(const FZxvHexagram& A, const FZxvHexagram& B);

	UFUNCTION(BlueprintPure, Category = "ZXV|Hexagram")
	static FZxvHexagram HexagramNot(const FZxvHexagram& A);
};
