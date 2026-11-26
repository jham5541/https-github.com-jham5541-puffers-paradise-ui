// Versioned county law, proposals, the judge, bribery and generic county sentences
// (handoff Rev 12, "County policy, lobbying, and corruption" and "Custody chapter and release rules").
//
// * Each county has an ordered list of law versions. A case uses the version in effect at the place and time of
//   the conduct, and keeps it even after the law changes.
// * Mass is integer milligrams. Caps are inclusive. 3.5 g = 3500 mg; 6 lb = 2721554 mg (rounded down).
// * Proposals move Drafting -> Review -> Approved/Rejected/Deferred -> Scheduled -> Effective. Only the judge
//   (NPC offline or as fallback, an authorized server judge online) can publish a version.
// * Bribes change an official's discretionary handling at most. They never touch published law, strikes or sentences.

#pragma once

#include "CoreMinimal.h"
#include "Law/PuffersSentenceTypes.h"
#include "PuffersCountyLawTypes.generated.h"

UENUM(BlueprintType)
enum class EPuffersProposalKind : uint8
{
	PossessionReform,   // raises the no-prison possession cap (e.g. to 6 lb)
	FirearmMinimum,     // cannabis + firearm co-possession carries a minimum term
	Custom
};

UENUM(BlueprintType)
enum class EPuffersProposalState : uint8
{
	Drafting,
	Review,
	Approved,
	Rejected,
	Deferred,
	Scheduled,
	Effective
};

UENUM(BlueprintType)
enum class EPuffersJudgeKind : uint8
{
	NPC,
	Server
};

UENUM(BlueprintType)
enum class EPuffersJudgeDecision : uint8
{
	Approve,
	Reject,
	Defer
};

UENUM(BlueprintType)
enum class EPuffersBribeOutcome : uint8
{
	Accepted,            // a temporary change to the official's discretionary handling
	Refused,
	Reported,            // opens a corruption case and costs civic support
	LaterInvestigation   // accepted now, investigated later
};

// A fine/custody band above the fine-only cap. Bands are checked in order; the first whose cap covers the mass applies.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersPossessionBand
{
	GENERATED_BODY()

	// Inclusive upper bound in mg. -1 = no upper bound.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int64 MaxMg = -1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Fine = 0;

	// Simulated years of custody a conviction can carry (0 = fine only).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CustodyYears = 0.f;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCountyLawVersion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	FName CountyId;

	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	FDateTime EffectiveAt;

	// Covered possession above 0 and at or below this is fine-only, zero custody.
	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	int64 FineOnlyCapMg = 3500;

	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	int32 FineOnlyFine = 100;

	// Above the cap.
	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	TArray<FPuffersPossessionBand> Bands;

	// Cannabis (any positive amount) + a firearm in the same incident. 0 = no such offense in this version.
	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	float FirearmMinimumYears = 0.f;

	// Insider corruption content is enabled in this county.
	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	bool bInsiderCorruption = false;

	// Proposal that produced this version (None for the seed).
	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	FName FromProposal;
};

