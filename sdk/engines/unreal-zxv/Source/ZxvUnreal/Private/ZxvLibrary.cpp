// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
#include "ZxvLibrary.h"

namespace
{
using zxvue::Hexagram;
using zxvue::Rational;

// Both operands valid, then the operation; the result only on Ok.
template <typename Op>
EZxvResult BinOp(const FZxvRational& A, const FZxvRational& B, FZxvRational& Out, Op Fn)
{
	Rational X, Y, R;
	int32 Status = A.ToCore(&X);
	if (Status == ZXV_OK) Status = B.ToCore(&Y);
	if (Status == ZXV_OK) Status = Fn(X, Y, &R);
	if (Status == ZXV_OK) Out = FZxvRational::FromCore(R);
	return ZxvUnreal::ToResult(Status);
}

// An invalid stored tag (an editor value above 63) reads as tag 0.
Hexagram Hex(const FZxvHexagram& H)
{
	Hexagram Out;
	H.ToCore(&Out);
	return Out;
}

Rational RatOrZero(const FZxvRational& A)
{
	Rational Out;
	A.ToCore(&Out);
	return Out;
}

EZxvResult HexOut(int32 Status, const Hexagram& H, FZxvHexagram& Out)
{
	if (Status == ZXV_OK) Out = FZxvHexagram::FromCore(H);
	return ZxvUnreal::ToResult(Status);
}
} // namespace

FString UZxvLibrary::LastError()
{
	return FString(UTF8_TO_TCHAR(zxvue::last_error().c_str()));
}

FString UZxvLibrary::ResultName(EZxvResult Result)
{
	return FString(UTF8_TO_TCHAR(zxvue::status_name(ZxvUnreal::ToStatus(Result))));
}

EZxvResult UZxvLibrary::MakeRational(int64 Num, int64 Den, FZxvRational& Out)
{
	Rational R;
	const int32 Status = Rational::from_ints(Num, Den, &R);
	if (Status == ZXV_OK) Out = FZxvRational::FromCore(R);
	return ZxvUnreal::ToResult(Status);
}

EZxvResult UZxvLibrary::ParseRational(const FString& Text, FZxvRational& Out)
{
	Rational R;
	const int32 Status = Rational::parse(std::string(TCHAR_TO_UTF8(*Text)), &R);
	if (Status == ZXV_OK) Out = FZxvRational::FromCore(R);
	return ZxvUnreal::ToResult(Status);
}

FZxvRational UZxvLibrary::RationalFromInt(int32 Value)
{
	return FZxvRational::FromCore(Rational::of_int(Value));
}

EZxvResult UZxvLibrary::AddRational(const FZxvRational& A, const FZxvRational& B, FZxvRational& Out)
{
	return BinOp(A, B, Out, [](const Rational& X, const Rational& Y, Rational* R) { return X.add(Y, R); });
}

EZxvResult UZxvLibrary::SubtractRational(const FZxvRational& A, const FZxvRational& B, FZxvRational& Out)
{
	return BinOp(A, B, Out, [](const Rational& X, const Rational& Y, Rational* R) { return X.sub(Y, R); });
}

EZxvResult UZxvLibrary::MultiplyRational(const FZxvRational& A, const FZxvRational& B, FZxvRational& Out)
{
	return BinOp(A, B, Out, [](const Rational& X, const Rational& Y, Rational* R) { return X.mul(Y, R); });
}

EZxvResult UZxvLibrary::DivideRational(const FZxvRational& A, const FZxvRational& B, FZxvRational& Out)
{
	return BinOp(A, B, Out, [](const Rational& X, const Rational& Y, Rational* R) { return X.div(Y, R); });
}

EZxvResult UZxvLibrary::NegateRational(const FZxvRational& A, FZxvRational& Out)
{
	Rational X, R;
	int32 Status = A.ToCore(&X);
	if (Status == ZXV_OK) Status = X.neg(&R);
	if (Status == ZXV_OK) Out = FZxvRational::FromCore(R);
	return ZxvUnreal::ToResult(Status);
}

int32 UZxvLibrary::CompareRational(const FZxvRational& A, const FZxvRational& B)
{
	Rational X, Y;
	if (A.ToCore(&X) != ZXV_OK || B.ToCore(&Y) != ZXV_OK) return 0;
	return X.compare(Y);
}

bool UZxvLibrary::EqualRational(const FZxvRational& A, const FZxvRational& B)
{
	Rational X, Y;
	return A.ToCore(&X) == ZXV_OK && B.ToCore(&Y) == ZXV_OK && X == Y;
}

bool UZxvLibrary::LessRational(const FZxvRational& A, const FZxvRational& B)
{
	Rational X, Y;
	return A.ToCore(&X) == ZXV_OK && B.ToCore(&Y) == ZXV_OK && X < Y;
}

