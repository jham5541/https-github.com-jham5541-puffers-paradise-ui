// Pure rules for care deadlines, insurance, raid eligibility and estate manifests.

#pragma once

#include "CoreMinimal.h"
#include "Career/PuffersEstateTypes.h"

struct PUFFERSCLUB_API FPuffersEstateRules
{
	// Starts the first care window when the online career activates. No-op if already active.
	static bool ActivateOnline(FPuffersEstateState& State, const FDateTime& Now);

	static FDateTime CareDeadline(const FPuffersEstateState& State, float WindowHours);

	static bool HasActiveSelfCoverage(const FPuffersEstateState& State, FName OwnerId, const FDateTime& At);

	// bHasQualifyingResidence: owns an eligible house or holds a current, unlocked residential tenancy.
	static EPuffersCareStatus GetCareStatus(const FPuffersEstateState& State, FName OwnerId, float WindowHours, bool bHasQualifyingResidence, const FDateTime& Now);

	// Clock in. Opening the app or idling never renews the window; only a completed activity after clocking in does.
	static void ClockIn(FPuffersEstateState& State, const FDateTime& Now);

	// Records a completed care activity once (by op id). Requires clocking in. Renews LastCareAt.
	static bool RecordCare(FPuffersEstateState& State, FName OpId, const FDateTime& Now, FString& OutWhy);

	// Can buy this product now? Self cover only while current (before exposure); others need consent.
	static bool CanBuy(const FPuffersEstateState& State, const FPuffersInsuranceProduct& Product, EPuffersCareStatus Status, FName InsuredId, FName OwnerId, bool bInsuredConsents, FString& OutWhy);

	static FPuffersInsurancePolicy MakePolicy(const FPuffersInsuranceProduct& Product, FName PolicyId, FName OwnerId, FName InsuredId, FName BeneficiaryId, const FDateTime& Now);

	// Checks a claim. True if it should pay (once). Never pays twice per policy or per loss event.
	static bool ValidateClaim(const FPuffersEstateState& State, const FPuffersInsurancePolicy& Policy, const FPuffersLossEvent& Loss, FString& OutWhy);

	static bool HasOpenRaid(const FPuffersEstateState& State);

	static FPuffersRaid* FindRaid(FPuffersEstateState& State, FName RaidId);

	// Moves estate lines into transferable / excluded, and sends items over the carry capacity to claim storage.
	static void ClassifyForRaid(TArray<FPuffersEstateLine>& Lines, int32 WinnerCarrySlots);
};
