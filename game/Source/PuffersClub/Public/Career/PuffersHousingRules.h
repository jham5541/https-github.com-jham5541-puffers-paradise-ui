// Pure housing rules: billing entitlements, apartment lock/reinstate/reclaim, business applications, plot
// lapses and as-built sales, player leases.

#pragma once

#include "CoreMinimal.h"
#include "Career/PuffersHousingTypes.h"

struct PUFFERSCLUB_API FPuffersHousingRules
{
	// --- Entitlements (creator billing) ---

	// Applies a billing callback once. bPaid extends PaidThroughAt by PeriodDays; bPending marks it Unresolved.
	// Returns false for a repeated event id.
	static bool ApplyBillingEvent(FPuffersEntitlement& Entitlement, FName EventId, bool bPaid, bool bPending, int32 PeriodDays, const FDateTime& Now);

	// Current until PaidThroughAt; after that VerifiedUnpaid unless a renewal is pending.
	static void RefreshEntitlement(FPuffersEntitlement& Entitlement, const FDateTime& Now);

	// --- Apartments ---

	// Advances one tenancy against its entitlement: lock at the missed renewal, reclaim after the grace period.
	// Returns true if the state changed. A reclaim appends one marketplace return (OutReturn valid).
	static bool AdvanceTenancy(FPuffersApartmentTenancy& Tenancy, const FPuffersEntitlement& Entitlement, int32 GraceDays, const FDateTime& Now, FPuffersMarketplaceReturn& OutReturn, bool& bOutReclaimed);

	// A confirmed renewal during grace restores the same tenancy and manifest.
	static bool Reinstate(FPuffersApartmentTenancy& Tenancy, const FPuffersEntitlement& Entitlement, const FDateTime& Now);

	// Entry, customization, deposit, withdrawal, use, sale and transfer of contents all need a current tenancy.
	static bool CanUseContents(const FPuffersApartmentTenancy& Tenancy);

	// Items in an overdue unit are unavailable to every game transaction, raids included.
	static bool IsQuarantined(const FPuffersApartmentTenancy& Tenancy);

	// --- Business applications ---

	// Checks the vacancy, use, affordability and reservation conflicts. Approves (reserves) or rejects with a reason.
	static void Review(FPuffersBusinessApplication& Application, const FPuffersCommercialVacancy& Vacancy, int64 AvailableCash,
		bool bCannabisUse, bool bOwnerCanHoldCannabisLicense, const TArray<FPuffersBusinessApplication>& AllApplications, float ReservationHours, const FDateTime& Now);

	// Expires approved reservations past their time. Returns how many expired.
	static int32 ExpireReservations(TArray<FPuffersBusinessApplication>& Applications, const FDateTime& Now);

	static bool IsUnitTaken(const TArray<FPuffersBusinessApplication>& Applications, FName UnitId, FName ExceptApplication);

	// --- Plots ---

	// Moves a plot through its state table from its entitlement. Returns true if the state changed.
	static bool AdvancePlot(FPuffersPlotOwnership& Plot, const FPuffersEntitlement& Entitlement, int32 AsBuiltPrice);

	// Owner rights to build, operate or sign new leases.
	static bool CanDevelop(const FPuffersPlotOwnership& Plot, const FPuffersEntitlement& Entitlement);

	// Paying the missed fee before a purchase commits takes the property off the market.
	static bool Cure(FPuffersPlotOwnership& Plot, FPuffersEntitlement& Entitlement);

	// Validates an as-built purchase: listed, versions unchanged, buyer not the owner, not already sold.
	static bool CanBuyAsBuilt(const FPuffersPlotOwnership& Plot, FName BuyerId, const TArray<int32>& ExpectedVersions, FString& OutWhy);

	// Transfers the plot and its builds to the buyer. Tenants keep their leases; future rent goes to the buyer once.
	// Movable items are returned in OutMovableItems (they stay with their owners).
	static void SettleAsBuilt(FPuffersPlotOwnership& Plot, FName BuyerId, FName SaleTx, TArray<FName>& OutMovableItems);

	// --- Player leases ---

	// Rent period index for a moment (0-based from the lease start). -1 before the start.
	static int32 PeriodIndex(const FPuffersPlayerLease& Lease, const FDateTime& Now);

	// Marks the period posted. False if it was already posted.
	static bool MarkRentPosted(FPuffersPlayerLease& Lease, int32 Period);
};
