#include "Career/PuffersInsiderRules.h"
#include "Career/PuffersCountyLawRules.h"

bool FPuffersInsiderRules::CanStillAct(const FPuffersInsiderDefinition& Npc, const FPuffersInsiderRelationship& Rel, const FPuffersInsiderRequestDefinition& Request, bool bCountyAllowsCorruption, FString& OutWhy)
{
	if (Rel.bWithdrawn)
	{
		OutWhy = FString::Printf(TEXT("%s isn't working with you anymore."), *Npc.Name);
		return false;
	}
	if (Request.Role != Npc.Role)
	{
		OutWhy = FString::Printf(TEXT("%s can't do that in their role."), *Npc.Name);
		return false;
	}
	if (Request.bCorrupt && !bCountyAllowsCorruption)
	{
		OutWhy = TEXT("That kind of favor doesn't happen in this county.");
		return false;
	}
	return true;
}

FPuffersInsiderRequest FPuffersInsiderRules::Quote(const FPuffersInsiderDefinition& Npc, const FPuffersInsiderRelationship& Rel,
	const FPuffersInsiderRequestDefinition& Request, bool bCountyAllowsCorruption, FName QuoteId, const FDateTime& Now, FString& OutWhy)
{
	FPuffersInsiderRequest Quote;
	Quote.QuoteId = QuoteId;
	Quote.RequestId = Request.RequestId;
	Quote.QuotedAt = Now;
	Quote.DueAt = Now + FTimespan::FromHours(FMath::Max(1.f, Request.ReturnWindowHours));

	if (!CanStillAct(Npc, Rel, Request, bCountyAllowsCorruption, OutWhy))
	{
		Quote.Response = EPuffersInsiderResponse::Refuse;
		Quote.State = EPuffersRequestState::Declined;
		return Quote;
	}

	// Honest people report corrupt asks from someone they don't trust.
	if (Request.bCorrupt)
	{
		const float Roll = FPuffersCountyLawRules::StableRoll(QuoteId, 3);
		if (Npc.Integrity >= 0.8f && Rel.Trust < 50)
		{
			Quote.Response = Roll < Npc.Integrity ? EPuffersInsiderResponse::Report : EPuffersInsiderResponse::Refuse;
			Quote.State = EPuffersRequestState::Declined;
			OutWhy = Quote.Response == EPuffersInsiderResponse::Report
				? FString::Printf(TEXT("%s is reporting the offer."), *Npc.Name)
				: FString::Printf(TEXT("%s won't touch that."), *Npc.Name);
			return Quote;
		}
		if (Npc.Integrity + Request.Exposure * 0.5f > 0.9f && Rel.Trust < 70)
		{
			Quote.Response = EPuffersInsiderResponse::Refuse;
			Quote.State = EPuffersRequestState::Declined;
			OutWhy = FString::Printf(TEXT("%s says it's too risky."), *Npc.Name);
			return Quote;
		}
	}

	// Price: base x exposure x greed x prior demands, less a trust discount.
	const float ExposureFactor = 1.f + FMath::Clamp(Request.Exposure, 0.f, 1.f);
	const float GreedFactor = 1.f + FMath::Clamp(Npc.Greed, 0.f, 1.f) * 0.5f;
	const float DemandFactor = 1.f + 0.15f * Rel.DemandsMade * FMath::Clamp(Npc.Greed, 0.f, 1.f) * 2.f;
	const float TrustDiscount = FMath::Clamp(Rel.Trust / 200.f, 0.f, 0.4f);
	const float ResentmentFactor = 1.f + FMath::Clamp(Rel.Resentment / 100.f, 0.f, 1.f);
	Quote.Price = FMath::Max(1, FMath::RoundToInt(Request.BaseQuote * ExposureFactor * GreedFactor * DemandFactor * ResentmentFactor * (1.f - TrustDiscount)));

	// A close, trusted contact may do a low-exposure ask as a favor.
	if (!Request.bCorrupt && Rel.Trust >= 80 && Request.Exposure <= 0.3f)
	{
		Quote.bFavor = true;
		Quote.Price = 0;
	}

	// Greedy contacts with leverage counter instead of accepting.
	if (!Quote.bFavor && Npc.Greed >= 0.6f && Rel.DemandsMade > 0)
	{
		Quote.Response = EPuffersInsiderResponse::Counter;
		OutWhy = FString::Printf(TEXT("%s wants more this time: %d."), *Npc.Name, Quote.Price);
	}
	else
	{
		Quote.Response = EPuffersInsiderResponse::Accept;
		OutWhy = Quote.bFavor ? FString::Printf(TEXT("%s will do it as a favor."), *Npc.Name) : FString::Printf(TEXT("%s will do it for %d."), *Npc.Name, Quote.Price);
	}
	Quote.Terms = FString::Printf(TEXT("%s. Price %d Puff Cash, paid on completion; cancel before the due time for a full release."), *Request.Description, Quote.Price);
	return Quote;
}

