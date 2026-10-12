// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
#include "ZxvSubsystem.h"

#include "ZxvUnrealModule.h"

void UZxvSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bInitialized = true;
}

void UZxvSubsystem::Deinitialize()
{
	bInitialized = false;
	TArray<FName> Names;
	Sims.GetKeys(Names);
	for (const FName& Name : Names)
	{
		DestroyLockstep(Name);
	}
	Super::Deinitialize();
}

bool UZxvSubsystem::IsLibraryUsable()
{
	return FZxvUnrealModule::IsLibraryUsable();
}

TSubclassOf<UZxvLockstep> UZxvSubsystem::GetLockstepClass() const
{
	return UZxvLockstep::StaticClass();
}

UZxvLockstep* UZxvSubsystem::CreateLockstep(FName Name, const FZxvLockstepConfig& Config, EZxvResult& Result)
{
	if (Name.IsNone())
	{
		Result = EZxvResult::InvalidArgument;
		return nullptr;
	}
	if (Sims.Contains(Name))
	{
		Result = EZxvResult::Duplicate;
		return nullptr;
	}
	UClass* Class = GetLockstepClass();
	UZxvLockstep* Sim = NewObject<UZxvLockstep>(this, Class ? Class : UZxvLockstep::StaticClass());
	Result = Sim->Setup(Config);
	if (Result != EZxvResult::Ok)
	{
		UE_LOG(LogZxv, Warning, TEXT("ZXV: CreateLockstep(%s) failed: %s"), *Name.ToString(),
			UTF8_TO_TCHAR(zxvue::last_error().c_str()));
		return nullptr;
	}
	Sims.Add(Name, Sim);
	HandleSimCreated(Name, Sim);
	return Sim;
}

UZxvLockstep* UZxvSubsystem::FindLockstep(FName Name) const
{
	const TObjectPtr<UZxvLockstep>* Found = Sims.Find(Name);
	return Found ? Found->Get() : nullptr;
}

bool UZxvSubsystem::DestroyLockstep(FName Name)
{
	TObjectPtr<UZxvLockstep> Sim;
	if (!Sims.RemoveAndCopyValue(Name, Sim))
	{
		return false;
	}
	HandleSimDestroyed(Name, Sim);
	if (Sim)
	{
		Sim->MarkAsGarbage();
	}
	return true;
}

TArray<FName> UZxvSubsystem::GetLockstepNames() const
{
	TArray<FName> Names;
	Sims.GetKeys(Names);
	return Names;
}

void UZxvSubsystem::HandleSimCreated(FName Name, UZxvLockstep* Sim)
{
	OnSimCreated.Broadcast(Name, Sim);
}

void UZxvSubsystem::HandleSimDestroyed(FName Name, UZxvLockstep* Sim)
{
	OnSimDestroyed.Broadcast(Name, Sim);
}

bool UZxvSubsystem::IsTickable() const
{
	return bInitialized && Sims.Num() > 0;
}

// DeltaTime is a float and is deliberately ignored: each auto-paced sim
// reads the monotonic clock in integer microseconds instead.
void UZxvSubsystem::Tick(float DeltaTime)
{
	// Copy first: a handler may create or destroy sims.
	TArray<TPair<FName, UZxvLockstep*>> Paced;
	for (const TPair<FName, TObjectPtr<UZxvLockstep>>& It : Sims)
	{
		if (It.Value && It.Value->bAutoPace)
		{
			Paced.Emplace(It.Key, It.Value.Get());
		}
	}
	for (const TPair<FName, UZxvLockstep*>& It : Paced)
	{
		PaceSim(It.Key, It.Value);
	}
}

void UZxvSubsystem::PaceSim(FName Name, UZxvLockstep* Sim)
{
	int32 Due = 0;
	int64 Dropped = 0;
	const EZxvResult Result = Sim->PumpClock(Due, Dropped);
	if (Result != EZxvResult::Ok)
	{
		UE_LOG(LogZxv, Warning, TEXT("ZXV: pacing %s failed: %s"), *Name.ToString(),
			UTF8_TO_TCHAR(zxvue::last_error().c_str()));
		return;
	}
	if (Due > 0 || Dropped > 0)
	{
		OnTicksDue.Broadcast(Name, Sim, Due, Dropped);
	}
}
