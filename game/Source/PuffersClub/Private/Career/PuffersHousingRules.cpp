#include "Career/PuffersHousingRules.h"

bool FPuffersHousingRules::ApplyBillingEvent(FPuffersEntitlement& Entitlement, FName EventId, bool bPaid, bool bPending, int32 PeriodDays, const FDateTime& Now)
{
	if (EventId.IsNone() || Entitlement.ProcessedEvents.Contains(EventId))
	{
		return false;
	}
	Entitlement.ProcessedEvents.Add(EventId);
	if (bPending)
	{
		Entitlement.State = EPuffersEntitlementState::Unresolved;
		return true;
	}
	if (bPaid)
	{
		// Renewals extend from the paid-through date (or now, if it lapsed), never stack on themselves twice.
		const FDateTime Base = Entitlement.PaidThroughAt > Now ? Entitlement.PaidThroughAt : (Entitlement.PaidThroughAt.GetTicks() == 0 ? Now : Entitlement.PaidThroughAt);
		Entitlement.PaidThroughAt = Base + FTimespan::FromDays(FMath::Max(1, PeriodDays));
		Entitlement.State = EPuffersEntitlementState::Current;
		RefreshEntitlement(Entitlement, Now);
		return true;
	}
	Entitlement.State = Now >= Entitlement.PaidThroughAt ? EPuffersEntitlementState::VerifiedUnpaid : Entitlement.State;
	return true;
}

void FPuffersHousingRules::RefreshEntitlement(FPuffersEntitlement& Entitlement, const FDateTime& Now)
{
	if (Entitlement.State == EPuffersEntitlementState::Unresolved)
	{
		return;
	}
	Entitlement.State = Now < Entitlement.PaidThroughAt ? EPuffersEntitlementState::Current : EPuffersEntitlementState::VerifiedUnpaid;
}

bool FPuffersHousingRules::AdvanceTenancy(FPuffersApartmentTenancy& Tenancy, const FPuffersEntitlement& Entitlement, int32 GraceDays, const FDateTime& Now, FPuffersMarketplaceReturn& OutReturn, bool& bOutReclaimed)
{
	bOutReclaimed = false;
	if (Tenancy.State == EPuffersTenancyState::Reclaimed || Tenancy.State == EPuffersTenancyState::Ended)
	{
		return false;
	}
	const bool bPaidUp = Now < Entitlement.PaidThroughAt;

	if (Tenancy.State == EPuffersTenancyState::Current && !bPaidUp)
	{
		// The missed renewal locks everything. Snapshot the manifest.
		Tenancy.State = EPuffersTenancyState::LockedOverdue;
		Tenancy.LockedAt = Entitlement.PaidThroughAt;
		Tenancy.ReclaimAt = Entitlement.PaidThroughAt + FTimespan::FromDays(GraceDays);
		Tenancy.LockedItemCount = 0;
		for (const FPuffersManifestItem& Item : Tenancy.Manifest)
		{
			Tenancy.LockedItemCount += Item.Quantity;
		}
		// Fall through: a long absence can also pass the reclaim time in the same step.
	}

	if (Tenancy.State == EPuffersTenancyState::LockedOverdue)
	{
		if (bPaidUp && Entitlement.State == EPuffersEntitlementState::Current)
		{
			return Reinstate(Tenancy, Entitlement, Now);
		}
		// Keep it locked while billing is unresolved; confirm through the cutoff before committing loss.
		if (Now >= Tenancy.ReclaimAt && Entitlement.State == EPuffersEntitlementState::VerifiedUnpaid)
		{
			OutReturn.ReturnId = FName(*FString::Printf(TEXT("RET_%s"), *Tenancy.TenancyId.ToString()));
			OutReturn.FromTenancy = Tenancy.TenancyId;
			OutReturn.At = Now;
			for (const FPuffersManifestItem& Item : Tenancy.Manifest)
			{
				if (Item.bReclaimable)
				{
					OutReturn.Items.Add(Item);
				}
			}
			Tenancy.Manifest.RemoveAll([](const FPuffersManifestItem& Item) { return Item.bReclaimable; });
			Tenancy.State = EPuffersTenancyState::Reclaimed;
			bOutReclaimed = true;
		}
		return true;
	}
	return false;
}

