// Shared-world housing and property (handoff Rev 12, "City Sample apartment rent", "City Sample business
// applications", "Outskirts plots, player landlords, and as-built sales").
//
// * City apartments: a creator-billed monthly subscription (outside the Puff Cash wallet). A missed renewal locks the
//   unit; renewal within the 30-day grace restores the same tenancy and contents; after that the contents return to
//   marketplace stock once, with no payout.
// * Outskirts plots: bought with Puff Cash, but building or operating needs a creator-billed plot subscription.
//   Jayde (Nov 26): keep it, it pays for servers. A verified lapse on a completed building sells the whole property
//   as built (tenants keep their leases); the owner can cure until a purchase commits.
// * City businesses apply for vacant commercial units and lease them in Puff Cash.
// * Players can rent what they build to other players in Puff Cash.
// Billing goes through an entitlement adapter; development uses the mock in UPuffersCareerSubsystem.

#pragma once

#include "CoreMinimal.h"
#include "PuffersHousingTypes.generated.h"

UENUM(BlueprintType)
enum class EPuffersEntitlementKind : uint8
{
	Apartment,
	Plot
};

UENUM(BlueprintType)
enum class EPuffersEntitlementState : uint8
{
	Current,
	Unresolved,      // a renewal is pending reconciliation; nothing final can happen
	VerifiedUnpaid
};

UENUM(BlueprintType)
enum class EPuffersTenancyState : uint8
{
	Current,
	LockedOverdue,
	Reclaimed,
	Ended
};

UENUM(BlueprintType)
enum class EPuffersPlotState : uint8
{
	Current,
	RenewalUnresolved,
	SuspendedNoBuilding,   // unpaid with no completed building: development and operation suspended
	ListedAsBuilt,         // unpaid with a completed building: whole property for sale as built
	Sold
};

UENUM(BlueprintType)
enum class EPuffersApplicationState : uint8
{
	Pending,
	Approved,     // reserved for this applicant until ReservedUntil
	Rejected,
	Expired,
	Leased,
	Withdrawn
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersEntitlement
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName EntitlementId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersEntitlementKind Kind = EPuffersEntitlementKind::Apartment;

	// Unit id or plot id.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SubjectId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersEntitlementState State = EPuffersEntitlementState::Current;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime PaidThroughAt;

	// Billing callbacks already applied (repeats are ignored).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> ProcessedEvents;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersManifestItem
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ItemInstanceId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName Sku;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Quantity = 1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName OwnerId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SourceOrder;

	// Placed in the room (a reference to the owned instance, never a copy) or stored.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bPlaced = false;

	// Account-bound rewards and real-value entitlements can't live in reclaimable storage.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bReclaimable = true;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersApartmentListing
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName UnitId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Bedrooms = 1;

	// Shown for information. Billed by the creator subscription, never in Puff Cash.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString MonthlyFeeLabel = TEXT("$4.99 / month");

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 StorageSlots = 20;

	// Must be inside the configured City Sample city area.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bInCityArea = true;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersApartmentTenancy
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName TenancyId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName UnitId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName EntitlementId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersTenancyState State = EPuffersTenancyState::Current;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime StartedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime LockedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime ReclaimAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersManifestItem> Manifest;

	// Total item count snapshotted at lock time.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 LockedItemCount = 0;
};

// Marketplace stock returned by reclaims, with provenance.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersMarketplaceReturn
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ReturnId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName FromTenancy;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersManifestItem> Items;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime At;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCommercialVacancy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName UnitId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Building;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Floor = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 FootprintM2 = 80;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName CountyId;

	// Archetypes allowed here (zoning).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> AllowedUses;

	// Upfront Puff Cash charged once on taking possession.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Upfront = 500;

	// Weekly Puff Cash lease.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 WeeklyRent = 150;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Capacity = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> RequiredApprovals;

	// Residential units can't take the commercial path.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bResidential = false;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersBusinessApplication
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ApplicationId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ApplicantId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName UnitId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ArchetypeId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString BusinessName;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName OperatorId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersApplicationState State = EPuffersApplicationState::Pending;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString Reason;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime SubmittedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime ReservedUntil;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName UpfrontTx;

	// The operation created on taking possession.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName OperationId;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersPlotListing
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName PlotId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName CountyId;

	// 20 m grid cells (2 = starter, 64 = ranch).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Cells = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Price = 5000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MaxBuildings = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> AllowedUses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString MonthlyFeeLabel = TEXT("$2.99 / month");

	// The L_Outskirts build-zone lot this plot maps to.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName BuildZoneLotId;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersPlayerLease
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LeaseId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName BuildingId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName TenantId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 RentPerPeriod = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Deposit = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 PeriodDays = 7;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString UseRestrictions;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime StartsAt;

	// Rent periods already posted (period index), so each posts once.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<int32> PostedPeriods;

	// Who receives rent now (changes once on an as-built sale).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LandlordId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bActive = true;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersPlotBuilding
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName BuildingId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bCompleted = false;

	// Approved use: Home, Shop, Venue, Attraction.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName Use;

	// Bumped on every approved edit. An as-built purchase must match the listed version.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 BuildVersion = 1;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> PermanentFixtures;

	// Movable equipment and personal items stay with their owners on a sale.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> MovableItems;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersPlotOwnership
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName PlotId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName OwnerId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName EntitlementId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersPlotState State = EPuffersPlotState::Current;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName PurchaseTx;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersPlotBuilding> Buildings;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersPlayerLease> Leases;

	// As-built listing (forced sale) snapshot.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ListedPrice = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<int32> ListedBuildVersions;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SoldTo;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SaleTx;

	// A plot can have one forced sale and transfer once per listing.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ForcedSales = 0;

	bool HasCompletedBuilding() const
	{
		return Buildings.ContainsByPredicate([](const FPuffersPlotBuilding& B) { return B.bCompleted; });
	}
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersHousingState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersEntitlement> Entitlements;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersApartmentTenancy> Tenancies;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersMarketplaceReturn> MarketplaceReturns;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersBusinessApplication> Applications;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersPlotOwnership> Plots;
};
