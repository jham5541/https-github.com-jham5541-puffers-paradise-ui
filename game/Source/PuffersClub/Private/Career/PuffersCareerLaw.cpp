// County law (versions, proposals, judge, bribery, county sentences) and insider relationships.

#include "Career/PuffersCareerSubsystem.h"
#include "Career/PuffersCareerRules.h"
#include "Career/PuffersCountyLawRules.h"
#include "Career/PuffersInsiderRules.h"
#include "PuffersClub.h"

// ---------------------------------------------------------------------------------------------------------- Lookups

const FPuffersCountyDefinition* UPuffersCareerSubsystem::FindCounty(FName CountyId) const
{
	return LawCatalog.Counties.FindByPredicate([CountyId](const FPuffersCountyDefinition& C) { return C.CountyId == CountyId; });
}

const FPuffersProposalTemplate* UPuffersCareerSubsystem::FindTemplate(FName Id) const
{
	return LawCatalog.Proposals.FindByPredicate([Id](const FPuffersProposalTemplate& T) { return T.Id == Id; });
}

const FPuffersInsiderDefinition* UPuffersCareerSubsystem::FindInsider(FName NpcId) const
{
	return InsiderCatalog.Insiders.FindByPredicate([NpcId](const FPuffersInsiderDefinition& I) { return I.NpcId == NpcId; });
}

const FPuffersInsiderRequestDefinition* UPuffersCareerSubsystem::FindRequest(FName RequestId) const
{
	return InsiderCatalog.Requests.FindByPredicate([RequestId](const FPuffersInsiderRequestDefinition& R) { return R.RequestId == RequestId; });
}

FPuffersInsiderRelationship& UPuffersCareerSubsystem::Relationship(FName NpcId)
{
	FPuffersInsiderRelationship& Rel = Save->Insiders.FindOrAdd(NpcId);
	Rel.NpcId = NpcId;
	return Rel;
}

bool UPuffersCareerSubsystem::CountyAllowsCorruption(FName CountyId) const
{
	const FPuffersCountyLawVersion* Law = Save ? FPuffersCountyLawRules::SelectVersion(Save->Law, CountyId, Now()) : nullptr;
	return Law && Law->bInsiderCorruption;
}

FName UPuffersCareerSubsystem::GetCountyAt(const FVector& Location) const
{
	const FName MapCounty = MapCountyAt ? MapCountyAt(Location) : NAME_None;
	if (!MapCounty.IsNone())
	{
		for (const FPuffersCountyDefinition& County : LawCatalog.Counties)
		{
			if (County.CountyId == MapCounty || County.MapCountyIds.Contains(MapCounty))
			{
				return County.CountyId;
			}
		}
	}
	return LawCatalog.DefaultCounty;
}

FString UPuffersCareerSubsystem::DescribeCurrentLaw(FName CountyId) const
{
	if (!Save)
	{
		return FString();
	}
	const FPuffersCountyLawVersion* Law = FPuffersCountyLawRules::SelectVersion(Save->Law, CountyId, Now());
	FString Text = Law ? FPuffersCountyLawRules::DescribeLaw(*Law) : FString::Printf(TEXT("No law on file for %s."), *CountyId.ToString());
	for (const FPuffersPolicyProposal& P : Save->Law.Proposals)
	{
		if (P.CountyId == CountyId && P.State == EPuffersProposalState::Scheduled)
		{
			Text += FString::Printf(TEXT(" Coming %s UTC: %s."), *P.ActivationAt.ToString(TEXT("%Y-%m-%d %H:%M")), *P.TemplateId.ToString());
		}
	}
	return Text;
}

// ---------------------------------------------------------------------------------------------------------- Proposals

bool UPuffersCareerSubsystem::HasCivicAccess(FName CountyId) const
{
	FString Why;
	if (!Save || !FPuffersCareerRules::CanHoldCannabisLicense(Save->Owner, Why))
	{
		return false;
	}
	for (const FPuffersOperation& Operation : Save->Owner.Operations)
	{
		if (Operation.Kind == EPuffersOperationKind::LicensedCannabis && Operation.bOpen && !Operation.bRegulatedSalesClosed
			&& Operation.CountyId == CountyId
			&& Operation.ProfitableWeeks >= Config.CivicAccessProfitableWeeks
			&& Operation.NetProfit >= Config.CivicAccessNetProfit)
		{
			return true;
		}
	}
	return false;
}