bool FPuffersInsiderRules::GiveGift(const FPuffersInsiderDefinition& Npc, FPuffersInsiderRelationship& Rel, FName GiftId, bool bProfessionalScene, FString& OutWhy)
{
	// A gift in a professional scene can be a conflict for an honest official.
	if (bProfessionalScene && Npc.Integrity >= 0.7f && Npc.Role != EPuffersInsiderRole::Lawyer)
	{
		Rel.Resentment += 5;
		OutWhy = FString::Printf(TEXT("%s can't accept gifts on the job."), *Npc.Name);
		return false;
	}
	if (Npc.GiftDislikes.Contains(GiftId))
	{
		Rel.Resentment += 5;
		Rel.Affection = FMath::Max(0, Rel.Affection - 3);
		OutWhy = FString::Printf(TEXT("%s doesn't want that."), *Npc.Name);
		return false;
	}
	const bool bThoughtful = Npc.GiftLikes.Contains(GiftId);
	const bool bRepeat = Rel.GiftsGiven.Contains(GiftId);
	int32 Gain = bThoughtful ? 12 : 4;
	if (bRepeat)
	{
		Gain /= 3;
	}
	Rel.Affection = FMath::Clamp(Rel.Affection + Gain, 0, 100);
	Rel.GiftsGiven.Add(GiftId);
	OutWhy = bThoughtful ? FString::Printf(TEXT("%s loves it."), *Npc.Name) : FString::Printf(TEXT("%s says thanks."), *Npc.Name);
	return true;
}

bool FPuffersInsiderRules::AskForIntimacy(const FPuffersInsiderDefinition& Npc, FPuffersInsiderRelationship& Rel, FString& OutWhy)
{
	if (!Npc.bOpenToRomance)
	{
		OutWhy = FString::Printf(TEXT("%s keeps it professional."), *Npc.Name);
		return false;
	}
	if (Rel.bBrokenUp || Rel.bWithdrawn)
	{
		OutWhy = FString::Printf(TEXT("%s isn't interested anymore."), *Npc.Name);
		return false;
	}
	if (Rel.Affection < Npc.IntimacyAffection || Rel.Resentment >= Rel.Affection)
	{
		OutWhy = FString::Printf(TEXT("%s isn't ready for that."), *Npc.Name);
		return false;
	}
	Rel.bIntimate = true;
	Rel.bRomantic = true;
	OutWhy = FString::Printf(TEXT("%s says yes."), *Npc.Name);
	return true;
}

void FPuffersInsiderRules::StopIntimacy(FPuffersInsiderRelationship& Rel)
{
	Rel.bIntimate = false;
}

bool FPuffersInsiderRules::Learn(FPuffersInsiderRelationship& Rel, const FPuffersKnowledgeFact& Fact)
{
	if (Fact.FactId.IsNone() || Rel.Knowledge.ContainsByPredicate([&Fact](const FPuffersKnowledgeFact& K) { return K.FactId == Fact.FactId; }))
	{
		return false;
	}
	Rel.Knowledge.Add(Fact);
	return true;
}

bool FPuffersInsiderRules::Contradict(FPuffersInsiderRelationship& Rel, FName Subject)
{
	bool bAny = false;
	for (FPuffersKnowledgeFact& Fact : Rel.Knowledge)
	{
		if (Fact.Subject == Subject && Fact.bIsClaim && !Fact.bContradicted)
		{
			Fact.bContradicted = true;
			bAny = true;
		}
	}
	if (bAny)
	{
		Rel.Trust = FMath::Max(0, Rel.Trust - 15);
		Rel.Resentment += 10;
	}
	return bAny;
}

