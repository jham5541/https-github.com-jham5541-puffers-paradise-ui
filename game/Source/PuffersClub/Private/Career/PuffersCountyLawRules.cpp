#include "Career/PuffersCountyLawRules.h"

void FPuffersCountyLawRules::SeedCounties(FPuffersCountyLawState& State, const TArray<FPuffersCountyDefinition>& Counties)
{
	for (const FPuffersCountyDefinition& County : Counties)
	{
		const bool bHasVersion = State.Versions.ContainsByPredicate([&County](const FPuffersCountyLawVersion& V) { return V.CountyId == County.CountyId; });
		if (!bHasVersion)
		{
			FPuffersCountyLawVersion Seed = County.SeedLaw;
			Seed.CountyId = County.CountyId;
			Seed.Version = 1;
			Seed.FromProposal = NAME_None;
			if (Seed.EffectiveAt.GetTicks() == 0)
			{
				Seed.EffectiveAt = FDateTime(2000, 1, 1);
			}
			State.Versions.Add(Seed);
		}
	}
}

const FPuffersCountyLawVersion* FPuffersCountyLawRules::SelectVersion(const FPuffersCountyLawState& State, FName CountyId, const FDateTime& At)
{
	const FPuffersCountyLawVersion* Best = nullptr;
	for (const FPuffersCountyLawVersion& Version : State.Versions)
	{
		if (Version.CountyId != CountyId || Version.EffectiveAt > At)
		{
			continue;
		}
		if (!Best || Version.EffectiveAt > Best->EffectiveAt || (Version.EffectiveAt == Best->EffectiveAt && Version.Version > Best->Version))
		{
			Best = &Version;
		}
	}
	return Best;
}

int32 FPuffersCountyLawRules::LatestVersionNumber(const FPuffersCountyLawState& State, FName CountyId)
{
	int32 Latest = 0;
	for (const FPuffersCountyLawVersion& Version : State.Versions)
	{
		if (Version.CountyId == CountyId)
		{
			Latest = FMath::Max(Latest, Version.Version);
		}
	}
	return Latest;
}

FPuffersCaseOutcome FPuffersCountyLawRules::EvaluateCase(const FPuffersCountyLawVersion& Law, const FPuffersCaseFacts& Facts)
{
	FPuffersCaseOutcome Out;
	Out.CaseId = Facts.CaseId;
	Out.LawVersion = Law.Version;

	const int64 PersonalMg = Facts.bDocumentedLicensedInventory ? 0 : FMath::Max<int64>(Facts.CannabisMg, 0);

	// Co-possession: any positive amount of cannabis plus a firearm in the same incident. Applies to everyone,
	// licensed owners included, and takes precedence over the possession-only thresholds.
	if (Law.FirearmMinimumYears > 0.f && Facts.CannabisMg > 0 && Facts.bFirearmInIncident)
	{
		Out.bCharge = true;
		Out.bFirearmOffense = true;
		Out.MinimumYearsIfConvicted = Law.FirearmMinimumYears;
		Out.Explanation = FString::Printf(TEXT("Cannabis and a firearm in the same incident under law v%d: a conviction carries at least %.0f simulated years."),
			Law.Version, Law.FirearmMinimumYears);
		return Out;
	}

	if (PersonalMg <= 0)
	{
		Out.Explanation = Facts.bDocumentedLicensedInventory
			? TEXT("Documented licensed inventory isn't personal possession.")
			: TEXT("No cannabis in this incident.");
		return Out;
	}

	Out.bCharge = true;
	if (PersonalMg <= Law.FineOnlyCapMg)
	{
		Out.Fine = Law.FineOnlyFine;
		Out.PossessionCustodyYears = 0.f;
		Out.Explanation = FString::Printf(TEXT("%lld mg is within the %lld mg fine-only cap (law v%d): fine %d, no custody."),
			PersonalMg, Law.FineOnlyCapMg, Law.Version, Law.FineOnlyFine);
		return Out;
	}

	for (const FPuffersPossessionBand& Band : Law.Bands)
	{
		if (Band.MaxMg < 0 || PersonalMg <= Band.MaxMg)
		{
			Out.Fine = Band.Fine;
			Out.PossessionCustodyYears = Band.CustodyYears;
			Out.Explanation = FString::Printf(TEXT("%lld mg is above the fine-only cap (law v%d): fine %d, up to %.1f simulated years."),
				PersonalMg, Law.Version, Band.Fine, Band.CustodyYears);
			return Out;
		}
	}

	Out.Explanation = FString::Printf(TEXT("%lld mg is above the fine-only cap (law v%d) and no band is configured."), PersonalMg, Law.Version);
	return Out;
}

