#include "Career/PuffersCareerRules.h"

EPuffersCareerRoute FPuffersCareerRules::GetRoute(const FPuffersCareerOwnerState& State)
{
	if (State.bStreetCommitted)
	{
		return State.bLicensesForfeited ? EPuffersCareerRoute::StreetForfeited : EPuffersCareerRoute::StreetDualEligible;
	}
	if (State.bEverLicensed)
	{
		return EPuffersCareerRoute::LicensedOnly;
	}
	return EPuffersCareerRoute::Uncommitted;
}

FString FPuffersCareerRules::RouteName(EPuffersCareerRoute Route)
{
	switch (Route)
	{
	case EPuffersCareerRoute::Uncommitted:        return TEXT("Uncommitted");
	case EPuffersCareerRoute::LicensedOnly:       return TEXT("Licensed");
	case EPuffersCareerRoute::StreetDualEligible: return TEXT("Street (dual operation eligible)");
	case EPuffersCareerRoute::StreetForfeited:    return TEXT("Street (cannabis licenses forfeited)");
	}
	return TEXT("Unknown");
}

FString FPuffersCareerRules::DescribeCommitment(EPuffersCareerRoute Route, EPuffersOperationKind Kind)
{
	if (Kind == EPuffersOperationKind::Street)
	{
		if (Route == EPuffersCareerRoute::StreetDualEligible || Route == EPuffersCareerRoute::StreetForfeited)
		{
			return TEXT("You're already committed to the street.");
		}
		return TEXT("Street ownership is PERMANENT for this career, even if you sell or close it later. "
			"You can keep or open a licensed shop until your first confirmed street bust. That bust revokes every "
			"cannabis license you hold, for good. Ordinary jobs and non-cannabis businesses always stay open to you.");
	}
	if (Kind == EPuffersOperationKind::LicensedCannabis)
	{
		if (Route == EPuffersCareerRoute::StreetForfeited)
		{
			return TEXT("You can't hold a cannabis license in this career after a confirmed street bust.");
		}
		if (Route == EPuffersCareerRoute::StreetDualEligible)
		{
			return TEXT("This is a dual-operation license. It lasts until your first confirmed street bust.");
		}
		return TEXT("A licensed shop keeps your route open. A confirmed street bust would revoke every cannabis license.");
	}
	return TEXT("Non-cannabis businesses don't affect your route.");
}

bool FPuffersCareerRules::CanHoldCannabisLicense(const FPuffersCareerOwnerState& State, FString& OutReason)
{
	if (State.bLicensesForfeited)
	{
		OutReason = TEXT("Cannabis licenses were forfeited after a confirmed street bust. Nothing restores them in this career.");
		return false;
	}
	return true;
}

bool FPuffersCareerRules::CommitStreet(FPuffersCareerOwnerState& State, const FDateTime& Now)
{
	if (State.bStreetCommitted)
	{
		return false;
	}
	State.bStreetCommitted = true;
	State.StreetCommittedAt = Now;
	// Existing valid licenses carry over as dual-operation licenses.
	for (FPuffersLicense& License : State.Licenses)
	{
		if (License.State == EPuffersLicenseState::Active)
		{
			License.bDualOperation = true;
		}
	}
	return true;
}

bool FPuffersCareerRules::GrantLicense(FPuffersCareerOwnerState& State, FName LicenseId, FName OperationId, FName CountyId, const FDateTime& Now, FString& OutReason)
{
	if (LicenseId.IsNone())
	{
		OutReason = TEXT("No license id.");
		return false;
	}
	if (State.Licenses.ContainsByPredicate([LicenseId](const FPuffersLicense& L) { return L.LicenseId == LicenseId; }))
	{
		OutReason = TEXT("That license was already issued.");
		return false;
	}
	if (!CanHoldCannabisLicense(State, OutReason))
	{
		return false;
	}
	FPuffersLicense& License = State.Licenses.AddDefaulted_GetRef();
	License.LicenseId = LicenseId;
	License.OperationId = OperationId;
	License.CountyId = CountyId;
	License.IssuedAt = Now;
	License.bDualOperation = State.bStreetCommitted;
	State.bEverLicensed = true;
	if (FPuffersOperation* Operation = FindOperation(State, OperationId))
	{
		Operation->bRegulatedSalesClosed = false;
	}
	return true;
}