void FPuffersInsiderRules::KeepPromise(FPuffersInsiderRelationship& Rel, FName PromiseId)
{
	if (!Rel.PromisesKept.Contains(PromiseId) && !Rel.PromisesBroken.Contains(PromiseId))
	{
		Rel.PromisesKept.Add(PromiseId);
		Rel.Trust = FMath::Clamp(Rel.Trust + 10, 0, 100);
		Rel.Reliability = FMath::Clamp(Rel.Reliability + 10, 0, 100);
	}
}

void FPuffersInsiderRules::BreakPromise(FPuffersInsiderRelationship& Rel, FName PromiseId)
{
	if (!Rel.PromisesKept.Contains(PromiseId) && !Rel.PromisesBroken.Contains(PromiseId))
	{
		Rel.PromisesBroken.Add(PromiseId);
		Rel.Trust = FMath::Max(0, Rel.Trust - 20);
		Rel.Reliability = FMath::Max(0, Rel.Reliability - 15);
		AddGrievance(Rel, TEXT("BrokenPromise"), 25);
	}
}

void FPuffersInsiderRules::AddGrievance(FPuffersInsiderRelationship& Rel, FName Grievance, int32 Resentment)
{
	Rel.Grievances.Add(Grievance);
	Rel.Resentment = FMath::Clamp(Rel.Resentment + Resentment, 0, 100);
}

void FPuffersInsiderRules::BreakUp(FPuffersInsiderRelationship& Rel)
{
	Rel.bRomantic = false;
	Rel.bIntimate = false;
	Rel.bBrokenUp = true;
	Rel.LastFallout = EPuffersFallout::CleanSplit;
}

EPuffersFallout FPuffersInsiderRules::ResolveFallout(const FPuffersInsiderDefinition& Npc, FPuffersInsiderRelationship& Rel, FName& OutLeakedFact)
{
	OutLeakedFact = NAME_None;
	if (Rel.Resentment < 40)
	{
		Rel.LastFallout = Rel.bRomantic ? EPuffersFallout::CleanSplit : EPuffersFallout::None;
		if (Rel.bRomantic)
		{
			BreakUp(Rel);
		}
		return Rel.LastFallout;
	}

	// Something they actually know (never a contradicted claim) is what they can act on.
	const FPuffersKnowledgeFact* Material = Rel.Knowledge.FindByPredicate([](const FPuffersKnowledgeFact& F) { return F.bMaterial && !F.bContradicted; });
	const bool bJusticeMotive = Npc.Motives.Contains(TEXT("Justice")) || Npc.Integrity >= 0.7f;
	const bool bRevengeMotive = Npc.Motives.Contains(TEXT("Status")) || Npc.Motives.Contains(TEXT("Money")) || Rel.Resentment >= 70;

	Rel.bWithdrawn = true;
	Rel.bIntimate = false;
	Rel.bRomantic = false;

	if (Material && bJusticeMotive && Rel.Resentment >= 60)
	{
		OutLeakedFact = Material->FactId;
		Rel.LastLeakedFact = Material->FactId;
		Rel.LastFallout = EPuffersFallout::FormalReport;
		return Rel.LastFallout;
	}
	const FPuffersKnowledgeFact* Any = Material ? Material
		: Rel.Knowledge.FindByPredicate([](const FPuffersKnowledgeFact& F) { return !F.bContradicted; });
	if (Any && bRevengeMotive)
	{
		OutLeakedFact = Any->FactId;
		Rel.LastLeakedFact = Any->FactId;
		Rel.LastFallout = EPuffersFallout::LeakFact;
		return Rel.LastFallout;
	}
	Rel.LastFallout = EPuffersFallout::WithdrawAccess;
	return Rel.LastFallout;
}

TArray<FName> FPuffersInsiderRules::ObserveAndReport(const FPuffersInsiderRelationship& Rel, FName SubjectId, const FDateTime& From, const FDateTime& To)
{
	TArray<FName> Out;
	for (const FPuffersKnowledgeFact& Fact : Rel.Knowledge)
	{
		if (Fact.Subject == SubjectId && Fact.At >= From && Fact.At <= To)
		{
			Out.Add(Fact.FactId);
		}
	}
	return Out;
}