FString UZxvLibrary::RationalToString(const FZxvRational& A)
{
	return FString(UTF8_TO_TCHAR(RatOrZero(A).to_string().c_str()));
}

FString UZxvLibrary::RationalToFractionString(const FZxvRational& A)
{
	return FString(UTF8_TO_TCHAR(RatOrZero(A).to_fraction_string().c_str()));
}

int64 UZxvLibrary::FloorRational(const FZxvRational& A)
{
	return RatOrZero(A).floor();
}

int64 UZxvLibrary::CeilRational(const FZxvRational& A)
{
	return RatOrZero(A).ceil();
}

EZxvResult UZxvLibrary::RationalToScaledInt(const FZxvRational& A, int64 Scale, int64& Out)
{
	Rational X;
	int32 Status = A.ToCore(&X);
	int64_t V = 0;
	if (Status == ZXV_OK) Status = X.to_scaled_floor(Scale, &V);
	if (Status == ZXV_OK) Out = V;
	return ZxvUnreal::ToResult(Status);
}

// The only floating-point arithmetic in the plugin, at the drawing edge.
double UZxvLibrary::ToFloatLossy(const FZxvRational& A)
{
	const Rational X = RatOrZero(A);
	return static_cast<double>(X.num()) / static_cast<double>(X.den());
}

EZxvResult UZxvLibrary::MakeHexagram(int32 Base, int32 Polarity, int32 Phase, FZxvHexagram& Out)
{
	Hexagram H;
	return HexOut(Hexagram::from_fields(Base, Polarity, Phase, &H), H, Out);
}

EZxvResult UZxvLibrary::HexagramFromTag(int32 Tag, FZxvHexagram& Out)
{
	Hexagram H;
	return HexOut(Hexagram::from_tag(Tag, &H), H, Out);
}

EZxvResult UZxvLibrary::HexagramFromTruth(EZxvTruth Truth, int32 Polarity, int32 Phase, FZxvHexagram& Out)
{
	Hexagram H;
	return HexOut(Hexagram::from_truth(static_cast<int>(Truth), Polarity, Phase, &H), H, Out);
}

int32 UZxvLibrary::HexagramBase(const FZxvHexagram& H) { return Hex(H).base(); }
int32 UZxvLibrary::HexagramPolarity(const FZxvHexagram& H) { return Hex(H).polarity(); }
int32 UZxvLibrary::HexagramPhase(const FZxvHexagram& H) { return Hex(H).phase(); }
EZxvTruth UZxvLibrary::HexagramToTruth(const FZxvHexagram& H) { return static_cast<EZxvTruth>(Hex(H).to_truth()); }
bool UZxvLibrary::HexagramIsForbidden(const FZxvHexagram& H) { return Hex(H).is_forbidden(); }
int32 UZxvLibrary::HexagramAction(const FZxvHexagram& H) { return Hex(H).action(); }

FString UZxvLibrary::HexagramName(const FZxvHexagram& H)
{
	return FString(UTF8_TO_TCHAR(Hex(H).name()));
}

FString UZxvLibrary::HexagramActionName(const FZxvHexagram& H)
{
	return FString(UTF8_TO_TCHAR(Hex(H).action_name()));
}

EZxvResult UZxvLibrary::HexagramStep(const FZxvHexagram& H, int32 Phase, FZxvHexagram& Out)
{
	Hexagram R;
	return HexOut(Hex(H).step(Phase, &R), R, Out);
}

FZxvHexagram UZxvLibrary::HexagramAdvance(const FZxvHexagram& H) { return FZxvHexagram::FromCore(Hex(H).advance()); }

EZxvResult UZxvLibrary::HexagramWithState(const FZxvHexagram& H, int32 Base, int32 Polarity, FZxvHexagram& Out)
{
	Hexagram R;
	return HexOut(Hex(H).with_state(Base, Polarity, &R), R, Out);
}

FZxvHexagram UZxvLibrary::HexagramForbid(const FZxvHexagram& H) { return FZxvHexagram::FromCore(Hex(H).forbid()); }

FZxvHexagram UZxvLibrary::HexagramAnd(const FZxvHexagram& A, const FZxvHexagram& B)
{
	return FZxvHexagram::FromCore(Hex(A).and_(Hex(B)));
}

FZxvHexagram UZxvLibrary::HexagramOr(const FZxvHexagram& A, const FZxvHexagram& B)
{
	return FZxvHexagram::FromCore(Hex(A).or_(Hex(B)));
}

FZxvHexagram UZxvLibrary::HexagramNot(const FZxvHexagram& A) { return FZxvHexagram::FromCore(Hex(A).not_()); }