const FPuffersLicense* FPuffersCareerRules::FindActiveLicense(const FPuffersCareerOwnerState& State, FName OperationId)
{
	return State.Licenses.FindByPredicate([OperationId](const FPuffersLicense& L)
	{
		return L.OperationId == OperationId && L.State == EPuffersLicenseState::Active;
	});
}

EPuffersStrikeResult FPuffersCareerRules::RecordFinding(FPuffersCareerOwnerState& State, const FPuffersEnforcementFinding& Finding, const FDateTime& Now, TArray<FName>& OutReleasedSales)
{
	OutReleasedSales.Reset();
	if (State.Findings.ContainsByPredicate([&Finding](const FPuffersEnforcementFinding& F) { return F.FindingId == Finding.FindingId; }))
	{
		return EPuffersStrikeResult::Duplicate;
	}

	FPuffersEnforcementFinding& Stored = State.Findings.Add_GetRef(Finding);
	if (Stored.At.GetTicks() == 0)
	{
		Stored.At = Now;
	}
	if (!Stored.IsQualifyingStrike())
	{
		return EPuffersStrikeResult::NotQualifying;
	}

	Stored.bCountedAsStrike = true;
	if (State.bLicensesForfeited)
	{
		return EPuffersStrikeResult::AdditionalStrike;
	}

	// First valid strike: revoke everything at once. Being caught in a verified street sale is street
	// activity, so an owner who never took the commitment is committed by it.
	if (!State.bStreetCommitted)
	{
		State.bStreetCommitted = true;
		State.StreetCommittedAt = Now;
	}
	State.bLicensesForfeited = true;
	State.ForfeitedByFinding = Stored.FindingId;
	for (FPuffersLicense& License : State.Licenses)
	{
		if (License.State == EPuffersLicenseState::Active)
		{
			License.State = EPuffersLicenseState::Revoked;
			License.EndedAt = Now;
		}
	}
	for (FPuffersOperation& Operation : State.Operations)
	{
		if (Operation.Kind == EPuffersOperationKind::LicensedCannabis)
		{
			Operation.bRegulatedSalesClosed = true;
		}
	}
	OutReleasedSales = State.PendingRegulatedSales;
	State.PendingRegulatedSales.Reset();
	return EPuffersStrikeResult::FirstStrike;
}

bool FPuffersCareerRules::InvalidateFinding(FPuffersCareerOwnerState& State, FName FindingId, const FString& Reason, bool& bOutEligibilityRestored)
{
	bOutEligibilityRestored = false;
	FPuffersEnforcementFinding* Finding = State.Findings.FindByPredicate([FindingId](const FPuffersEnforcementFinding& F) { return F.FindingId == FindingId; });
	if (!Finding || Finding->bInvalidated)
	{
		return false;
	}
	Finding->bInvalidated = true;
	Finding->InvalidationReason = Reason;

	if (!State.bLicensesForfeited)
	{
		return true;
	}
	const bool bOtherStrike = State.Findings.ContainsByPredicate([](const FPuffersEnforcementFinding& F) { return F.IsQualifyingStrike(); });
	if (!bOtherStrike)
	{
		State.bLicensesForfeited = false;
		State.ForfeitedByFinding = NAME_None;
		bOutEligibilityRestored = true;
	}
	else if (State.ForfeitedByFinding == FindingId)
	{
		// Another valid strike still stands; it now carries the forfeiture.
		for (const FPuffersEnforcementFinding& F : State.Findings)
		{
			if (F.IsQualifyingStrike())
			{
				State.ForfeitedByFinding = F.FindingId;
				break;
			}
		}
	}
	return true;
}