FName UPuffersCareerSubsystem::Propose(FName TemplateId, FName CountyId, FString& OutReason)
{
	if (!Save)
	{
		return NAME_None;
	}
	const FPuffersProposalTemplate* Template = FindTemplate(TemplateId);
	if (!Template || !FindCounty(CountyId))
	{
		OutReason = TEXT("Unknown proposal or county.");
		return NAME_None;
	}
	if (FPuffersCountyLawRules::FindOpenProposal(Save->Law, CountyId, Template->Kind))
	{
		OutReason = TEXT("A proposal like that is already moving in this county.");
		return NAME_None;
	}
	if (Template->Kind == EPuffersProposalKind::PossessionReform && !HasCivicAccess(CountyId))
	{
		OutReason = FString::Printf(TEXT("Civic access needs a licensed shop in %s with %d profitable weeks and %d net profit."),
			*CountyId.ToString(), Config.CivicAccessProfitableWeeks, Config.CivicAccessNetProfit);
		return NAME_None;
	}

	FPuffersPolicyProposal& Proposal = Save->Law.Proposals.AddDefaulted_GetRef();
	Proposal.ProposalId = MakeId(TEXT("PROP"));
	Proposal.TemplateId = TemplateId;
	Proposal.CountyId = CountyId;
	Proposal.Kind = Template->Kind;
	Proposal.SponsorId = Template->Kind == EPuffersProposalKind::FirearmMinimum ? FName(TEXT("CountyReview")) : Save->Owner.CareerOwnerId;
	Proposal.CreatedAt = Now();
	Proposal.State = EPuffersProposalState::Drafting;
	Notify(FString::Printf(TEXT("Proposal drafted in %s: %s."), *CountyId.ToString(), *Template->Title));
	SaveNow();
	return Proposal.ProposalId;
}

bool UPuffersCareerSubsystem::AddProposalSupport(FName ProposalId, FName SourceId, int32 Amount)
{
	FPuffersPolicyProposal* Proposal = Save ? FPuffersCountyLawRules::FindProposal(Save->Law, ProposalId) : nullptr;
	if (!Proposal || !FPuffersCountyLawRules::AddSupport(*Proposal, SourceId, Amount))
	{
		return false;
	}
	Save->Standing.CivicSupport += Amount;
	RecomputeStanding();
	return true;
}

bool UPuffersCareerSubsystem::SubmitForReview(FName ProposalId)
{
	FPuffersPolicyProposal* Proposal = Save ? FPuffersCountyLawRules::FindProposal(Save->Law, ProposalId) : nullptr;
	if (!Proposal || Proposal->State != EPuffersProposalState::Drafting)
	{
		return false;
	}
	Proposal->State = EPuffersProposalState::Review;
	const FPuffersCountyDefinition* County = FindCounty(Proposal->CountyId);
	const float Violence = County ? FPuffersCountyLawRules::ViolenceInWindow(Save->Law, *County, Now()) : 0.f;
	Proposal->EvidenceSummary = FString::Printf(TEXT("Support %d from %d sources. Verified violent street deals in the last %.0f days: %.1f."),
		Proposal->Support, Proposal->SupportSources.Num(), County ? County->ViolenceReviewWindowDays : 0.f, Violence);
	Notify(FString::Printf(TEXT("Proposal %s is in front of the judge."), *ProposalId.ToString()));
	return true;
}

bool UPuffersCareerSubsystem::ReportViolentIncident(const FPuffersViolentIncident& Incident)
{
	if (!Save || !FPuffersCountyLawRules::AddIncident(Save->Law, Incident))
	{
		return false;
	}
	OpenFirearmReviewIfDue(Incident.CountyId);
	SaveNow();
	return true;
}

