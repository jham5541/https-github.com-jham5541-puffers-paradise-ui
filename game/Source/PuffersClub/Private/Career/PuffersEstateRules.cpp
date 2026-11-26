#include "Career/PuffersEstateRules.h"

bool FPuffersEstateRules::ActivateOnline(FPuffersEstateState& State, const FDateTime& Now)
{
	if (State.bOnlineCareerActive)
	{
		return false;
	}
	State.bOnlineCareerActive = true;
	State.ActivatedAt = Now;
	State.LastCareAt = Now;
	return true;
}

FDateTime FPuffersEstateRules::CareDeadline(const FPuffersEstateState& State, float WindowHours)
{
	return State.LastCareAt + FTimespan::FromHours(WindowHours);
}

bool FPuffersEstateRules::HasActiveSelfCoverage(const FPuffersEstateState& State, FName OwnerId, const FDateTime& At)
{
	return State.Policies.ContainsByPredicate([OwnerId, &At](const FPuffersInsurancePolicy& P)
	{
		return P.Kind == EPuffersInsuranceKind::Self && P.InsuredId == OwnerId && P.IsActiveAt(At);
	});
}

EPuffersCareStatus FPuffersEstateRules::GetCareStatus(const FPuffersEstateState& State, FName OwnerId, float WindowHours, bool bHasQualifyingResidence, const FDateTime& Now)
{
	if (!State.bOnlineCareerActive)
	{
		return EPuffersCareStatus::NotOnline;
	}
	// The deadline itself is the first overdue moment (LastCareAt + window).
	if (Now < CareDeadline(State, WindowHours))
	{
		return EPuffersCareStatus::Current;
	}
	if (HasActiveSelfCoverage(State, OwnerId, Now))
	{
		return EPuffersCareStatus::OverdueProtected;
	}
	// An absent owner without a qualifying residence has no raid target until they have one.
	return bHasQualifyingResidence ? EPuffersCareStatus::OverdueExposed : EPuffersCareStatus::OverdueProtected;
}

void FPuffersEstateRules::ClockIn(FPuffersEstateState& State, const FDateTime& Now)
{
	State.bClockedIn = true;
	State.ClockedInAt = Now;
}

bool FPuffersEstateRules::RecordCare(FPuffersEstateState& State, FName OpId, const FDateTime& Now, FString& OutWhy)
{
	if (!State.bOnlineCareerActive)
	{
		OutWhy = TEXT("Care only applies to online careers.");
		return false;
	}
	if (State.CareReceipts.Contains(OpId))
	{
		OutWhy = TEXT("Already recorded.");
		return false;
	}
	if (!State.bClockedIn)
	{
		OutWhy = TEXT("Clock in first, then finish a care activity.");
		return false;
	}
	State.CareReceipts.Add(OpId);
	State.LastCareAt = Now;
	State.bClockedIn = false;
	return true;
}

bool FPuffersEstateRules::CanBuy(const FPuffersEstateState& State, const FPuffersInsuranceProduct& Product, EPuffersCareStatus Status, FName InsuredId, FName OwnerId, bool bInsuredConsents, FString& OutWhy)
{
	if (Product.Kind == EPuffersInsuranceKind::Self)
	{
		if (InsuredId != OwnerId)
		{
			OutWhy = TEXT("Self cover insures you.");
			return false;
		}
		if (Status != EPuffersCareStatus::Current)
		{
			OutWhy = TEXT("Buy or renew self cover while your care is current, before you're overdue.");
			return false;
		}
		return true;
	}
	if (InsuredId == OwnerId)
	{
		OutWhy = TEXT("This policy covers a team member or partner.");
		return false;
	}
	if (!bInsuredConsents)
	{
		OutWhy = TEXT("The insured has to agree to the policy.");
		return false;
	}
	return true;
}

FPuffersInsurancePolicy FPuffersEstateRules::MakePolicy(const FPuffersInsuranceProduct& Product, FName PolicyId, FName OwnerId, FName InsuredId, FName BeneficiaryId, const FDateTime& Now)
{
	FPuffersInsurancePolicy Policy;
	Policy.PolicyId = PolicyId;
	Policy.ProductId = Product.ProductId;
	Policy.Kind = Product.Kind;
	Policy.InsuredId = InsuredId;
	Policy.PolicyholderId = OwnerId;
	Policy.BeneficiaryId = Product.Kind == EPuffersInsuranceKind::Self ? OwnerId : BeneficiaryId;
	Policy.StartsAt = Now;
	Policy.EndsAt = Now + FTimespan::FromDays(FMath::Max(1, Product.TermDays));
	Policy.Premium = Product.Premium;
	Policy.Benefit = Product.Benefit;
	Policy.bConsented = true;
	return Policy;
}

bool FPuffersEstateRules::ValidateClaim(const FPuffersEstateState& State, const FPuffersInsurancePolicy& Policy, const FPuffersLossEvent& Loss, FString& OutWhy)
{
	if (Policy.Kind == EPuffersInsuranceKind::Self)
	{
		OutWhy = TEXT("Self cover protects your estate; it doesn't pay a death benefit.");
		return false;
	}
	if (Policy.bClaimPaid || State.SettledLossEvents.Contains(Loss.EventId))
	{
		OutWhy = TEXT("This loss was already settled.");
		return false;
	}
	if (Loss.CharacterId != Policy.InsuredId)
	{
		OutWhy = TEXT("That character isn't insured by this policy.");
		return false;
	}
	if (!Policy.IsActiveAt(Loss.At))
	{
		OutWhy = TEXT("The policy wasn't active at the time of the loss.");
		return false;
	}
	if (!Loss.bPermanentDeath || !Loss.bConfirmed)
	{
		OutWhy = TEXT("Only a confirmed permanent death is covered. Respawns and knockdowns aren't.");
		return false;
	}
	if (Loss.bScriptedOrPractice)
	{
		OutWhy = TEXT("Scripted and practice deaths aren't covered.");
		return false;
	}
	if (Loss.bIntentional && Loss.CausedBy == Policy.BeneficiaryId)
	{
		OutWhy = TEXT("The beneficiary caused this loss on purpose. No payout.");
		return false;
	}
	return true;
}

bool FPuffersEstateRules::HasOpenRaid(const FPuffersEstateState& State)
{
	return State.Raids.ContainsByPredicate([](const FPuffersRaid& R) { return R.State == EPuffersRaidState::Open; });
}

FPuffersRaid* FPuffersEstateRules::FindRaid(FPuffersEstateState& State, FName RaidId)
{
	return State.Raids.FindByPredicate([RaidId](const FPuffersRaid& R) { return R.RaidId == RaidId; });
}

void FPuffersEstateRules::ClassifyForRaid(TArray<FPuffersEstateLine>& Lines, int32 WinnerCarrySlots)
{
	int32 SlotsLeft = FMath::Max(0, WinnerCarrySlots);
	for (FPuffersEstateLine& Line : Lines)
	{
		if (Line.Kind != EPuffersEstateAssetKind::Item || !Line.Transfers())
		{
			continue;
		}
		if (SlotsLeft > 0)
		{
			--SlotsLeft;
		}
		else
		{
			Line.bToClaimStorage = true;
		}
	}
}