bool FPuffersHousingRules::Reinstate(FPuffersApartmentTenancy& Tenancy, const FPuffersEntitlement& Entitlement, const FDateTime& Now)
{
	if (Tenancy.State != EPuffersTenancyState::LockedOverdue)
	{
		return false;
	}
	if (Entitlement.State != EPuffersEntitlementState::Current || Now >= Entitlement.PaidThroughAt || Now >= Tenancy.ReclaimAt)
	{
		return false;
	}
	Tenancy.State = EPuffersTenancyState::Current;
	return true;
}

bool FPuffersHousingRules::CanUseContents(const FPuffersApartmentTenancy& Tenancy)
{
	return Tenancy.State == EPuffersTenancyState::Current;
}

bool FPuffersHousingRules::IsQuarantined(const FPuffersApartmentTenancy& Tenancy)
{
	return Tenancy.State == EPuffersTenancyState::LockedOverdue;
}

bool FPuffersHousingRules::IsUnitTaken(const TArray<FPuffersBusinessApplication>& Applications, FName UnitId, FName ExceptApplication)
{
	return Applications.ContainsByPredicate([UnitId, ExceptApplication](const FPuffersBusinessApplication& A)
	{
		return A.UnitId == UnitId && A.ApplicationId != ExceptApplication
			&& (A.State == EPuffersApplicationState::Approved || A.State == EPuffersApplicationState::Leased);
	});
}

void FPuffersHousingRules::Review(FPuffersBusinessApplication& Application, const FPuffersCommercialVacancy& Vacancy, int64 AvailableCash,
	bool bCannabisUse, bool bOwnerCanHoldCannabisLicense, const TArray<FPuffersBusinessApplication>& AllApplications, float ReservationHours, const FDateTime& Now)
{
	if (Application.State != EPuffersApplicationState::Pending)
	{
		return;
	}
	auto Reject = [&Application](const FString& Why)
	{
		Application.State = EPuffersApplicationState::Rejected;
		Application.Reason = Why;
	};

	if (Vacancy.bResidential)
	{
		Reject(TEXT("That's a residential unit. Apartments use the apartment subscription."));
		return;
	}
	if (Vacancy.AllowedUses.Num() > 0 && !Vacancy.AllowedUses.Contains(Application.ArchetypeId))
	{
		Reject(FString::Printf(TEXT("Zoning doesn't allow a %s here."), *Application.ArchetypeId.ToString()));
		return;
	}
	if (bCannabisUse && !bOwnerCanHoldCannabisLicense)
	{
		Reject(TEXT("A cannabis use needs an owner who can hold a cannabis license."));
		return;
	}
	if (AvailableCash < Vacancy.Upfront)
	{
		Reject(FString::Printf(TEXT("You need %d Puff Cash available for the upfront payment."), Vacancy.Upfront));
		return;
	}
	if (IsUnitTaken(AllApplications, Vacancy.UnitId, Application.ApplicationId))
	{
		Reject(TEXT("Someone else already reserved this unit. Pick another storefront."));
		return;
	}
	Application.State = EPuffersApplicationState::Approved;
	Application.ReservedUntil = Now + FTimespan::FromHours(FMath::Max(1.f, ReservationHours));
	Application.Reason = FString::Printf(TEXT("Approved. Reserved for you until %s UTC."), *Application.ReservedUntil.ToString(TEXT("%Y-%m-%d %H:%M")));
}

int32 FPuffersHousingRules::ExpireReservations(TArray<FPuffersBusinessApplication>& Applications, const FDateTime& Now)
{
	int32 Expired = 0;
	for (FPuffersBusinessApplication& Application : Applications)
	{
		if (Application.State == EPuffersApplicationState::Approved && Now >= Application.ReservedUntil)
		{
			Application.State = EPuffersApplicationState::Expired;
			Application.Reason = TEXT("The reservation expired before you took possession.");
			++Expired;
		}
	}
	return Expired;
}