void UPuffersCareerSubsystem::OpenFirearmReviewIfDue(FName CountyId)
{
	const FPuffersCountyDefinition* County = FindCounty(CountyId);
	if (!County)
	{
		return;
	}
	const float Violence = FPuffersCountyLawRules::ViolenceInWindow(Save->Law, *County, Now());
	if (Violence < County->ViolenceThreshold || FPuffersCountyLawRules::FindOpenProposal(Save->Law, CountyId, EPuffersProposalKind::FirearmMinimum))
	{
		return;
	}
	const FPuffersCountyLawVersion* Current = FPuffersCountyLawRules::SelectVersion(Save->Law, CountyId, Now());
	if (Current && Current->FirearmMinimumYears > 0.f)
	{
		return;
	}
	const FPuffersProposalTemplate* Template = LawCatalog.Proposals.FindByPredicate([](const FPuffersProposalTemplate& T) { return T.Kind == EPuffersProposalKind::FirearmMinimum; });
	if (!Template)
	{
		return;
	}
	FString Why;
	const FName ProposalId = Propose(Template->Id, CountyId, Why);
	if (!ProposalId.IsNone())
	{
		SubmitForReview(ProposalId);
		Notify(FString::Printf(TEXT("A spike in verified violent street deals in %s opened a review: cannabis with a firearm could carry a %.0f-year minimum for everyone."),
			*CountyId.ToString(), Template->NewFirearmMinimumYears));
	}
}

EPuffersJudgeDecision UPuffersCareerSubsystem::RunNpcJudge(FName ProposalId, FString& OutReason)
{
	FPuffersPolicyProposal* Proposal = Save ? FPuffersCountyLawRules::FindProposal(Save->Law, ProposalId) : nullptr;
	const FPuffersProposalTemplate* Template = Proposal ? FindTemplate(Proposal->TemplateId) : nullptr;
	const FPuffersCountyDefinition* County = Proposal ? FindCounty(Proposal->CountyId) : nullptr;
	if (!Proposal || !Template || !County)
	{
		OutReason = TEXT("Nothing to decide.");
		return EPuffersJudgeDecision::Defer;
	}
	const float Violence = FPuffersCountyLawRules::ViolenceInWindow(Save->Law, *County, Now());
	const EPuffersJudgeDecision Decision = FPuffersCountyLawRules::NpcJudge(*Proposal, *Template, Violence, *County, OutReason);
	if (FPuffersCountyLawRules::Decide(*Proposal, *Template, Decision, EPuffersJudgeKind::NPC, TEXT("NPC_Judge"), OutReason, Now()))
	{
		Notify(FString::Printf(TEXT("Judge on %s: %s. %s"), *Template->Title,
			Decision == EPuffersJudgeDecision::Approve ? TEXT("approved") : Decision == EPuffersJudgeDecision::Reject ? TEXT("rejected") : TEXT("deferred"), *OutReason));
	}
	SaveNow();
	return Decision;
}

bool UPuffersCareerSubsystem::ServerJudgeDecide(FName ProposalId, EPuffersJudgeDecision Decision, FName JudgeId, const FString& Reason)
{
	FString Why;
	if (!RequireShared(Why) || JudgeId.IsNone())
	{
		return false;
	}
	FPuffersPolicyProposal* Proposal = Save ? FPuffersCountyLawRules::FindProposal(Save->Law, ProposalId) : nullptr;
	const FPuffersProposalTemplate* Template = Proposal ? FindTemplate(Proposal->TemplateId) : nullptr;
	if (!Proposal || !Template)
	{
		return false;
	}
	const bool bOk = FPuffersCountyLawRules::Decide(*Proposal, *Template, Decision, EPuffersJudgeKind::Server, JudgeId, Reason, Now());
	SaveNow();
	return bOk;
}