// How imposed years turn into custody blocks. Versioned; the scale is stored with each sentence at sentencing.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCustodyScale
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	int32 Version = 1;

	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	float BlocksPerSimulatedYear = 3.f;

	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	int32 BlocksPerDay = 3;

	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	float MinActiveSecondsPerBlock = 100.f;

	// Routine mission played for each generic block.
	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	FName RoutineMission = TEXT("CUS_ROUTINE");

	UPROPERTY(EditAnywhere, SaveGame, BlueprintReadOnly)
	FName FacilityId = TEXT("PRISON_PuffingtonState");
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersProposalTemplate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EPuffersProposalKind Kind = EPuffersProposalKind::Custom;

	// New fine-only cap if adopted (PossessionReform). -1 = unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int64 NewFineOnlyCapMg = -1;

	// New co-possession minimum if adopted (FirearmMinimum). -1 = unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float NewFirearmMinimumYears = -1.f;

	// Documented support the NPC judge needs to approve a reform.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RequiredSupport = 100;

	// Hours from approval to activation.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ActivationDelayHours = 24.f;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCountyDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName CountyId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Name;

	// County ids from Content/Data/Law/Counties.json (the map's pie slices) that belong to this county.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> MapCountyIds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FPuffersCountyLawVersion SeedLaw;

	// Verified violent street-deal incidents are summed (weighted) over this window.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ViolenceReviewWindowDays = 14.f;

	// Weighted total that opens a firearm-minimum review.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ViolenceThreshold = 10.f;

	// Weighted total the NPC judge needs to approve the firearm minimum once in review.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ViolenceApprovalThreshold = 10.f;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersPolicyProposal
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ProposalId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName TemplateId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CountyId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersProposalKind Kind = EPuffersProposalKind::Custom;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersProposalState State = EPuffersProposalState::Drafting;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SponsorId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Support = 0;

	// Each support source counts once (a meeting, testimony, a completed commitment...).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> SupportSources;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime CreatedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime DecidedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime ActivationAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersJudgeKind DecidedBy = EPuffersJudgeKind::NPC;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName JudgeId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString Reason;

	// Evidence summary shown to the judge.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString EvidenceSummary;

	// Version published on activation.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 PublishedVersion = 0;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersViolentIncident
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName IncidentId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CountyId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime At;

	// Only verified, non-practice incidents count. Reports, virality and replays never add incidents.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bVerified = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bPractice = false;

	// 1 for a violent deal, more for findings such as injury.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	float Weight = 1.f;
};

// Verified facts of one incident, as recorded at the time.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCaseFacts
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FName CaseId;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FName CountyId;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FDateTime ConductAt;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int64 CannabisMg = 0;

	// Evidence of a firearm in the SAME incident. Ownership, prior incidents or items stored elsewhere never count.
	UPROPERTY(SaveGame, BlueprintReadWrite)
	bool bFirearmInIncident = false;

	// Documented licensed wholesale inventory is tracked separately and isn't personal possession.
	UPROPERTY(SaveGame, BlueprintReadWrite)
	bool bDocumentedLicensedInventory = false;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FName CharacterId;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCaseOutcome
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CaseId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 LawVersion = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bCharge = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Fine = 0;

	// Custody a conviction can carry for possession alone.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	float PossessionCustodyYears = 0.f;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bFirearmOffense = false;

	// Floor at sentencing if convicted (the firearm minimum). Never auto-imposed.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	float MinimumYearsIfConvicted = 0.f;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString Explanation;
};

// A county conviction turned into custody blocks with the scale in force at sentencing.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCountySentence
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SentenceId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CaseId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 LawVersion = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	float ImposedYears = 0.f;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ImposedBlocks = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FPuffersCustodyScale Scale;

	// The definition handed to the sentence service (persisted so a reload resumes the same term).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FPuffersSentenceDefinition Definition;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersBribeRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName OfferId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ActorId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName OfficialId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Amount = 0;

	// What the bribe tries to change ("Inspection", "Patrol", "Scheduling"...). Never law, strikes or sentences.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName Scope;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersBribeOutcome Outcome = EPuffersBribeOutcome::Refused;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime At;

	// Discretionary favor expires here (Accepted / LaterInvestigation).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime FavorUntil;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName TxId;
};

// Everything the law service persists.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCountyLawState
{
	GENERATED_BODY()

	// Every published version, all counties, in publication order.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersCountyLawVersion> Versions;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersPolicyProposal> Proposals;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersViolentIncident> Incidents;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TMap<FName, FPuffersCaseFacts> Cases;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TMap<FName, FPuffersCaseOutcome> Outcomes;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersCountySentence> Sentences;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersBribeRecord> Bribes;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 CorruptionCases = 0;
};