bool FPuffersHousingRules::AdvancePlot(FPuffersPlotOwnership& Plot, const FPuffersEntitlement& Entitlement, int32 AsBuiltPrice)
{
	const EPuffersPlotState Before = Plot.State;
	if (Plot.State == EPuffersPlotState::Sold)
	{
		return false;
	}
	switch (Entitlement.State)
	{
	case EPuffersEntitlementState::Current:
		if (Plot.State == EPuffersPlotState::RenewalUnresolved || Plot.State == EPuffersPlotState::SuspendedNoBuilding)
		{
			Plot.State = EPuffersPlotState::Current;
		}
		// A listed property comes off the market only through Cure (so a cure races cleanly against a sale).
		break;
	case EPuffersEntitlementState::Unresolved:
		if (Plot.State == EPuffersPlotState::Current)
		{
			Plot.State = EPuffersPlotState::RenewalUnresolved;
		}
		break;
	case EPuffersEntitlementState::VerifiedUnpaid:
		if (Plot.State == EPuffersPlotState::ListedAsBuilt)
		{
			break;
		}
		if (Plot.HasCompletedBuilding() && Plot.ForcedSales == 0)
		{
			Plot.State = EPuffersPlotState::ListedAsBuilt;
			Plot.ListedPrice = AsBuiltPrice;
			Plot.ListedBuildVersions.Reset();
			for (const FPuffersPlotBuilding& Building : Plot.Buildings)
			{
				Plot.ListedBuildVersions.Add(Building.BuildVersion);
			}
		}
		else
		{
			Plot.State = EPuffersPlotState::SuspendedNoBuilding;
		}
		break;
	}
	return Plot.State != Before;
}

bool FPuffersHousingRules::CanDevelop(const FPuffersPlotOwnership& Plot, const FPuffersEntitlement& Entitlement)
{
	return Plot.State == EPuffersPlotState::Current && Entitlement.State == EPuffersEntitlementState::Current;
}

bool FPuffersHousingRules::Cure(FPuffersPlotOwnership& Plot, FPuffersEntitlement& Entitlement)
{
	if (Plot.State == EPuffersPlotState::Sold || Entitlement.State != EPuffersEntitlementState::Current)
	{
		return false;
	}
	if (Plot.State == EPuffersPlotState::ListedAsBuilt || Plot.State == EPuffersPlotState::SuspendedNoBuilding || Plot.State == EPuffersPlotState::RenewalUnresolved)
	{
		Plot.State = EPuffersPlotState::Current;
		Plot.ListedPrice = 0;
		Plot.ListedBuildVersions.Reset();
		return true;
	}
	return false;
}

bool FPuffersHousingRules::CanBuyAsBuilt(const FPuffersPlotOwnership& Plot, FName BuyerId, const TArray<int32>& ExpectedVersions, FString& OutWhy)
{
	if (Plot.State == EPuffersPlotState::Sold)
	{
		OutWhy = TEXT("Already sold.");
		return false;
	}
	if (Plot.State != EPuffersPlotState::ListedAsBuilt)
	{
		OutWhy = TEXT("This property isn't for sale (the owner may have paid the missed fee).");
		return false;
	}
	if (BuyerId == Plot.OwnerId)
	{
		OutWhy = TEXT("The owner cures by paying the fee, not by buying.");
		return false;
	}
	if (ExpectedVersions != Plot.ListedBuildVersions)
	{
		OutWhy = TEXT("The listing changed. Inspect it again before buying.");
		return false;
	}
	return true;
}

void FPuffersHousingRules::SettleAsBuilt(FPuffersPlotOwnership& Plot, FName BuyerId, FName SaleTx, TArray<FName>& OutMovableItems)
{
	OutMovableItems.Reset();
	for (FPuffersPlotBuilding& Building : Plot.Buildings)
	{
		OutMovableItems.Append(Building.MovableItems);
		Building.MovableItems.Reset();
	}
	for (FPuffersPlayerLease& Lease : Plot.Leases)
	{
		if (Lease.bActive)
		{
			Lease.LandlordId = BuyerId;
		}
	}
	Plot.State = EPuffersPlotState::Sold;
	Plot.SoldTo = BuyerId;
	Plot.SaleTx = SaleTx;
	Plot.ForcedSales += 1;
}

int32 FPuffersHousingRules::PeriodIndex(const FPuffersPlayerLease& Lease, const FDateTime& Now)
{
	if (Now < Lease.StartsAt)
	{
		return -1;
	}
	const double Days = (Now - Lease.StartsAt).GetTotalDays();
	return static_cast<int32>(Days / FMath::Max(1, Lease.PeriodDays));
}

bool FPuffersHousingRules::MarkRentPosted(FPuffersPlayerLease& Lease, int32 Period)
{
	if (Period < 0 || Lease.PostedPeriods.Contains(Period))
	{
		return false;
	}
	Lease.PostedPeriods.Add(Period);
	return true;
}
