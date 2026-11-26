// The open-world career owner (handoff Rev 12, "Career and ownership systems").
//
// The magnate loop: earn wages from ordinary work, then own (a licensed shop, or a permanent street commitment),
// expand, and shape Puffington. Story missions are optional opportunities on top of this loop.
//
// Route and licensing state lives on the persistent career owner for the world, never on the selected Puffer, so
// switching characters can't launder a strike. The rules are identical in a local save and on a shared server.

#pragma once

#include "CoreMinimal.h"
#include "PuffersCareerTypes.generated.h"

UENUM(BlueprintType)
enum class EPuffersCareerRoute : uint8
{
	Uncommitted,          // ordinary work and non-owner shifts only
	LicensedOnly,         // holds or held a cannabis license; never took the street commitment
	StreetDualEligible,   // street committed (permanent); may also hold licenses until the first confirmed strike
	StreetForfeited       // street committed and cannabis licenses permanently forfeited in this career save
};

UENUM(BlueprintType)
enum class EPuffersOperationKind : uint8
{
	LicensedCannabis,   // storefront or grow under a county license
	Street,             // street stock, crew contracts and territory
	NonCannabis         // restaurant, bodega, garage, nightclub, merch... never affected by a cannabis strike
};

UENUM(BlueprintType)
enum class EPuffersLicenseState : uint8
{
	Active,
	Revoked,    // one-strike revocation; never comes back in this career save
	Closed      // given up voluntarily or the premises closed
};

// What an enforcement record says happened. Only a verified finding of qualifying street activity is a strike.
UENUM(BlueprintType)
enum class EPuffersFindingKind : uint8
{
	WitnessClaim,              // someone said so; starts an investigation, never a strike
	OutfitMatch,               // looked like the suspect; never a strike
	Association,               // seen with the wrong people; never a strike
	UnrelatedStop,             // stopped for something else
	QualifyingStreetActivity   // e.g. caught in an actual street sale
};

UENUM(BlueprintType)
enum class EPuffersStrikeResult : uint8
{
	NotQualifying,     // recorded, no licensing effect
	FirstStrike,       // licenses revoked and future grants barred
	AdditionalStrike,  // already forfeited; kept on the record
	Duplicate          // this finding id was already recorded
};

// Content/Data/Career/Career.json -> Archetypes. One per business type the player can run.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersBusinessArchetype
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EPuffersOperationKind Kind = EPuffersOperationKind::NonCannabis;

	// Puff Cash to open (lease deposit and fixtures for a storefront; stock and crew for a street operation).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 EntryCost = 0;

	// Staff needed before it can open.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RequiredStaff = 0;

	// Fixtures that must be installed before opening.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> RequiredFixtures;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> StockCategories;

	// Optional upgrades that open new decisions (second counter, workshop, manager's office).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> Upgrades;

	// Weekly overhead shown on the operating statement before profit.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 WeeklyOverhead = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Description;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersLicense
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LicenseId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName OperationId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CountyId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersLicenseState State = EPuffersLicenseState::Active;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime IssuedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime EndedAt;

	// Granted while the owner was street committed (a dual-operation license).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bDualOperation = false;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersOperation
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName OperationId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ArchetypeId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersOperationKind Kind = EPuffersOperationKind::NonCannabis;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString DisplayName;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CountyId;

	// Premises (a commercial unit, a plot building, or None for street work).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName PremisesId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bOpen = false;

	// A revoked license closes regulated sales; the operation's other records stay.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bRegulatedSalesClosed = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> InstalledFixtures;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> Upgrades;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Stock = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ManagerId;

	// Lifetime net profit from customers (never from other players, so circular trades don't count as growth).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 NetProfit = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime OpenedAt;

	// Weeks in a row this operation made a profit (licensed civic access needs a sustained run).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ProfitableWeeks = 0;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersEnforcementFinding
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName FindingId;

	// The playable Puffer involved. Evidence stays on the character; the strike lands on the career owner.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CharacterId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersFindingKind Kind = EPuffersFindingKind::WitnessClaim;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bIdentityConfirmed = false;

	// Issued by the enforcement authority (police finding), not a civilian claim.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bAuthoritative = false;

	// A supported mistaken-identity correction invalidates the finding (kept for the audit trail).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bInvalidated = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString InvalidationReason;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CountyId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime At;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bCountedAsStrike = false;

	bool IsQualifyingStrike() const
	{
		return Kind == EPuffersFindingKind::QualifyingStreetActivity && bIdentityConfirmed && bAuthoritative && !bInvalidated;
	}
};

// Persistent route and licensing state for one career owner in one world.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCareerOwnerState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CareerOwnerId;

	// Sticky. Survives selling or closing the street operation.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bStreetCommitted = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime StreetCommittedAt;

	// Has ever held a cannabis license.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bEverLicensed = false;

	// One strike: permanent seller disqualification for this career save.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bLicensesForfeited = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ForfeitedByFinding;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersLicense> Licenses;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersOperation> Operations;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersEnforcementFinding> Findings;

	// Licensed sales reserved (prepaid) but not yet committed. Revocation releases them once.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> PendingRegulatedSales;
};

// One phase of the 400-hour career target (Career.json -> Phases).
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCareerPhase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 TargetHours = 0;

	// Requirements to enter this phase. All are "at least".
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int64 MinLifetimeWages = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MinOperations = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int64 MinNetCannabisProfit = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MinCounties = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MinInfluence = 0;
};

// The standing metrics shown on the career screen, with what moved them.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCareerStanding
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 LifetimeWages = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 NetCannabisProfit = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 StaffedCapacity = 0;

	// Licensed influence: market reach + civic support.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 MarketReach = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 CivicSupport = 0;

	// Street influence: territory + crew relationships.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Territory = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 CrewStanding = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName PhaseId;

	// Active play seconds per phase (AFK, menus, custody idling and offline time excluded).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TMap<FName, double> ActiveSecondsByPhase;

	// Latest reasons standing changed, newest last (capped).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FString> RecentChanges;

	int32 LicensedInfluence() const { return MarketReach + CivicSupport; }
	int32 StreetInfluence() const { return Territory + CrewStanding; }
};

// "Earn your first operation" (the first Unreal build). Each milestone ticks when its event happens.
UENUM(BlueprintType)
enum class EPuffersFirstBuildMilestone : uint8
{
	PaidShift,
	LicensedOwnership,
	StreetOwnership,
	DualOperation,
	MistakenMatchCleared,
	VerifiedStrike,
	RecoveryJob,
	LawSeeded,
	ReformPassed,
	FirearmMinimumPublished,
	CountyConviction
};
