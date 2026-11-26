// Insider relationships (handoff Rev 12, "Insider relationships and county corruption").
//
// Lawyers, county officials and officers can become professional contacts, friends, consensual romantic partners,
// paid contacts, or a mix. Each tracks affection, trust, reliability and resentment separately (no single
// romance or bribery meter), plus what they actually know and where it came from.
//
// Consent and favors are separate decisions: declining intimacy never closes the professional route, and paying
// for a task settles that task with no relationship or intimacy entitlement.

#pragma once

#include "CoreMinimal.h"
#include "PuffersInsiderTypes.generated.h"

UENUM(BlueprintType)
enum class EPuffersInsiderRole : uint8
{
	Lawyer,
	CountyOfficial,
	Officer
};

UENUM(BlueprintType)
enum class EPuffersFactSource : uint8
{
	Observed,   // saw a tagged scene detail
	Told,       // the player shared it directly
	Hearsay     // heard it from someone else
};

UENUM(BlueprintType)
enum class EPuffersInsiderResponse : uint8
{
	Accept,
	Counter,   // a different price; needs the player's explicit acceptance
	Refuse,
	Report     // reports the offer (corrupt requests only)
};

UENUM(BlueprintType)
enum class EPuffersRequestState : uint8
{
	Quoted,
	Accepted,
	Completed,
	Failed,
	Exposed,
	Abandoned,
	Declined
};

UENUM(BlueprintType)
enum class EPuffersFallout : uint8
{
	None,
	CleanSplit,
	WithdrawAccess,
	LeakFact,      // repeats one specific fact they actually know
	FormalReport   // reports something material they know
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersInsiderRequestDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EPuffersInsiderRole Role = EPuffersInsiderRole::Lawyer;

	// Needs a county with insider corruption enabled.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bCorrupt = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 BaseQuote = 100;

	// 0..1, raises the quote and the chance of a refusal or report.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Exposure = 0.f;

	// An ObserveAndReport job returns only what the insider actually saw or heard in the window.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bObserveAndReport = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float ReturnWindowHours = 24.f;

	// Game-defined outcome tags (Lead, Evidence, InspectionPreview, SchedulingFavor, Representation...).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> OutcomeTags;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersInsiderDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName NpcId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EPuffersInsiderRole Role = EPuffersInsiderRole::Lawyer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName CountyId;

	// 0..1. Honest contacts refuse or report corrupt asks.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Integrity = 0.5f;

	// 0..1. Greedy contacts raise later quotes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Greed = 0.3f;

	// Adult, consenting, and open to a romantic relationship at all. The NPC still decides each time.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bOpenToRomance = false;

	// Affection needed before they'd say yes to intimacy.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 IntimacyAffection = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> GiftLikes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> GiftDislikes;

	// Their own motives, used to pick fallout and betrayals (Career, Money, Family, Justice, Status).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> Motives;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> Requests;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Bio;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersKnowledgeFact
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName FactId;

	// What the fact is about ("Player_MetCouncilman", "Player_StreetSale"...).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName Subject;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersFactSource Source = EPuffersFactSource::Observed;

	// Who told them (Told / Hearsay).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName FromId;

	// A claim is remembered as a claim; it can be contradicted later.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bIsClaim = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bContradicted = false;

	// Material enough to report (a crime they witnessed, a corrupt deal they took part in).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bMaterial = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime At;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SceneId;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersInsiderRequest
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName QuoteId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName RequestId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersInsiderResponse Response = EPuffersInsiderResponse::Accept;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersRequestState State = EPuffersRequestState::Quoted;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Price = 0;

	// A trusted contact may do it as a favor (owed back, no money).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bFavor = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString Terms;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime QuotedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime DueAt;

	// The fee hold and the settlement transaction (each posts once).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName HoldId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SettlementTx;

	// What an ObserveAndReport job returned (fact ids with provenance).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> ReportedFacts;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SubjectId;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersInsiderRelationship
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName NpcId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Affection = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Trust = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Reliability = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Resentment = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bFriend = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bRomantic = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bPaidArrangement = false;

	// Both said yes. Either can stop at any time.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bIntimate = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bBrokenUp = false;

	// Stopped working with the player (a sour ending). The professional route is closed for this contact.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bWithdrawn = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> Grievances;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> PromisesKept;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> PromisesBroken;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersKnowledgeFact> Knowledge;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersInsiderRequest> Requests;

	// Requests asked so far (greedy contacts raise each next quote).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 DemandsMade = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> GiftsGiven;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersFallout LastFallout = EPuffersFallout::None;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LastLeakedFact;
};