int32 UPuffersCareerSubsystem::PublishDueLaws()
{
	if (!Save)
	{
		return 0;
	}
	const TArray<FName> Published = FPuffersCountyLawRules::PublishDue(Save->Law, LawCatalog.Proposals, Now());
	for (const FName& ProposalId : Published)
	{
		const FPuffersPolicyProposal* Proposal = FPuffersCountyLawRules::FindProposal(Save->Law, ProposalId);
		if (!Proposal)
		{
			continue;
		}
		Publish(EventLawPublished, ProposalId);
		const FPuffersCountyLawVersion* Law = FPuffersCountyLawRules::SelectVersion(Save->Law, Proposal->CountyId, Now());
		Notify(FString::Printf(TEXT("New law in effect. %s"), Law ? *FPuffersCountyLawRules::DescribeLaw(*Law) : TEXT("")));
		Milestone(Proposal->Kind == EPuffersProposalKind::FirearmMinimum ? EPuffersFirstBuildMilestone::FirearmMinimumPublished : EPuffersFirstBuildMilestone::ReformPassed);
	}
	if (Published.Num() > 0)
	{
		SaveNow();
	}
	return Published.Num();
}

// ---------------------------------------------------------------------------------------------------------- Cases

FPuffersCaseOutcome UPuffersCareerSubsystem::OpenCase(const FPuffersCaseFacts& Facts)
{
	FPuffersCaseOutcome Outcome;
	if (!Save)
	{
		return Outcome;
	}
	if (const FPuffersCaseOutcome* Existing = Save->Law.Outcomes.Find(Facts.CaseId))
	{
		return *Existing;
	}
	const FPuffersCountyLawVersion* Law = FPuffersCountyLawRules::SelectVersion(Save->Law, Facts.CountyId, Facts.ConductAt);
	if (!Law)
	{
		Outcome.CaseId = Facts.CaseId;
		Outcome.Explanation = TEXT("No county law on file.");
		return Outcome;
	}
	Outcome = FPuffersCountyLawRules::EvaluateCase(*Law, Facts);
	Save->Law.Cases.Add(Facts.CaseId, Facts);
	Save->Law.Outcomes.Add(Facts.CaseId, Outcome);

	// A fine-only outcome is charged once now.
	if (Outcome.bCharge && !Outcome.bFirearmOffense && Outcome.PossessionCustodyYears <= 0.f && Outcome.Fine > 0)
	{
		PostWorld(FName(*FString::Printf(TEXT("FINE_%s"), *Facts.CaseId.ToString())), FPuffersLedger::Pocket, -Outcome.Fine, TEXT("World.County"),
			FString::Printf(TEXT("Possession fine (%s)"), *Facts.CountyId.ToString()));
	}
	SaveNow();
	return Outcome;
}

FName UPuffersCareerSubsystem::ConvictAndSentence(FName CaseId, float CourtYears)
{
	if (!Save)
	{
		return NAME_None;
	}
	const FPuffersCaseOutcome* Outcome = Save->Law.Outcomes.Find(CaseId);
	if (!Outcome || !Outcome->bCharge)
	{
		return NAME_None;
	}
	const FName SentenceId = FName(*FString::Printf(TEXT("SENT_%s"), *CaseId.ToString()));
	if (Save->Law.Sentences.ContainsByPredicate([SentenceId](const FPuffersCountySentence& S) { return S.SentenceId == SentenceId; }))
	{
		return SentenceId;
	}

	const float Years = FPuffersCountyLawRules::SentenceYears(*Outcome, CourtYears);
	if (Outcome->Fine > 0 && (Outcome->PossessionCustodyYears > 0.f || Outcome->bFirearmOffense))
	{
		PostWorld(FName(*FString::Printf(TEXT("FINE_%s"), *CaseId.ToString())), FPuffersLedger::Pocket, -Outcome->Fine, TEXT("World.County"), TEXT("Court fine"));
	}
	if (Years <= 0.f)
	{
		return NAME_None;
	}

	FPuffersCountySentence& Sentence = Save->Law.Sentences.AddDefaulted_GetRef();
	Sentence.SentenceId = SentenceId;
	Sentence.CaseId = CaseId;
	Sentence.LawVersion = Outcome->LawVersion;
	Sentence.ImposedYears = Years;
	Sentence.Scale = LawCatalog.CustodyScale;   // persisted: later balance changes leave this term intact
	Sentence.ImposedBlocks = FPuffersCountyLawRules::YearsToBlocks(Years, Sentence.Scale);
	Sentence.Definition = FPuffersCountyLawRules::MakeSentenceDefinition(SentenceId, Sentence.ImposedBlocks, Sentence.Scale);

	const bool bImposed = ImposeCustody ? ImposeCustody(Sentence.Definition) : false;
	Publish(EventCountySentence, SentenceId);
	Notify(FString::Printf(TEXT("Sentenced to %.1f simulated years (%d custody blocks, scale v%d) under law v%d.%s"),
		Years, Sentence.ImposedBlocks, Sentence.Scale.Version, Sentence.LawVersion, bImposed ? TEXT("") : TEXT(" Custody starts when the current term ends.")));
	Milestone(EPuffersFirstBuildMilestone::CountyConviction);
	SaveNow();
	return SentenceId;
}

