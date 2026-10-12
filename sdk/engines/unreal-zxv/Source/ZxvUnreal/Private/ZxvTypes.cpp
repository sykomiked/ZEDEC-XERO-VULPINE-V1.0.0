// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
#include "ZxvTypes.h"

namespace ZxvUnreal
{
EZxvResult ToResult(int32 Status)
{
	switch (Status)
	{
	case ZXV_OK: return EZxvResult::Ok;
	case ZXV_E_ARG: return EZxvResult::InvalidArgument;
	case ZXV_E_RANGE: return EZxvResult::OutOfRange;
	case ZXV_E_STATE: return EZxvResult::WrongState;
	case ZXV_E_BUDGET: return EZxvResult::OverBudget;
	case ZXV_E_FORMAT: return EZxvResult::BadFormat;
	case ZXV_E_DUPLICATE: return EZxvResult::Duplicate;
	case ZXV_E_NOT_FOUND: return EZxvResult::NotFound;
	case ZXV_E_CAPACITY: return EZxvResult::Capacity;
	case ZXV_E_NOMEM: return EZxvResult::OutOfMemory;
	case ZXV_E_RMAG_INVALID: return EZxvResult::InvalidBudget;
	default: return EZxvResult::Other;
	}
}

int32 ToStatus(EZxvResult Result)
{
	switch (Result)
	{
	case EZxvResult::Ok: return ZXV_OK;
	case EZxvResult::InvalidArgument: return ZXV_E_ARG;
	case EZxvResult::OutOfRange: return ZXV_E_RANGE;
	case EZxvResult::WrongState: return ZXV_E_STATE;
	case EZxvResult::OverBudget: return ZXV_E_BUDGET;
	case EZxvResult::BadFormat: return ZXV_E_FORMAT;
	case EZxvResult::Duplicate: return ZXV_E_DUPLICATE;
	case EZxvResult::NotFound: return ZXV_E_NOT_FOUND;
	case EZxvResult::Capacity: return ZXV_E_CAPACITY;
	case EZxvResult::OutOfMemory: return ZXV_E_NOMEM;
	case EZxvResult::InvalidBudget: return ZXV_E_RMAG_INVALID;
	default: return ZXV_E_INTERNAL;
	}
}
} // namespace ZxvUnreal

int32 FZxvLockstepConfig::ToCore(zxvue::LockstepConfig* Out) const
{
	if (!Out || Players < 0 || RationalSlots < 0 || HexagramSlots < 0 || InputBytes < 0 || History < 0)
	{
		return ZXV_E_ARG;
	}
	Out->players = static_cast<uint32_t>(Players);
	Out->rat_slots = static_cast<uint32_t>(RationalSlots);
	Out->hex_slots = static_cast<uint32_t>(HexagramSlots);
	Out->input_bytes = static_cast<uint32_t>(InputBytes);
	Out->history = static_cast<uint32_t>(History);
	Out->predict = bPredict;
	return ZXV_OK;
}