float FPuffersCountyLawRules::SentenceYears(const FPuffersCaseOutcome& Outcome, float CourtYears)
{
	float Years = FMath::Max(0.f, CourtYears);
	if (Outcome.bFirearmOffense)
	{
		Years = FMath::Max(Years, Outcome.MinimumYearsIfConvicted);
	}
	return Years;
}

int32 FPuffersCountyLawRules::YearsToBlocks(float Years, const FPuffersCustodyScale& Scale)
{
	if (Years <= 0.f)
	{
		return 0;
	}
	return FMath::Max(1, FMath::CeilToInt(Years * FMath::Max(0.01f, Scale.BlocksPerSimulatedYear) - KINDA_SMALL_NUMBER));
}

FPuffersSentenceDefinition FPuffersCountyLawRules::MakeSentenceDefinition(FName SentenceId, int32 Blocks, const FPuffersCustodyScale& Scale)
{
	FPuffersSentenceDefinition Definition;
	Definition.SentenceId = SentenceId;
	Definition.FacilityId = Scale.FacilityId;
	Definition.BlocksPerDay = FMath::Max(1, Scale.BlocksPerDay);
	Definition.MinActiveSecondsPerBlock = Scale.MinActiveSecondsPerBlock;
	Definition.ReleaseWorldFlag = FName(*FString::Printf(TEXT("%s_Released"), *SentenceId.ToString()));
	for (int32 Block = 0; Block < Blocks; ++Block)
	{
		Definition.BlockMissions.Add(Scale.RoutineMission);
	}
	return Definition;
}

bool FPuffersCountyLawRules::AddIncident(FPuffersCountyLawState& State, const FPuffersViolentIncident& Incident)
{
	if (!Incident.bVerified || Incident.bPractice || Incident.IncidentId.IsNone())
	{
		return false;
	}
	if (State.Incidents.ContainsByPredicate([&Incident](const FPuffersViolentIncident& I) { return I.IncidentId == Incident.IncidentId; }))
	{
		return false;
	}
	State.Incidents.Add(Incident);
	return true;
}

float FPuffersCountyLawRules::ViolenceInWindow(const FPuffersCountyLawState& State, const FPuffersCountyDefinition& County, const FDateTime& Now)
{
	const FDateTime Start = Now - FTimespan::FromDays(County.ViolenceReviewWindowDays);
	float Total = 0.f;
	for (const FPuffersViolentIncident& Incident : State.Incidents)
	{
		if (Incident.CountyId == County.CountyId && Incident.At >= Start && Incident.At <= Now)
		{
			Total += FMath::Max(0.f, Incident.Weight);
		}
	}
	return Total;
}

FPuffersPolicyProposal* FPuffersCountyLawRules::FindProposal(FPuffersCountyLawState& State, FName ProposalId)
{
	return State.Proposals.FindByPredicate([ProposalId](const FPuffersPolicyProposal& P) { return P.ProposalId == ProposalId; });
}

const FPuffersPolicyProposal* FPuffersCountyLawRules::FindOpenProposal(const FPuffersCountyLawState& State, FName CountyId, EPuffersProposalKind Kind)
{
	return State.Proposals.FindByPredicate([CountyId, Kind](const FPuffersPolicyProposal& P)
	{
		return P.CountyId == CountyId && P.Kind == Kind
			&& (P.State == EPuffersProposalState::Drafting || P.State == EPuffersProposalState::Review
				|| P.State == EPuffersProposalState::Deferred || P.State == EPuffersProposalState::Approved
				|| P.State == EPuffersProposalState::Scheduled);
	});
}