// ---------------------------------------------------------------------------------------------------------- Bribery

EPuffersBribeOutcome UPuffersCareerSubsystem::OfferBribe(FName OfferId, FName OfficialId, int32 Amount, FName Scope)
{
	if (!Save || Amount <= 0)
	{
		return EPuffersBribeOutcome::Refused;
	}
	if (const FPuffersBribeRecord* Existing = Save->Law.Bribes.FindByPredicate([OfferId](const FPuffersBribeRecord& B) { return B.OfferId == OfferId; }))
	{
		return Existing->Outcome;   // never reroll
	}
	const FPuffersInsiderDefinition* Npc = FindInsider(OfficialId);
	const float Integrity = Npc ? Npc->Integrity : 0.6f;
	const float Greed = Npc ? Npc->Greed : 0.3f;
	const int32 Price = InsiderCatalog.BribePrices.Contains(OfficialId) ? InsiderCatalog.BribePrices[OfficialId] : 500;

	FPuffersBribeRecord& Record = Save->Law.Bribes.AddDefaulted_GetRef();
	Record.OfferId = OfferId;
	Record.ActorId = Save->Owner.CareerOwnerId;
	Record.OfficialId = OfficialId;
	Record.Amount = Amount;
	Record.Scope = Scope;
	Record.At = Now();
	Record.Outcome = FPuffersCountyLawRules::ResolveBribe(OfferId, Integrity, Greed, Amount, Price);

	const EPuffersBribeOutcome Outcome = Record.Outcome;
	if (Outcome == EPuffersBribeOutcome::Accepted || Outcome == EPuffersBribeOutcome::LaterInvestigation)
	{
		Record.FavorUntil = Now() + FTimespan::FromDays(3.0);
		Record.TxId = FName(*FString::Printf(TEXT("BRIBE_%s"), *OfferId.ToString()));
		PostWorld(Record.TxId, FPuffersLedger::Pocket, -Amount, TEXT("World.Officials"), TEXT("Cash in an envelope"));
	}
	if (Outcome == EPuffersBribeOutcome::Reported || Outcome == EPuffersBribeOutcome::LaterInvestigation)
	{
		Save->Law.CorruptionCases += 1;
		Save->Standing.CivicSupport = FMath::Max(0, Save->Standing.CivicSupport - 25);
		FPuffersCareerRules::NoteChange(Save->Standing, TEXT("Civic support -25: a corruption case was opened"));
	}
	// Published law, strikes and imposed sentences are untouched by design.
	Notify(FString::Printf(TEXT("Bribe to %s: %s."), *OfficialId.ToString(),
		Outcome == EPuffersBribeOutcome::Accepted ? TEXT("taken") : Outcome == EPuffersBribeOutcome::Refused ? TEXT("refused")
		: Outcome == EPuffersBribeOutcome::Reported ? TEXT("reported, a corruption case is open") : TEXT("taken, but someone's asking questions")));
	SaveNow();
	return Outcome;
}

bool UPuffersCareerSubsystem::HasDiscretionaryFavor(FName OfficialId, FName Scope) const
{
	if (!Save)
	{
		return false;
	}
	const FDateTime At = Now();
	return Save->Law.Bribes.ContainsByPredicate([OfficialId, Scope, &At](const FPuffersBribeRecord& B)
	{
		return B.OfficialId == OfficialId && B.Scope == Scope && B.FavorUntil > At
			&& (B.Outcome == EPuffersBribeOutcome::Accepted || B.Outcome == EPuffersBribeOutcome::LaterInvestigation);
	});
}

