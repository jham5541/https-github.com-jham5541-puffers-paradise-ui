// Pure rules for the career owner: route commitment, the one-strike license rule, regulated sale gates and
// magnate standing. No world, no saves, so every case in the handoff's acceptance list can be unit tested.

#pragma once

#include "CoreMinimal.h"
#include "Career/PuffersCareerTypes.h"

struct PUFFERSCLUB_API FPuffersCareerRules
{
	static EPuffersCareerRoute GetRoute(const FPuffersCareerOwnerState& State);

	static FString RouteName(EPuffersCareerRoute Route);

	// What the commit screen says before the player accepts.
	static FString DescribeCommitment(EPuffersCareerRoute Route, EPuffersOperationKind Kind);

	// Can this career apply for (or renew, or be reissued) a cannabis license? Explains why not.
	static bool CanHoldCannabisLicense(const FPuffersCareerOwnerState& State, FString& OutReason);

	// Records the permanent street commitment. Returns false if it was already made (still committed).
	static bool CommitStreet(FPuffersCareerOwnerState& State, const FDateTime& Now);

	// Grants a license if the owner is eligible. Fails on a duplicate id or an ineligible owner.
	static bool GrantLicense(FPuffersCareerOwnerState& State, FName LicenseId, FName OperationId, FName CountyId, const FDateTime& Now, FString& OutReason);

	static const FPuffersLicense* FindActiveLicense(const FPuffersCareerOwnerState& State, FName OperationId);

	// Records an enforcement finding. Only a qualifying finding is a strike; the first one atomically revokes
	// every cannabis license, closes regulated sales and releases pending regulated sales (returned in OutReleasedSales).
	static EPuffersStrikeResult RecordFinding(FPuffersCareerOwnerState& State, const FPuffersEnforcementFinding& Finding, const FDateTime& Now, TArray<FName>& OutReleasedSales);

	// Mistaken-identity correction. Restores eligibility only if no other valid strike remains. Revoked licenses
	// stay revoked (the owner can apply again). Returns true if the finding existed and was valid before.
	static bool InvalidateFinding(FPuffersCareerOwnerState& State, FName FindingId, const FString& Reason, bool& bOutEligibilityRestored);

	// Gate for every regulated action: opening a cannabis premises, taking an ownership/control role, committing a sale.
	static bool CanCommitRegulatedSale(const FPuffersCareerOwnerState& State, FName OperationId, FString& OutReason);

	// Can this operation open? Fixtures, staff and (for cannabis) a valid license and eligible owner.
	static bool CanOpenOperation(const FPuffersCareerOwnerState& State, const FPuffersOperation& Operation, const FPuffersBusinessArchetype& Archetype, int32 StaffCount, FString& OutReason);

	static FPuffersOperation* FindOperation(FPuffersCareerOwnerState& State, FName OperationId);
	static const FPuffersOperation* FindOperation(const FPuffersCareerOwnerState& State, FName OperationId);

	// Number of distinct counties with an open operation.
	static int32 CountCounties(const FPuffersCareerOwnerState& State);

	// Highest phase whose requirements are met (phases are listed in order). None if the list is empty.
	static FName EvaluatePhase(const TArray<FPuffersCareerPhase>& Phases, const FPuffersCareerOwnerState& State, const FPuffersCareerStanding& Standing);

	// Adds a reason to the standing history (keeps the newest 20).
	static void NoteChange(FPuffersCareerStanding& Standing, const FString& Reason);
};