bool FPuffersCountyLawRules::AddSupport(FPuffersPolicyProposal& Proposal, FName SourceId, int32 Amount)
{
	if (SourceId.IsNone() || Amount <= 0 || Proposal.SupportSources.Contains(SourceId))
	{
		return false;
	}
	if (Proposal.State != EPuffersProposalState::Drafting && Proposal.State != EPuffersProposalState::Review && Proposal.State != EPuffersProposalState::Deferred)
	{
		return false;
	}
	Proposal.SupportSources.Add(SourceId);
	Proposal.Support += Amount;
	return true;
}

EPuffersJudgeDecision FPuffersCountyLawRules::NpcJudge(const FPuffersPolicyProposal& Proposal, const FPuffersProposalTemplate& Template, float ViolenceWeight, const FPuffersCountyDefinition& County, FString& OutReason)
{
	if (Proposal.Kind == EPuffersProposalKind::FirearmMinimum)
	{
		const float Needed = FMath::Max(0.01f, County.ViolenceApprovalThreshold);
		if (ViolenceWeight >= Needed)
		{
			OutReason = FString::Printf(TEXT("Verified violent street deals (%.1f) reached the threshold (%.1f). Public safety outweighs the cost of longer terms."), ViolenceWeight, Needed);
			return EPuffersJudgeDecision::Approve;
		}
		if (ViolenceWeight >= Needed * 0.5f)
		{
			OutReason = FString::Printf(TEXT("Violence (%.1f of %.1f) is rising but not sustained. Deferred for another review."), ViolenceWeight, Needed);
			return EPuffersJudgeDecision::Defer;
		}
		OutReason = FString::Printf(TEXT("Violence (%.1f of %.1f) doesn't justify a mandatory minimum."), ViolenceWeight, Needed);
		return EPuffersJudgeDecision::Reject;
	}

	const int32 Needed = FMath::Max(1, Template.RequiredSupport);
	if (Proposal.Support >= Needed)
	{
		OutReason = FString::Printf(TEXT("Documented support (%d of %d) from businesses, workers and neighbors carries the proposal."), Proposal.Support, Needed);
		return EPuffersJudgeDecision::Approve;
	}
	if (Proposal.Support * 2 >= Needed)
	{
		OutReason = FString::Printf(TEXT("Support (%d of %d) is promising. Come back with more testimony."), Proposal.Support, Needed);
		return EPuffersJudgeDecision::Defer;
	}
	OutReason = FString::Printf(TEXT("Support (%d of %d) is too thin."), Proposal.Support, Needed);
	return EPuffersJudgeDecision::Reject;
}

bool FPuffersCountyLawRules::Decide(FPuffersPolicyProposal& Proposal, const FPuffersProposalTemplate& Template, EPuffersJudgeDecision Decision, EPuffersJudgeKind Judge, FName JudgeId, const FString& Reason, const FDateTime& Now)
{
	if (Proposal.State != EPuffersProposalState::Review && Proposal.State != EPuffersProposalState::Deferred)
	{
		return false;
	}
	Proposal.DecidedAt = Now;
	Proposal.DecidedBy = Judge;
	Proposal.JudgeId = JudgeId;
	Proposal.Reason = Reason;
	switch (Decision)
	{
	case EPuffersJudgeDecision::Approve:
		Proposal.State = EPuffersProposalState::Scheduled;
		Proposal.ActivationAt = Now + FTimespan::FromHours(FMath::Max(0.f, Template.ActivationDelayHours));
		break;
	case EPuffersJudgeDecision::Reject:
		Proposal.State = EPuffersProposalState::Rejected;
		break;
	case EPuffersJudgeDecision::Defer:
		Proposal.State = EPuffersProposalState::Deferred;
		break;
	}
	return true;
}