// ---------------------------------------------------------------------------------------------------------- Insiders

FPuffersInsiderRequest UPuffersCareerSubsystem::QuoteInsider(FName NpcId, FName RequestId, FString& OutWhy)
{
	FPuffersInsiderRequest Quote;
	const FPuffersInsiderDefinition* Npc = FindInsider(NpcId);
	const FPuffersInsiderRequestDefinition* Request = FindRequest(RequestId);
	if (!Save || !Npc || !Request)
	{
		OutWhy = TEXT("Unknown contact or request.");
		return Quote;
	}
	FPuffersInsiderRelationship& Rel = Relationship(NpcId);
	Quote = FPuffersInsiderRules::Quote(*Npc, Rel, *Request, CountyAllowsCorruption(Npc->CountyId), MakeId(TEXT("QUOTE")), Now(), OutWhy);
	if (Request->bObserveAndReport && Request->OutcomeTags.Num() > 0)
	{
		// ObserveAndReport jobs watch the subject named by the request's first tag.
		Quote.SubjectId = Request->OutcomeTags[0];
	}
	Rel.DemandsMade += 1;
	Rel.Requests.Add(Quote);
	if (Quote.Response == EPuffersInsiderResponse::Report)
	{
		Save->Law.CorruptionCases += 1;
		Save->Standing.CivicSupport = FMath::Max(0, Save->Standing.CivicSupport - 15);
		Rel.Resentment = FMath::Clamp(Rel.Resentment + 20, 0, 100);
		Notify(FString::Printf(TEXT("%s reported your request. An investigation notice is on the way."), *Npc->Name));
	}
	SaveNow();
	return Quote;
}

bool UPuffersCareerSubsystem::AcceptInsiderQuote(FName NpcId, FName QuoteId, FString& OutWhy)
{
	const FPuffersInsiderDefinition* Npc = FindInsider(NpcId);
	if (!Save || !Npc)
	{
		return false;
	}
	FPuffersInsiderRelationship& Rel = Relationship(NpcId);
	FPuffersInsiderRequest* Quote = Rel.Requests.FindByPredicate([QuoteId](const FPuffersInsiderRequest& R) { return R.QuoteId == QuoteId; });
	const FPuffersInsiderRequestDefinition* Request = Quote ? FindRequest(Quote->RequestId) : nullptr;
	if (!Quote || !Request || Quote->State != EPuffersRequestState::Quoted
		|| (Quote->Response != EPuffersInsiderResponse::Accept && Quote->Response != EPuffersInsiderResponse::Counter))
	{
		OutWhy = TEXT("That quote isn't open.");
		return false;
	}
	if (!FPuffersInsiderRules::CanStillAct(*Npc, Rel, *Request, CountyAllowsCorruption(Npc->CountyId), OutWhy))
	{
		Quote->State = EPuffersRequestState::Abandoned;
		return false;
	}
	if (Quote->Price > 0)
	{
		Quote->HoldId = FName(*FString::Printf(TEXT("HOLD_%s"), *QuoteId.ToString()));
		if (PlaceHold(Quote->HoldId, FPuffersLedger::Pocket, Quote->Price, FString::Printf(TEXT("%s: %s"), *Npc->Name, *Request->Description)) != EPuffersLedgerResult::Ok)
		{
			OutWhy = TEXT("Not enough Puff Cash available.");
			return false;
		}
	}
	Quote->State = EPuffersRequestState::Accepted;
	SaveNow();
	return true;
}

