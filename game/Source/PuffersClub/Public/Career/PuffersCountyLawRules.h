// Pure rules for county law: version selection, charging, the review queue, the NPC judge, publication,
// bribery outcomes and turning a conviction into custody blocks.

#pragma once

#include "CoreMinimal.h"
#include "Career/PuffersCountyLawTypes.h"

struct PUFFERSCLUB_API FPuffersCountyLawRules
{
	static constexpr int64 MgPerGram = 1000;
	// 6 lb = 2721.554 g, rounded down to the supported precision.
	static constexpr int64 SixPoundsMg = 2721554;
	static constexpr int64 SeedFineOnlyCapMg = 3500;

	// Seeds each county's first version if it has none.
	static void SeedCounties(FPuffersCountyLawState& State, const TArray<FPuffersCountyDefinition>& Counties);

	// The version in force in a county at a moment (latest EffectiveAt <= At). Null if the county has none.
	static const FPuffersCountyLawVersion* SelectVersion(const FPuffersCountyLawState& State, FName CountyId, const FDateTime& At);

	static int32 LatestVersionNumber(const FPuffersCountyLawState& State, FName CountyId);

	// Charges for an incident under the law version in force at the conduct. The firearm rule beats possession thresholds.
	static FPuffersCaseOutcome EvaluateCase(const FPuffersCountyLawVersion& Law, const FPuffersCaseFacts& Facts);

	// Years a conviction imposes: the court's term, floored by the firearm minimum when it applies.
	static float SentenceYears(const FPuffersCaseOutcome& Outcome, float CourtYears);

	// Custody blocks for imposed years with a scale (at least 1 block for any positive term).
	static int32 YearsToBlocks(float Years, const FPuffersCustodyScale& Scale);

	// A generated sentence-service definition for a county term.
	static FPuffersSentenceDefinition MakeSentenceDefinition(FName SentenceId, int32 Blocks, const FPuffersCustodyScale& Scale);

	// Adds a verified incident once (deduplicated by id). Returns false for duplicates, unverified or practice incidents.
	static bool AddIncident(FPuffersCountyLawState& State, const FPuffersViolentIncident& Incident);

	// Weighted verified incidents in the county's review window ending at Now.
	static float ViolenceInWindow(const FPuffersCountyLawState& State, const FPuffersCountyDefinition& County, const FDateTime& Now);

	static FPuffersPolicyProposal* FindProposal(FPuffersCountyLawState& State, FName ProposalId);
	static const FPuffersPolicyProposal* FindOpenProposal(const FPuffersCountyLawState& State, FName CountyId, EPuffersProposalKind Kind);

	// Adds documented support once per source. Only while drafting or in review.
	static bool AddSupport(FPuffersPolicyProposal& Proposal, FName SourceId, int32 Amount);

	// NPC judge: a deterministic review. Reform: approve at RequiredSupport, defer at half, else reject.
	// Firearm minimum: approve at the county's approval threshold, defer while above half of it, else reject.
	static EPuffersJudgeDecision NpcJudge(const FPuffersPolicyProposal& Proposal, const FPuffersProposalTemplate& Template, float ViolenceWeight, const FPuffersCountyDefinition& County, FString& OutReason);

	// Records a decision. Approval schedules activation. Only Review or Deferred proposals can be decided.
	static bool Decide(FPuffersPolicyProposal& Proposal, const FPuffersProposalTemplate& Template, EPuffersJudgeDecision Decision, EPuffersJudgeKind Judge, FName JudgeId, const FString& Reason, const FDateTime& Now);

	// Publishes every scheduled proposal whose activation time has passed. Returns the proposals that took effect.
	static TArray<FName> PublishDue(FPuffersCountyLawState& State, const TArray<FPuffersProposalTemplate>& Templates, const FDateTime& Now);

	// Bribe outcome from the official's integrity (0..1), greed (0..1), the amount against their price, and a stable
	// roll derived from the offer id so reloading never rerolls it.
	static EPuffersBribeOutcome ResolveBribe(FName OfferId, float Integrity, float Greed, int32 Amount, int32 Price);

	// Stable 0..1 roll for an id.
	static float StableRoll(FName Id, int32 Salt);

	static FString DescribeLaw(const FPuffersCountyLawVersion& Law);
};
