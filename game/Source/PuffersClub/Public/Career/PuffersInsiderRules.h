// Pure insider rules: quotes, gifts, consent, knowledge with provenance, promises and fallout.

#pragma once

#include "CoreMinimal.h"
#include "Career/PuffersInsiderTypes.h"

struct PUFFERSCLUB_API FPuffersInsiderRules
{
	// Quote a request. Considers role, county capability, effort/exposure, motives (greed), history and prior demands.
	// Response is Accept (at Price), Counter (higher price; needs explicit acceptance), Refuse or Report.
	static FPuffersInsiderRequest Quote(const FPuffersInsiderDefinition& Npc, const FPuffersInsiderRelationship& Rel,
		const FPuffersInsiderRequestDefinition& Request, bool bCountyAllowsCorruption, FName QuoteId, const FDateTime& Now, FString& OutWhy);

	// A gift's effect from the recipient's preferences and the circumstances. Returns false if refused.
	static bool GiveGift(const FPuffersInsiderDefinition& Npc, FPuffersInsiderRelationship& Rel, FName GiftId, bool bProfessionalScene, FString& OutWhy);

	// The player asks for intimacy. The NPC decides from boundaries and affection. A no never affects the professional route.
	static bool AskForIntimacy(const FPuffersInsiderDefinition& Npc, FPuffersInsiderRelationship& Rel, FString& OutWhy);

	// Either side stops. Nothing else changes.
	static void StopIntimacy(FPuffersInsiderRelationship& Rel);

	// Writes a fact the NPC actually observed or was told. Duplicate fact ids are ignored.
	static bool Learn(FPuffersInsiderRelationship& Rel, const FPuffersKnowledgeFact& Fact);

	// Marks a remembered claim as contradicted when the NPC later observes the truth.
	static bool Contradict(FPuffersInsiderRelationship& Rel, FName Subject);

	static void KeepPromise(FPuffersInsiderRelationship& Rel, FName PromiseId);
	static void BreakPromise(FPuffersInsiderRelationship& Rel, FName PromiseId);

	// Public humiliation, using them as leverage, exposing the relationship publicly...
	static void AddGrievance(FPuffersInsiderRelationship& Rel, FName Grievance, int32 Resentment);

	// Picks fallout from motive, grievances, actual knowledge and role. Betrayal needs a credible motive and something to act on.
	static EPuffersFallout ResolveFallout(const FPuffersInsiderDefinition& Npc, FPuffersInsiderRelationship& Rel, FName& OutLeakedFact);

	// A respectful breakup.
	static void BreakUp(FPuffersInsiderRelationship& Rel);

	// ObserveAndReport: only facts about the subject the NPC saw or heard, in the window. Returns the fact ids.
	static TArray<FName> ObserveAndReport(const FPuffersInsiderRelationship& Rel, FName SubjectId, const FDateTime& From, const FDateTime& To);

	// Can this insider still do the request? Rechecked at acceptance and at completion.
	static bool CanStillAct(const FPuffersInsiderDefinition& Npc, const FPuffersInsiderRelationship& Rel, const FPuffersInsiderRequestDefinition& Request, bool bCountyAllowsCorruption, FString& OutWhy);
};