TArray<FName> FPuffersCountyLawRules::PublishDue(FPuffersCountyLawState& State, const TArray<FPuffersProposalTemplate>& Templates, const FDateTime& Now)
{
	TArray<FName> Published;
	for (FPuffersPolicyProposal& Proposal : State.Proposals)
	{
		if (Proposal.State != EPuffersProposalState::Scheduled || Proposal.ActivationAt > Now)
		{
			continue;
		}
		const FPuffersProposalTemplate* Template = Templates.FindByPredicate([&Proposal](const FPuffersProposalTemplate& T) { return T.Id == Proposal.TemplateId; });
		const FPuffersCountyLawVersion* Current = SelectVersion(State, Proposal.CountyId, Proposal.ActivationAt);
		if (!Template || !Current)
		{
			continue;
		}

		FPuffersCountyLawVersion Next = *Current;
		Next.Version = LatestVersionNumber(State, Proposal.CountyId) + 1;
		Next.EffectiveAt = Proposal.ActivationAt;
		Next.FromProposal = Proposal.ProposalId;
		if (Template->NewFineOnlyCapMg >= 0)
		{
			Next.FineOnlyCapMg = Template->NewFineOnlyCapMg;
			// Bands at or below the new cap no longer apply.
			Next.Bands.RemoveAll([&Next](const FPuffersPossessionBand& B) { return B.MaxMg >= 0 && B.MaxMg <= Next.FineOnlyCapMg; });
		}
		if (Template->NewFirearmMinimumYears >= 0.f)
		{
			Next.FirearmMinimumYears = Template->NewFirearmMinimumYears;
		}

		Proposal.State = EPuffersProposalState::Effective;
		Proposal.PublishedVersion = Next.Version;
		State.Versions.Add(Next);
		Published.Add(Proposal.ProposalId);
	}
	return Published;
}

float FPuffersCountyLawRules::StableRoll(FName Id, int32 Salt)
{
	const uint32 Hash = HashCombine(GetTypeHash(Id.ToString()), GetTypeHash(Salt));
	return static_cast<float>(Hash % 10000) / 10000.f;
}

EPuffersBribeOutcome FPuffersCountyLawRules::ResolveBribe(FName OfferId, float Integrity, float Greed, int32 Amount, int32 Price)
{
	const float Roll = StableRoll(OfferId, 7);
	const float Ratio = Price > 0 ? static_cast<float>(Amount) / static_cast<float>(Price) : 1.f;

	// Honest officials report a clear offer.
	if (Integrity >= 0.8f)
	{
		return Roll < Integrity ? EPuffersBribeOutcome::Reported : EPuffersBribeOutcome::Refused;
	}
	if (Ratio < 0.5f)
	{
		return Roll < Integrity ? EPuffersBribeOutcome::Reported : EPuffersBribeOutcome::Refused;
	}
	const float AcceptChance = FMath::Clamp(Greed * FMath::Min(Ratio, 2.f) - Integrity * 0.5f, 0.f, 0.95f);
	if (Roll < AcceptChance)
	{
		// Some accepted bribes come back as an investigation later.
		return StableRoll(OfferId, 13) < Integrity ? EPuffersBribeOutcome::LaterInvestigation : EPuffersBribeOutcome::Accepted;
	}
	return Roll < AcceptChance + Integrity * 0.5f ? EPuffersBribeOutcome::Reported : EPuffersBribeOutcome::Refused;
}

FString FPuffersCountyLawRules::DescribeLaw(const FPuffersCountyLawVersion& Law)
{
	FString Text = FString::Printf(TEXT("%s law v%d: possession up to %.1f g is fine-only (%d)."),
		*Law.CountyId.ToString(), Law.Version, static_cast<double>(Law.FineOnlyCapMg) / MgPerGram, Law.FineOnlyFine);
	if (Law.FirearmMinimumYears > 0.f)
	{
		Text += FString::Printf(TEXT(" Cannabis with a firearm: minimum %.0f simulated years."), Law.FirearmMinimumYears);
	}
	return Text;
}