bool FPuffersCareerRules::CanCommitRegulatedSale(const FPuffersCareerOwnerState& State, FName OperationId, FString& OutReason)
{
	if (!CanHoldCannabisLicense(State, OutReason))
	{
		return false;
	}
	const FPuffersOperation* Operation = FindOperation(State, OperationId);
	if (!Operation || Operation->Kind != EPuffersOperationKind::LicensedCannabis)
	{
		OutReason = TEXT("That isn't a licensed cannabis operation.");
		return false;
	}
	if (Operation->bRegulatedSalesClosed)
	{
		OutReason = TEXT("Licensed sales are closed at this location.");
		return false;
	}
	if (!FindActiveLicense(State, OperationId))
	{
		OutReason = TEXT("No active county license for this location.");
		return false;
	}
	return true;
}

bool FPuffersCareerRules::CanOpenOperation(const FPuffersCareerOwnerState& State, const FPuffersOperation& Operation, const FPuffersBusinessArchetype& Archetype, int32 StaffCount, FString& OutReason)
{
	for (const FName& Fixture : Archetype.RequiredFixtures)
	{
		if (!Operation.InstalledFixtures.Contains(Fixture))
		{
			OutReason = FString::Printf(TEXT("Install the %s first."), *Fixture.ToString());
			return false;
		}
	}
	if (StaffCount < Archetype.RequiredStaff)
	{
		OutReason = FString::Printf(TEXT("Hire %d more staff."), Archetype.RequiredStaff - StaffCount);
		return false;
	}
	if (Operation.Kind == EPuffersOperationKind::LicensedCannabis)
	{
		if (!CanHoldCannabisLicense(State, OutReason))
		{
			return false;
		}
		if (!FindActiveLicense(State, Operation.OperationId))
		{
			OutReason = TEXT("This location needs an active county cannabis license.");
			return false;
		}
	}
	return true;
}

FPuffersOperation* FPuffersCareerRules::FindOperation(FPuffersCareerOwnerState& State, FName OperationId)
{
	return State.Operations.FindByPredicate([OperationId](const FPuffersOperation& O) { return O.OperationId == OperationId; });
}

const FPuffersOperation* FPuffersCareerRules::FindOperation(const FPuffersCareerOwnerState& State, FName OperationId)
{
	return State.Operations.FindByPredicate([OperationId](const FPuffersOperation& O) { return O.OperationId == OperationId; });
}

int32 FPuffersCareerRules::CountCounties(const FPuffersCareerOwnerState& State)
{
	TSet<FName> Counties;
	for (const FPuffersOperation& Operation : State.Operations)
	{
		if (Operation.bOpen && !Operation.CountyId.IsNone())
		{
			Counties.Add(Operation.CountyId);
		}
	}
	return Counties.Num();
}

FName FPuffersCareerRules::EvaluatePhase(const TArray<FPuffersCareerPhase>& Phases, const FPuffersCareerOwnerState& State, const FPuffersCareerStanding& Standing)
{
	FName Best;
	int32 OpenOperations = 0;
	for (const FPuffersOperation& Operation : State.Operations)
	{
		OpenOperations += Operation.bOpen ? 1 : 0;
	}
	const int32 Counties = CountCounties(State);
	const int32 Influence = FMath::Max(Standing.LicensedInfluence(), Standing.StreetInfluence());

	for (const FPuffersCareerPhase& Phase : Phases)
	{
		const bool bMet = Standing.LifetimeWages >= Phase.MinLifetimeWages
			&& OpenOperations >= Phase.MinOperations
			&& Standing.NetCannabisProfit >= Phase.MinNetCannabisProfit
			&& Counties >= Phase.MinCounties
			&& Influence >= Phase.MinInfluence;
		if (!bMet)
		{
			break;
		}
		Best = Phase.Id;
	}
	return Best;
}

void FPuffersCareerRules::NoteChange(FPuffersCareerStanding& Standing, const FString& Reason)
{
	Standing.RecentChanges.Add(Reason);
	while (Standing.RecentChanges.Num() > 20)
	{
		Standing.RecentChanges.RemoveAt(0);
	}
}