EPuffersRequestState UPuffersCareerSubsystem::CompleteInsiderRequest(FName NpcId, FName QuoteId, bool bSucceeded)
{
	const FPuffersInsiderDefinition* Npc = FindInsider(NpcId);
	if (!Save || !Npc)
	{
		return EPuffersRequestState::Failed;
	}
	FPuffersInsiderRelationship& Rel = Relationship(NpcId);
	FPuffersInsiderRequest* Quote = Rel.Requests.FindByPredicate([QuoteId](const FPuffersInsiderRequest& R) { return R.QuoteId == QuoteId; });
	const FPuffersInsiderRequestDefinition* Request = Quote ? FindRequest(Quote->RequestId) : nullptr;
	if (!Quote || !Request)
	{
		return EPuffersRequestState::Failed;
	}
	if (Quote->State != EPuffersRequestState::Accepted)
	{
		return Quote->State;   // already settled: reloading or retrying never settles twice
	}

	FString Why;
	const bool bCanAct = FPuffersInsiderRules::CanStillAct(*Npc, Rel, *Request, CountyAllowsCorruption(Npc->CountyId), Why);
	const bool bSuccess = bSucceeded && bCanAct;
	if (bSuccess)
	{
		if (!Quote->HoldId.IsNone())
		{
			Quote->SettlementTx = FName(*FString::Printf(TEXT("PAY_%s"), *QuoteId.ToString()));
			CaptureHold(Quote->HoldId, Quote->SettlementTx, FName(*FString::Printf(TEXT("World.Insider.%s"), *NpcId.ToString())), Quote->Price);
		}
		if (Request->bObserveAndReport)
		{
			Quote->ReportedFacts = FPuffersInsiderRules::ObserveAndReport(Rel, Quote->SubjectId.IsNone() ? FName(TEXT("Subject")) : Quote->SubjectId, Quote->QuotedAt, Quote->DueAt);
			Notify(Quote->ReportedFacts.Num() > 0
				? FString::Printf(TEXT("%s reports back with %d thing(s) they actually saw or heard."), *Npc->Name, Quote->ReportedFacts.Num())
				: FString::Printf(TEXT("%s didn't see anything worth reporting."), *Npc->Name));
		}
		Quote->State = EPuffersRequestState::Completed;
		Rel.Reliability = FMath::Clamp(Rel.Reliability + 5, 0, 100);
		// Payment settles the task. It never buys affection or intimacy.
	}
	else
	{
		if (!Quote->HoldId.IsNone())
		{
			ReleaseHold(Quote->HoldId);
		}
		Quote->State = bCanAct ? EPuffersRequestState::Failed : EPuffersRequestState::Abandoned;
		if (!bCanAct)
		{
			Notify(Why);
		}
	}
	SaveNow();
	return Quote->State;
}

bool UPuffersCareerSubsystem::GiveInsiderGift(FName NpcId, FName ItemId, bool bProfessionalScene, FString& OutWhy)
{
	const FPuffersInsiderDefinition* Npc = FindInsider(NpcId);
	if (!Save || !Npc)
	{
		return false;
	}
	if (RemoveInventory && !RemoveInventory(ItemId, 1))
	{
		OutWhy = TEXT("You don't have that.");
		return false;
	}
	const bool bAccepted = FPuffersInsiderRules::GiveGift(*Npc, Relationship(NpcId), ItemId, bProfessionalScene, OutWhy);
	if (!bAccepted && AddInventory)
	{
		AddInventory(ItemId, 1);   // a refused gift comes back
	}
	SaveNow();
	return bAccepted;
}

bool UPuffersCareerSubsystem::AskInsiderForIntimacy(FName NpcId, FString& OutWhy)
{
	const FPuffersInsiderDefinition* Npc = FindInsider(NpcId);
	if (!Save || !Npc)
	{
		return false;
	}
	const bool bYes = FPuffersInsiderRules::AskForIntimacy(*Npc, Relationship(NpcId), OutWhy);
	SaveNow();
	return bYes;
}

void UPuffersCareerSubsystem::StopInsiderIntimacy(FName NpcId)
{
	if (Save)
	{
		FPuffersInsiderRules::StopIntimacy(Relationship(NpcId));
		SaveNow();
	}
}

bool UPuffersCareerSubsystem::ShareWithInsider(FName NpcId, FName Subject, bool bIsClaim, bool bMaterial)
{
	if (!Save || !FindInsider(NpcId))
	{
		return false;
	}
	FPuffersKnowledgeFact Fact;
	Fact.FactId = MakeId(TEXT("FACT"));
	Fact.Subject = Subject;
	Fact.Source = EPuffersFactSource::Told;
	Fact.FromId = Save->Owner.CareerOwnerId;
	Fact.bIsClaim = bIsClaim;
	Fact.bMaterial = bMaterial;
	Fact.At = Now();
	return FPuffersInsiderRules::Learn(Relationship(NpcId), Fact);
}

bool UPuffersCareerSubsystem::InsiderObserves(FName NpcId, FName Subject, FName SceneId, bool bMaterial)
{
	if (!Save || !FindInsider(NpcId))
	{
		return false;
	}
	FPuffersInsiderRelationship& Rel = Relationship(NpcId);
	// Seeing the truth contradicts an earlier claim about the same subject.
	FPuffersInsiderRules::Contradict(Rel, Subject);
	FPuffersKnowledgeFact Fact;
	Fact.FactId = MakeId(TEXT("FACT"));
	Fact.Subject = Subject;
	Fact.Source = EPuffersFactSource::Observed;
	Fact.SceneId = SceneId;
	Fact.bMaterial = bMaterial;
	Fact.At = Now();
	return FPuffersInsiderRules::Learn(Rel, Fact);
}

void UPuffersCareerSubsystem::InsiderPromise(FName NpcId, FName PromiseId, bool bKept)
{
	if (!Save)
	{
		return;
	}
	FPuffersInsiderRelationship& Rel = Relationship(NpcId);
	if (bKept)
	{
		FPuffersInsiderRules::KeepPromise(Rel, PromiseId);
	}
	else
	{
		FPuffersInsiderRules::BreakPromise(Rel, PromiseId);
		if (Rel.Resentment >= 40)
		{
			WrongInsider(NpcId, NAME_None, 0);
		}
	}
	SaveNow();
}

EPuffersFallout UPuffersCareerSubsystem::WrongInsider(FName NpcId, FName Grievance, int32 Resentment)
{
	const FPuffersInsiderDefinition* Npc = FindInsider(NpcId);
	if (!Save || !Npc)
	{
		return EPuffersFallout::None;
	}
	FPuffersInsiderRelationship& Rel = Relationship(NpcId);
	if (!Grievance.IsNone())
	{
		FPuffersInsiderRules::AddGrievance(Rel, Grievance, Resentment);
	}
	if (Rel.Resentment < 40)
	{
		SaveNow();
		return EPuffersFallout::None;
	}
	FName Leaked;
	const EPuffersFallout Fallout = FPuffersInsiderRules::ResolveFallout(*Npc, Rel, Leaked);
	Publish(EventInsiderFallout, NpcId);
	switch (Fallout)
	{
	case EPuffersFallout::FormalReport:
		Save->Law.CorruptionCases += 1;
		Notify(FString::Printf(TEXT("%s made a formal report about something they actually know."), *Npc->Name));
		break;
	case EPuffersFallout::LeakFact:
		Notify(FString::Printf(TEXT("%s is repeating what they know. Expect rumors."), *Npc->Name));
		break;
	case EPuffersFallout::WithdrawAccess:
		Notify(FString::Printf(TEXT("%s stopped taking your calls."), *Npc->Name));
		break;
	case EPuffersFallout::CleanSplit:
		Notify(FString::Printf(TEXT("You and %s split. No hard feelings."), *Npc->Name));
		break;
	default:
		break;
	}
	SaveNow();
	return Fallout;
}

void UPuffersCareerSubsystem::BreakUpWithInsider(FName NpcId)
{
	if (Save)
	{
		FPuffersInsiderRules::BreakUp(Relationship(NpcId));
		SaveNow();
	}
}

bool UPuffersCareerSubsystem::GetInsider(FName NpcId, FPuffersInsiderRelationship& Out) const
{
	const FPuffersInsiderRelationship* Rel = Save ? Save->Insiders.Find(NpcId) : nullptr;
	if (Rel)
	{
		Out = *Rel;
	}
	return Rel != nullptr;
}
