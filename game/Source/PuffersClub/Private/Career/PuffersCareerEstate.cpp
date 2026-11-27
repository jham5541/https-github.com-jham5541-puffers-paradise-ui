// Care, insurance, raids and estate transfer; apartments, business applications, plots and player leases.

#include "Career/PuffersCareerSubsystem.h"
#include "Career/PuffersCareerRules.h"
#include "Career/PuffersEstateRules.h"
#include "Career/PuffersHousingRules.h"
#include "Career/PuffersWorkforceRules.h"
#include "PuffersClub.h"

// ---------------------------------------------------------------------------------------------------------- Lookups

const FPuffersInsuranceProduct* UPuffersCareerSubsystem::FindProduct(FName Id) const
{
	return Config.InsuranceProducts.FindByPredicate([Id](const FPuffersInsuranceProduct& P) { return P.ProductId == Id; });
}

const FPuffersApartmentListing* UPuffersCareerSubsystem::FindApartment(FName UnitId) const
{
	return HousingCatalog.Apartments.FindByPredicate([UnitId](const FPuffersApartmentListing& A) { return A.UnitId == UnitId; });
}

const FPuffersCommercialVacancy* UPuffersCareerSubsystem::FindVacancy(FName UnitId) const
{
	return HousingCatalog.Vacancies.FindByPredicate([UnitId](const FPuffersCommercialVacancy& V) { return V.UnitId == UnitId; });
}

const FPuffersPlotListing* UPuffersCareerSubsystem::FindPlotListing(FName PlotId) const
{
	return HousingCatalog.Plots.FindByPredicate([PlotId](const FPuffersPlotListing& P) { return P.PlotId == PlotId; });
}

FPuffersEntitlement* UPuffersCareerSubsystem::FindEntitlement(FName EntitlementId)
{
	return Save ? Save->Housing.Entitlements.FindByPredicate([EntitlementId](const FPuffersEntitlement& E) { return E.EntitlementId == EntitlementId; }) : nullptr;
}

const FPuffersEntitlement* UPuffersCareerSubsystem::FindEntitlement(FName EntitlementId) const
{
	return Save ? Save->Housing.Entitlements.FindByPredicate([EntitlementId](const FPuffersEntitlement& E) { return E.EntitlementId == EntitlementId; }) : nullptr;
}

// ---------------------------------------------------------------------------------------------------------- Care

bool UPuffersCareerSubsystem::ActivateOnlineCareer()
{
	FString Why;
	if (!Save || !RequireShared(Why))
	{
		return false;
	}
	const bool bNew = FPuffersEstateRules::ActivateOnline(Save->Estate, Now());
	if (bNew)
	{
		Notify(FString::Printf(TEXT("Online career active. Clock in and do a care activity at least every %.0f days to keep your estate safe, or carry self cover."),
			Config.CareWindowHours / 24.f));
		SaveNow();
	}
	return bNew;
}

bool UPuffersCareerSubsystem::HasQualifyingResidence() const
{
	if (!Save)
	{
		return false;
	}
	for (const FPuffersApartmentTenancy& Tenancy : Save->Housing.Tenancies)
	{
		if (Tenancy.State == EPuffersTenancyState::Current)
		{
			return true;
		}
	}
	for (const FPuffersPlotOwnership& Plot : Save->Housing.Plots)
	{
		if (Plot.OwnerId != Save->Owner.CareerOwnerId || Plot.State != EPuffersPlotState::Current)
		{
			continue;
		}
		if (Plot.Buildings.ContainsByPredicate([](const FPuffersPlotBuilding& B) { return B.bCompleted && B.Use == TEXT("Home"); }))
		{
			return true;
		}
	}
	return false;
}

EPuffersCareStatus UPuffersCareerSubsystem::GetCareStatus() const
{
	return Save ? FPuffersEstateRules::GetCareStatus(Save->Estate, Save->Owner.CareerOwnerId, Config.CareWindowHours, HasQualifyingResidence(), Now())
		: EPuffersCareStatus::NotOnline;
}

FDateTime UPuffersCareerSubsystem::GetCareDeadline() const
{
	return Save ? FPuffersEstateRules::CareDeadline(Save->Estate, Config.CareWindowHours) : FDateTime();
}

void UPuffersCareerSubsystem::ClockIn()
{
	if (Save && Save->Estate.bOnlineCareerActive)
	{
		FPuffersEstateRules::ClockIn(Save->Estate, Now());
	}
}

bool UPuffersCareerSubsystem::CompleteCareActivity(FName OpId, FName ActivityId, FString& OutWhy)
{
	if (!Save)
	{
		return false;
	}
	if (Config.CareActivities.Num() > 0 && !Config.CareActivities.ContainsByPredicate([ActivityId](const FPuffersCareActivity& A) { return A.Id == ActivityId; }))
	{
		OutWhy = TEXT("That doesn't count as care.");
		return false;
	}
	if (!FPuffersEstateRules::RecordCare(Save->Estate, OpId, Now(), OutWhy))
	{
		return false;
	}
	// Care before a raid commits closes it and returns the estate to protected status.
	for (FPuffersRaid& Raid : Save->Estate.Raids)
	{
		if (Raid.State == EPuffersRaidState::Open)
		{
			Raid.State = EPuffersRaidState::ClosedByCare;
			Raid.EndedAt = Now();
			Notify(TEXT("You checked in before the raid finished. Your estate is protected again."));
		}
	}
	Notify(FString::Printf(TEXT("Care recorded. Next deadline %s UTC."), *GetCareDeadline().ToString(TEXT("%Y-%m-%d %H:%M"))));
	SaveNow();
	return true;
}

FName UPuffersCareerSubsystem::BuyInsurance(FName ProductId, FName InsuredId, FName BeneficiaryId, bool bInsuredConsents, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return NAME_None;
	}
	const FPuffersInsuranceProduct* Product = FindProduct(ProductId);
	if (!Product)
	{
		OutWhy = TEXT("Unknown policy.");
		return NAME_None;
	}
	const FName Owner = Save->Owner.CareerOwnerId;
	if (InsuredId.IsNone())
	{
		InsuredId = Owner;
	}
	if (!FPuffersEstateRules::CanBuy(Save->Estate, *Product, GetCareStatus() == EPuffersCareStatus::NotOnline ? EPuffersCareStatus::Current : GetCareStatus(),
		InsuredId, Owner, bInsuredConsents, OutWhy))
	{
		return NAME_None;
	}
	const FName PolicyId = MakeId(TEXT("POL"));
	const FName TxId = FName(*FString::Printf(TEXT("PREM_%s"), *PolicyId.ToString()));
	if (PostWorld(TxId, FPuffersLedger::Pocket, -Product->Premium, TEXT("World.Insurer"), FString::Printf(TEXT("%s premium"), *Product->Name)) != EPuffersLedgerResult::Ok)
	{
		OutWhy = FString::Printf(TEXT("The premium is %d Puff Cash."), Product->Premium);
		return NAME_None;
	}
	FPuffersInsurancePolicy Policy = FPuffersEstateRules::MakePolicy(*Product, PolicyId, Owner, InsuredId, BeneficiaryId.IsNone() ? Owner : BeneficiaryId, Now());
	Policy.PremiumTx = TxId;
	Save->Estate.Policies.Add(Policy);
	Notify(FString::Printf(TEXT("%s from %s: covers %s until %s UTC.%s"), *Product->Name, *Product->Insurer, *InsuredId.ToString(),
		*Policy.EndsAt.ToString(TEXT("%Y-%m-%d")), Product->Benefit > 0 ? *FString::Printf(TEXT(" Pays %d on a confirmed permanent death."), Product->Benefit) : TEXT(" Protects your estate from inactivity raids.")));
	SaveNow();
	return PolicyId;
}

bool UPuffersCareerSubsystem::FileInsuranceClaim(FName PolicyId, const FPuffersLossEvent& Loss, FString& OutWhy)
{
	if (!Save)
	{
		return false;
	}
	FPuffersInsurancePolicy* Policy = Save->Estate.Policies.FindByPredicate([PolicyId](const FPuffersInsurancePolicy& P) { return P.PolicyId == PolicyId; });
	if (!Policy)
	{
		OutWhy = TEXT("No such policy.");
		return false;
	}
	if (!FPuffersEstateRules::ValidateClaim(Save->Estate, *Policy, Loss, OutWhy))
	{
		return false;
	}
	const FName TxId = FName(*FString::Printf(TEXT("CLAIM_%s"), *PolicyId.ToString()));
	const FName Beneficiary = Policy->BeneficiaryId;
	const int32 Benefit = Policy->Benefit;
	EPuffersLedgerResult Result;
	if (Beneficiary == Save->Owner.CareerOwnerId)
	{
		Result = PostWorld(TxId, FPuffersLedger::Pocket, Benefit, TEXT("World.Insurer"), TEXT("Life insurance benefit"));
	}
	else
	{
		const FDateTime At = Now();
		Result = Save->Ledger.Transfer(TxId, TEXT("World.Insurer"), FName(*FString::Printf(TEXT("Player.%s"), *Beneficiary.ToString())), Benefit, TEXT("Life insurance benefit"), At);
	}
	if (Result != EPuffersLedgerResult::Ok && Result != EPuffersLedgerResult::Duplicate)
	{
		OutWhy = TEXT("The payout couldn't post.");
		return false;
	}
	Policy = Save->Estate.Policies.FindByPredicate([PolicyId](const FPuffersInsurancePolicy& P) { return P.PolicyId == PolicyId; });
	Policy->bClaimPaid = true;
	Policy->ClaimTx = TxId;
	Save->Estate.SettledLossEvents.Add(Loss.EventId);
	Notify(FString::Printf(TEXT("Claim paid: %d Puff Cash to %s."), Benefit, *Beneficiary.ToString()));
	SaveNow();
	return true;
}

// ---------------------------------------------------------------------------------------------------------- Raids

TArray<FPuffersEstateLine> UPuffersCareerSubsystem::BuildEstateManifest() const
{
	TArray<FPuffersEstateLine> Lines;
	if (!Save)
	{
		return Lines;
	}
	auto AddLine = [&Lines](EPuffersEstateAssetKind Kind, FName AssetId, int64 Quantity, EPuffersEstateExclusion Exclusion, FName Loan = NAME_None)
	{
		FPuffersEstateLine& Line = Lines.AddDefaulted_GetRef();
		Line.Kind = Kind;
		Line.AssetId = AssetId;
		Line.Quantity = Quantity;
		Line.Exclusion = Exclusion;
		Line.LienLoanId = Loan;
	};
	auto LienOn = [this](FName AssetId) -> FName
	{
		const FName* Loan = Save->Workforce.Liens.Find(AssetId);
		return Loan ? *Loan : NAME_None;
	};

	// Spendable Puff Cash in every owned account (held funds stay with their holds).
	TSet<FName> Accounts(Save->Ledger.GetOwnedAccounts());
	Accounts.Add(FPuffersLedger::Pocket);
	for (const FName& Account : Accounts)
	{
		const int64 Available = GetAvailable(Account);
		if (Available > 0)
		{
			AddLine(EPuffersEstateAssetKind::Cash, Account, Available, EPuffersEstateExclusion::None);
		}
	}

	// Carried and stored inventory.
	if (GetInventory)
	{
		for (const TPair<FName, int32>& Item : GetInventory())
		{
			if (Item.Value > 0)
			{
				const FName Loan = LienOn(Item.Key);
				AddLine(EPuffersEstateAssetKind::Item, Item.Key, Item.Value, Loan.IsNone() ? EPuffersEstateExclusion::None : EPuffersEstateExclusion::Liened, Loan);
			}
		}
	}

	// Apartment contents: loot from a current unit; quarantined in an overdue one. The tenancy itself never transfers.
	for (const FPuffersApartmentTenancy& Tenancy : Save->Housing.Tenancies)
	{
		if (Tenancy.State != EPuffersTenancyState::Current && Tenancy.State != EPuffersTenancyState::LockedOverdue)
		{
			continue;
		}
		AddLine(EPuffersEstateAssetKind::Property, Tenancy.TenancyId, 1, EPuffersEstateExclusion::PaidTenancy);
		for (const FPuffersManifestItem& Item : Tenancy.Manifest)
		{
			const EPuffersEstateExclusion Exclusion = !Item.bReclaimable ? EPuffersEstateExclusion::NonTransferable
				: FPuffersHousingRules::IsQuarantined(Tenancy) ? EPuffersEstateExclusion::RentReclaimQuarantine : EPuffersEstateExclusion::None;
			AddLine(EPuffersEstateAssetKind::Item, Item.ItemInstanceId, Item.Quantity, Exclusion);
		}
	}

	// Vehicles (garage integration).
	if (GetOwnedVehicles)
	{
		for (const FName& Vehicle : GetOwnedVehicles())
		{
			const FName Loan = LienOn(Vehicle);
			AddLine(EPuffersEstateAssetKind::Vehicle, Vehicle, 1, Loan.IsNone() ? EPuffersEstateExclusion::None : EPuffersEstateExclusion::Liened, Loan);
		}
	}

	// Owned plots and homes (Jayde, Nov 26: homes are part of the estate).
	for (const FPuffersPlotOwnership& Plot : Save->Housing.Plots)
	{
		if (Plot.OwnerId != Save->Owner.CareerOwnerId || Plot.State == EPuffersPlotState::Sold)
		{
			continue;
		}
		const bool bFrozen = Plot.State == EPuffersPlotState::ListedAsBuilt || Plot.State == EPuffersPlotState::SuspendedNoBuilding;
		const FName Loan = LienOn(Plot.PlotId);
		AddLine(EPuffersEstateAssetKind::Property, Plot.PlotId, 1,
			bFrozen ? EPuffersEstateExclusion::PlotFrozenForNonpayment : (Loan.IsNone() ? EPuffersEstateExclusion::None : EPuffersEstateExclusion::Liened), Loan);
	}

	// Business interests.
	for (const FPuffersOperation& Operation : Save->Owner.Operations)
	{
		AddLine(EPuffersEstateAssetKind::BusinessInterest, Operation.OperationId, 1, EPuffersEstateExclusion::None);
	}
	return Lines;
}

bool UPuffersCareerSubsystem::StartRaid(FName RaidId, FName RaiderId, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return false;
	}
	if (FPuffersEstateRules::FindRaid(Save->Estate, RaidId))
	{
		OutWhy = TEXT("That raid already exists.");
		return false;
	}
	if (GetCareStatus() != EPuffersCareStatus::OverdueExposed)
	{
		OutWhy = TEXT("This home isn't raidable.");
		return false;
	}
	if (FPuffersEstateRules::HasOpenRaid(Save->Estate))
	{
		OutWhy = TEXT("Someone is already raiding this estate.");
		return false;
	}
	FPuffersRaid& Raid = Save->Estate.Raids.AddDefaulted_GetRef();
	Raid.RaidId = RaidId;
	Raid.RaiderId = RaiderId;
	Raid.StartedAt = Now();
	SaveNow();
	return true;
}

bool UPuffersCareerSubsystem::EndRaid(FName RaidId, EPuffersRaidState Outcome)
{
	FPuffersRaid* Raid = Save ? FPuffersEstateRules::FindRaid(Save->Estate, RaidId) : nullptr;
	if (!Raid || Raid->State != EPuffersRaidState::Open || (Outcome != EPuffersRaidState::Failed && Outcome != EPuffersRaidState::Abandoned))
	{
		return false;
	}
	Raid->State = Outcome;
	Raid->EndedAt = Now();
	SaveNow();
	return true;
}

bool UPuffersCareerSubsystem::SettleRaidSuccess(FName RaidId, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return false;
	}
	FPuffersRaid* Raid = FPuffersEstateRules::FindRaid(Save->Estate, RaidId);
	if (!Raid)
	{
		OutWhy = TEXT("No such raid.");
		return false;
	}
	if (Raid->State == EPuffersRaidState::Succeeded)
	{
		OutWhy = TEXT("Already settled.");
		return false;   // repeated success callbacks never pay twice
	}
	if (Raid->State != EPuffersRaidState::Open)
	{
		OutWhy = TEXT("That raid is over.");
		return false;
	}
	// Recheck care, coverage and residence before committing.
	if (GetCareStatus() != EPuffersCareStatus::OverdueExposed)
	{
		Raid->State = EPuffersRaidState::ClosedByCare;
		Raid->EndedAt = Now();
		OutWhy = TEXT("The owner is protected again. Raid closed.");
		SaveNow();
		return false;
	}

	const FName Raider = Raid->RaiderId;
	const FName RaiderAccount = FName(*FString::Printf(TEXT("Player.%s"), *Raider.ToString()));
	FPuffersEstateTransfer Transfer;
	Transfer.TransferId = FName(*FString::Printf(TEXT("XFER_%s"), *RaidId.ToString()));
	Transfer.RaidId = RaidId;
	Transfer.FromOwner = Save->Owner.CareerOwnerId;
	Transfer.ToOwner = Raider;
	Transfer.At = Now();
	Transfer.Lines = BuildEstateManifest();
	FPuffersEstateRules::ClassifyForRaid(Transfer.Lines, Config.RaidCarrySlots);

	for (const FPuffersEstateLine& Line : Transfer.Lines)
	{
		if (!Line.Transfers())
		{
			continue;
		}
		switch (Line.Kind)
		{
		case EPuffersEstateAssetKind::Cash:
		{
			const FName TxId = FName(*FString::Printf(TEXT("%s_%s"), *Transfer.TransferId.ToString(), *Line.AssetId.ToString()));
			const FDateTime At = Now();
			const FName Account = Line.AssetId;
			const int64 Amount = Line.Quantity;
			PostLedger([&](FPuffersLedger& L) { return L.Transfer(TxId, Account, RaiderAccount, Amount, TEXT("Raid settlement"), At); });
			break;
		}
		case EPuffersEstateAssetKind::Item:
		{
			bool bFromApartment = false;
			for (FPuffersApartmentTenancy& Tenancy : Save->Housing.Tenancies)
			{
				const int32 Removed = Tenancy.Manifest.RemoveAll([&Line](const FPuffersManifestItem& Item) { return Item.ItemInstanceId == Line.AssetId; });
				bFromApartment |= Removed > 0;
			}
			if (!bFromApartment && RemoveInventory)
			{
				RemoveInventory(Line.AssetId, static_cast<int32>(Line.Quantity));
			}
			break;
		}
		case EPuffersEstateAssetKind::Vehicle:
			if (TransferVehicle)
			{
				TransferVehicle(Line.AssetId, Raider);
			}
			break;
		case EPuffersEstateAssetKind::Property:
			for (FPuffersPlotOwnership& Plot : Save->Housing.Plots)
			{
				if (Plot.PlotId == Line.AssetId)
				{
					// The lien (if any) stays on the asset with its loan record.
					Plot.OwnerId = Raider;
					for (FPuffersPlayerLease& Lease : Plot.Leases)
					{
						if (Lease.bActive)
						{
							Lease.LandlordId = Raider;
						}
					}
				}
			}
			break;
		case EPuffersEstateAssetKind::BusinessInterest:
		{
			const FName OperationId = Line.AssetId;
			for (FPuffersLicense& License : Save->Owner.Licenses)
			{
				if (License.OperationId == OperationId && License.State == EPuffersLicenseState::Active)
				{
					// The recipient must pass their own licensing gates.
					License.State = EPuffersLicenseState::Closed;
					License.EndedAt = Now();
				}
			}
			Save->Owner.Operations.RemoveAll([OperationId](const FPuffersOperation& O) { return O.OperationId == OperationId; });
			Save->Workforce.Shifts.RemoveAll([OperationId](const FPuffersShift& S) { return S.BusinessId == OperationId && S.State != EPuffersShiftState::Completed; });
			break;
		}
		}
	}

	Raid = FPuffersEstateRules::FindRaid(Save->Estate, RaidId);
	Raid->State = EPuffersRaidState::Succeeded;
	Raid->EndedAt = Now();
	Raid->TransferId = Transfer.TransferId;
	Save->Estate.Transfers.Add(Transfer);
	Save->Estate.EstateResets += 1;
	Save->Estate.LastCareAt = Now();   // a fresh window after reentry

	Publish(EventRaidSettled, RaidId);
	Notify(FString::Printf(TEXT("Your estate was raided while you were away: %lld Puff Cash and %d asset line(s) went to another player. "
		"Your record, route and license status stay with you. Take a %s shift to start over."),
		Transfer.TotalCashMoved(), Transfer.Lines.FilterByPredicate([](const FPuffersEstateLine& L) { return L.Transfers(); }).Num(), *Config.RecoveryJobId.ToString()));
	RecomputeStanding();
	SaveNow();
	return true;
}

// ---------------------------------------------------------------------------------------------------------- Apartments

FName UPuffersCareerSubsystem::SubscribeApartment(FName UnitId, FName BillingEventId, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return NAME_None;
	}
	const FPuffersApartmentListing* Listing = FindApartment(UnitId);
	if (!Listing || !Listing->bInCityArea)
	{
		OutWhy = TEXT("Monthly rentals are City Sample apartments only.");
		return NAME_None;
	}
	if (Save->Housing.Tenancies.ContainsByPredicate([UnitId](const FPuffersApartmentTenancy& T)
		{ return T.UnitId == UnitId && (T.State == EPuffersTenancyState::Current || T.State == EPuffersTenancyState::LockedOverdue); }))
	{
		OutWhy = TEXT("You already rent this unit.");
		return NAME_None;
	}
	FPuffersEntitlement& Entitlement = Save->Housing.Entitlements.AddDefaulted_GetRef();
	Entitlement.EntitlementId = MakeId(TEXT("ENT_APT"));
	Entitlement.Kind = EPuffersEntitlementKind::Apartment;
	Entitlement.SubjectId = UnitId;
	Entitlement.PaidThroughAt = Now();
	FPuffersHousingRules::ApplyBillingEvent(Entitlement, BillingEventId, true, false, Config.BillingPeriodDays, Now());

	FPuffersApartmentTenancy& Tenancy = Save->Housing.Tenancies.AddDefaulted_GetRef();
	Tenancy.TenancyId = MakeId(TEXT("TEN"));
	Tenancy.UnitId = UnitId;
	Tenancy.EntitlementId = Entitlement.EntitlementId;
	Tenancy.StartedAt = Now();
	Notify(FString::Printf(TEXT("You moved into %s (%s, billed outside the game). Miss a renewal and it locks; %d days later the contents go back to the marketplace."),
		*Listing->Name, *Listing->MonthlyFeeLabel, Config.ApartmentGraceDays));
	SaveNow();
	return Tenancy.TenancyId;
}

bool UPuffersCareerSubsystem::ApplyBillingEvent(FName EntitlementId, FName EventId, bool bPaid, bool bPending)
{
	FPuffersEntitlement* Entitlement = FindEntitlement(EntitlementId);
	if (!Entitlement || !FPuffersHousingRules::ApplyBillingEvent(*Entitlement, EventId, bPaid, bPending, Config.BillingPeriodDays, Now()))
	{
		return false;
	}
	AdvanceHousing();
	SaveNow();
	return true;
}

void UPuffersCareerSubsystem::AdvanceHousing()
{
	if (!Save)
	{
		return;
	}
	const FDateTime At = Now();
	for (FPuffersEntitlement& Entitlement : Save->Housing.Entitlements)
	{
		FPuffersHousingRules::RefreshEntitlement(Entitlement, At);
	}
	for (FPuffersApartmentTenancy& Tenancy : Save->Housing.Tenancies)
	{
		const FPuffersEntitlement* Entitlement = FindEntitlement(Tenancy.EntitlementId);
		if (!Entitlement)
		{
			continue;
		}
		const EPuffersTenancyState Before = Tenancy.State;
		FPuffersMarketplaceReturn Return;
		bool bReclaimed = false;
		if (FPuffersHousingRules::AdvanceTenancy(Tenancy, *Entitlement, Config.ApartmentGraceDays, At, Return, bReclaimed))
		{
			if (bReclaimed)
			{
				Save->Housing.MarketplaceReturns.Add(Return);
				Notify(FString::Printf(TEXT("%s was reclaimed: %d item(s) went back to marketplace stock. No payout."), *Tenancy.UnitId.ToString(), Return.Items.Num()));
			}
			else if (Before == EPuffersTenancyState::Current && Tenancy.State == EPuffersTenancyState::LockedOverdue)
			{
				Notify(FString::Printf(TEXT("%s is locked: the renewal was missed. Renew by %s UTC to keep your things."), *Tenancy.UnitId.ToString(),
					*Tenancy.ReclaimAt.ToString(TEXT("%Y-%m-%d"))));
			}
			else if (Tenancy.State == EPuffersTenancyState::Current && Before == EPuffersTenancyState::LockedOverdue)
			{
				Notify(FString::Printf(TEXT("%s is unlocked. Everything is where you left it."), *Tenancy.UnitId.ToString()));
			}
		}
	}
	for (FPuffersPlotOwnership& Plot : Save->Housing.Plots)
	{
		const FPuffersEntitlement* Entitlement = FindEntitlement(Plot.EntitlementId);
		const FPuffersPlotListing* Listing = FindPlotListing(Plot.PlotId);
		if (!Entitlement)
		{
			continue;
		}
		if (FPuffersHousingRules::AdvancePlot(Plot, *Entitlement, Listing ? Listing->Price * 2 : 10000) && Plot.State == EPuffersPlotState::ListedAsBuilt)
		{
			Notify(FString::Printf(TEXT("Plot %s's subscription lapsed. The property is listed for sale as built. Pay the fee before someone buys it to keep it."), *Plot.PlotId.ToString()));
		}
	}
	FPuffersHousingRules::ExpireReservations(Save->Housing.Applications, At);
}

bool UPuffersCareerSubsystem::CanEnterApartment(FName TenancyId) const
{
	const FPuffersApartmentTenancy* Tenancy = Save ? Save->Housing.Tenancies.FindByPredicate([TenancyId](const FPuffersApartmentTenancy& T) { return T.TenancyId == TenancyId; }) : nullptr;
	return Tenancy && FPuffersHousingRules::CanUseContents(*Tenancy);
}

bool UPuffersCareerSubsystem::StoreInApartment(FName TenancyId, FName ItemId, bool bPlace, FString& OutWhy)
{
	FPuffersApartmentTenancy* Tenancy = Save ? Save->Housing.Tenancies.FindByPredicate([TenancyId](const FPuffersApartmentTenancy& T) { return T.TenancyId == TenancyId; }) : nullptr;
	if (!Tenancy || !FPuffersHousingRules::CanUseContents(*Tenancy))
	{
		OutWhy = TEXT("The unit is locked.");
		return false;
	}
	if (RemoveInventory && !RemoveInventory(ItemId, 1))
	{
		OutWhy = TEXT("You don't have that.");
		return false;
	}
	FPuffersManifestItem& Item = Tenancy->Manifest.AddDefaulted_GetRef();
	Item.ItemInstanceId = MakeId(*FString::Printf(TEXT("INST_%s"), *ItemId.ToString()));
	Item.Sku = ItemId;
	Item.OwnerId = Save->Owner.CareerOwnerId;
	Item.bPlaced = bPlace;
	SaveNow();
	return true;
}

bool UPuffersCareerSubsystem::TakeFromApartment(FName TenancyId, FName ItemInstanceId, FString& OutWhy)
{
	FPuffersApartmentTenancy* Tenancy = Save ? Save->Housing.Tenancies.FindByPredicate([TenancyId](const FPuffersApartmentTenancy& T) { return T.TenancyId == TenancyId; }) : nullptr;
	if (!Tenancy || !FPuffersHousingRules::CanUseContents(*Tenancy))
	{
		OutWhy = TEXT("The unit is locked.");
		return false;
	}
	const int32 Index = Tenancy->Manifest.IndexOfByPredicate([ItemInstanceId](const FPuffersManifestItem& I) { return I.ItemInstanceId == ItemInstanceId; });
	if (Index == INDEX_NONE)
	{
		OutWhy = TEXT("That isn't here.");
		return false;
	}
	const FName Sku = Tenancy->Manifest[Index].Sku;
	const int32 Quantity = Tenancy->Manifest[Index].Quantity;
	Tenancy->Manifest.RemoveAt(Index);
	if (AddInventory)
	{
		AddInventory(Sku, Quantity);
	}
	SaveNow();
	return true;
}

// ---------------------------------------------------------------------------------------------------------- Business applications

FName UPuffersCareerSubsystem::ApplyForVacancy(FName UnitId, FName ArchetypeId, const FString& BusinessName, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return NAME_None;
	}
	const FPuffersCommercialVacancy* Vacancy = FindVacancy(UnitId);
	const FPuffersBusinessArchetype* Archetype = FindArchetype(ArchetypeId);
	if (!Vacancy || !Archetype)
	{
		OutWhy = TEXT("Unknown unit or business type.");
		return NAME_None;
	}
	FPuffersBusinessApplication& Application = Save->Housing.Applications.AddDefaulted_GetRef();
	Application.ApplicationId = MakeId(TEXT("APP"));
	Application.ApplicantId = Save->Owner.CareerOwnerId;
	Application.UnitId = UnitId;
	Application.ArchetypeId = ArchetypeId;
	Application.BusinessName = BusinessName.IsEmpty() ? Archetype->Name : BusinessName;
	Application.OperatorId = Save->Owner.CareerOwnerId;
	Application.SubmittedAt = Now();

	FString LicenseWhy;
	const bool bCanLicense = FPuffersCareerRules::CanHoldCannabisLicense(Save->Owner, LicenseWhy);
	FPuffersHousingRules::Review(Application, *Vacancy, GetAvailable(FPuffersLedger::Pocket), Archetype->Kind == EPuffersOperationKind::LicensedCannabis,
		bCanLicense, Save->Housing.Applications, Config.VacancyReservationHours, Now());
	OutWhy = Application.Reason;
	Notify(FString::Printf(TEXT("Application for %s: %s"), *Vacancy->Building, *Application.Reason));
	SaveNow();
	return Application.ApplicationId;
}

FName UPuffersCareerSubsystem::TakePossession(FName ApplicationId, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return NAME_None;
	}
	FPuffersHousingRules::ExpireReservations(Save->Housing.Applications, Now());
	FPuffersBusinessApplication* Application = Save->Housing.Applications.FindByPredicate([ApplicationId](const FPuffersBusinessApplication& A) { return A.ApplicationId == ApplicationId; });
	if (!Application)
	{
		OutWhy = TEXT("No such application.");
		return NAME_None;
	}
	if (Application->State == EPuffersApplicationState::Leased)
	{
		return Application->OperationId;
	}
	if (Application->State != EPuffersApplicationState::Approved)
	{
		OutWhy = Application->Reason.IsEmpty() ? TEXT("Not approved.") : Application->Reason;
		return NAME_None;
	}
	const FPuffersCommercialVacancy* Vacancy = FindVacancy(Application->UnitId);
	if (!Vacancy)
	{
		return NAME_None;
	}
	const FName TxId = FName(*FString::Printf(TEXT("UPFRONT_%s"), *ApplicationId.ToString()));
	if (PostWorld(TxId, FPuffersLedger::Pocket, -Vacancy->Upfront, TEXT("World.Landlord"), FString::Printf(TEXT("Lease upfront, %s"), *Vacancy->Building)) != EPuffersLedgerResult::Ok)
	{
		Application = Save->Housing.Applications.FindByPredicate([ApplicationId](const FPuffersBusinessApplication& A) { return A.ApplicationId == ApplicationId; });
		Application->State = EPuffersApplicationState::Withdrawn;
		Application->Reason = TEXT("Couldn't afford the upfront payment. The reservation was released.");
		OutWhy = Application->Reason;
		return NAME_None;
	}
	Application = Save->Housing.Applications.FindByPredicate([ApplicationId](const FPuffersBusinessApplication& A) { return A.ApplicationId == ApplicationId; });
	Application->UpfrontTx = TxId;

	// Entry cost is covered by the lease; the operation starts in fit-out.
	FPuffersBusinessArchetype* Archetype = Config.Archetypes.FindByPredicate([Application](const FPuffersBusinessArchetype& A) { return A.Id == Application->ArchetypeId; });
	const int32 SavedCost = Archetype ? Archetype->EntryCost : 0;
	if (Archetype)
	{
		Archetype->EntryCost = 0;
	}
	const FName OperationId = StartOperation(FName(*FString::Printf(TEXT("OP_%s"), *Application->UnitId.ToString())), Application->ArchetypeId,
		Vacancy->CountyId, Application->UnitId, Application->BusinessName, OutWhy);
	if (Archetype)
	{
		Archetype->EntryCost = SavedCost;
	}
	Application = Save->Housing.Applications.FindByPredicate([ApplicationId](const FPuffersBusinessApplication& A) { return A.ApplicationId == ApplicationId; });
	Application->State = EPuffersApplicationState::Leased;
	Application->OperationId = OperationId;
	SaveNow();
	return OperationId;
}

// ---------------------------------------------------------------------------------------------------------- Plots

bool UPuffersCareerSubsystem::BuyPlot(FName PlotId, FName BillingEventId, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return false;
	}
	const FPuffersPlotListing* Listing = FindPlotListing(PlotId);
	if (!Listing)
	{
		OutWhy = TEXT("Unknown plot.");
		return false;
	}
	if (Save->Housing.Plots.ContainsByPredicate([PlotId](const FPuffersPlotOwnership& P) { return P.PlotId == PlotId && P.State != EPuffersPlotState::Sold; }))
	{
		OutWhy = TEXT("That plot is taken.");
		return false;
	}
	const FName TxId = FName(*FString::Printf(TEXT("PLOT_%s"), *PlotId.ToString()));
	if (PostWorld(TxId, FPuffersLedger::Pocket, -Listing->Price, TEXT("World.LandOffice"), FString::Printf(TEXT("Plot %s"), *PlotId.ToString())) != EPuffersLedgerResult::Ok)
	{
		OutWhy = FString::Printf(TEXT("The plot costs %d Puff Cash."), Listing->Price);
		return false;
	}
	FPuffersEntitlement& Entitlement = Save->Housing.Entitlements.AddDefaulted_GetRef();
	Entitlement.EntitlementId = MakeId(TEXT("ENT_PLOT"));
	Entitlement.Kind = EPuffersEntitlementKind::Plot;
	Entitlement.SubjectId = PlotId;
	Entitlement.PaidThroughAt = Now();
	if (!BillingEventId.IsNone())
	{
		FPuffersHousingRules::ApplyBillingEvent(Entitlement, BillingEventId, true, false, Config.BillingPeriodDays, Now());
	}
	FPuffersHousingRules::RefreshEntitlement(Entitlement, Now());

	FPuffersPlotOwnership& Plot = Save->Housing.Plots.AddDefaulted_GetRef();
	Plot.PlotId = PlotId;
	Plot.OwnerId = Save->Owner.CareerOwnerId;
	Plot.EntitlementId = Entitlement.EntitlementId;
	Plot.PurchaseTx = TxId;
	Plot.State = Entitlement.State == EPuffersEntitlementState::Current ? EPuffersPlotState::Current : EPuffersPlotState::SuspendedNoBuilding;
	Notify(FString::Printf(TEXT("You own plot %s. Building and operating on it needs the plot subscription (%s)."), *PlotId.ToString(), *Listing->MonthlyFeeLabel));
	SaveNow();
	return true;
}

bool UPuffersCareerSubsystem::AddPlotBuilding(FName PlotId, FName BuildingId, FName Use, bool bCompleted, FString& OutWhy)
{
	FPuffersPlotOwnership* Plot = Save ? Save->Housing.Plots.FindByPredicate([PlotId](const FPuffersPlotOwnership& P) { return P.PlotId == PlotId; }) : nullptr;
	const FPuffersEntitlement* Entitlement = Plot ? FindEntitlement(Plot->EntitlementId) : nullptr;
	const FPuffersPlotListing* Listing = FindPlotListing(PlotId);
	if (!Plot || !Entitlement || Plot->OwnerId != Save->Owner.CareerOwnerId)
	{
		OutWhy = TEXT("You don't own that plot.");
		return false;
	}
	if (!FPuffersHousingRules::CanDevelop(*Plot, *Entitlement))
	{
		OutWhy = TEXT("Building needs a current plot subscription.");
		return false;
	}
	if (Listing && Listing->AllowedUses.Num() > 0 && !Listing->AllowedUses.Contains(Use))
	{
		OutWhy = FString::Printf(TEXT("%s isn't allowed on this plot."), *Use.ToString());
		return false;
	}
	FPuffersPlotBuilding* Existing = Plot->Buildings.FindByPredicate([BuildingId](const FPuffersPlotBuilding& B) { return B.BuildingId == BuildingId; });
	if (Existing)
	{
		Existing->bCompleted = bCompleted;
		Existing->Use = Use;
		Existing->BuildVersion += 1;
		SaveNow();
		Publish(TEXT("PlotBuildingChanged"), PlotId);
		return true;
	}
	if (Listing && Plot->Buildings.Num() >= Listing->MaxBuildings)
	{
		OutWhy = TEXT("This plot is at its building limit.");
		return false;
	}
	FPuffersPlotBuilding& Building = Plot->Buildings.AddDefaulted_GetRef();
	Building.BuildingId = BuildingId;
	Building.Use = Use;
	Building.bCompleted = bCompleted;
	SaveNow();
	Publish(TEXT("PlotBuildingChanged"), PlotId);
	return true;
}

FName UPuffersCareerSubsystem::LeaseToPlayer(FName PlotId, FName BuildingId, FName TenantId, int32 Rent, int32 Deposit, int32 PeriodDays, FString& OutWhy)
{
	FPuffersPlotOwnership* Plot = Save ? Save->Housing.Plots.FindByPredicate([PlotId](const FPuffersPlotOwnership& P) { return P.PlotId == PlotId; }) : nullptr;
	const FPuffersEntitlement* Entitlement = Plot ? FindEntitlement(Plot->EntitlementId) : nullptr;
	if (!Plot || !Entitlement || !FPuffersHousingRules::CanDevelop(*Plot, *Entitlement))
	{
		OutWhy = TEXT("New leases need a current plot subscription.");
		return NAME_None;
	}
	const FPuffersPlotBuilding* Building = Plot->Buildings.FindByPredicate([BuildingId](const FPuffersPlotBuilding& B) { return B.BuildingId == BuildingId; });
	if (!Building || !Building->bCompleted)
	{
		OutWhy = TEXT("Finish the building first.");
		return NAME_None;
	}
	FPuffersPlayerLease& Lease = Plot->Leases.AddDefaulted_GetRef();
	Lease.LeaseId = MakeId(TEXT("LEASE"));
	Lease.BuildingId = BuildingId;
	Lease.TenantId = TenantId;
	Lease.RentPerPeriod = Rent;
	Lease.Deposit = Deposit;
	Lease.PeriodDays = FMath::Max(1, PeriodDays);
	Lease.StartsAt = Now();
	Lease.LandlordId = Plot->OwnerId;
	// The refundable deposit is held in escrow, not paid to the landlord.
	if (Deposit > 0)
	{
		Save->Ledger.Transfer(FName(*FString::Printf(TEXT("DEP_%s"), *Lease.LeaseId.ToString())), FName(*FString::Printf(TEXT("Player.%s"), *TenantId.ToString())),
			FName(*FString::Printf(TEXT("Escrow.%s"), *Lease.LeaseId.ToString())), Deposit, TEXT("Lease deposit"), Now());
	}
	SaveNow();
	return Lease.LeaseId;
}

int32 UPuffersCareerSubsystem::CollectLeaseRent()
{
	if (!Save)
	{
		return 0;
	}
	int32 Posted = 0;
	const FDateTime At = Now();
	for (FPuffersPlotOwnership& Plot : Save->Housing.Plots)
	{
		for (FPuffersPlayerLease& Lease : Plot.Leases)
		{
			if (!Lease.bActive || Lease.LandlordId != Save->Owner.CareerOwnerId)
			{
				continue;
			}
			const int32 Period = FPuffersHousingRules::PeriodIndex(Lease, At);
			for (int32 P = 0; P <= Period; ++P)
			{
				if (Lease.PostedPeriods.Contains(P))
				{
					continue;
				}
				const FName TxId = FName(*FString::Printf(TEXT("RENT_%s_%d"), *Lease.LeaseId.ToString(), P));
				const FName TenantAccount = FName(*FString::Printf(TEXT("Player.%s"), *Lease.TenantId.ToString()));
				const int64 Rent = Lease.RentPerPeriod;
				const EPuffersLedgerResult Result = PostLedger([&](FPuffersLedger& L) { return L.Transfer(TxId, TenantAccount, FPuffersLedger::Pocket, Rent, TEXT("Tenant rent"), At); });
				if (Result == EPuffersLedgerResult::Ok || Result == EPuffersLedgerResult::Duplicate)
				{
					FPuffersHousingRules::MarkRentPosted(Lease, P);
					++Posted;
				}
			}
		}
	}
	return Posted;
}

bool UPuffersCareerSubsystem::CurePlot(FName PlotId, FName BillingEventId)
{
	FPuffersPlotOwnership* Plot = Save ? Save->Housing.Plots.FindByPredicate([PlotId](const FPuffersPlotOwnership& P) { return P.PlotId == PlotId; }) : nullptr;
	FPuffersEntitlement* Entitlement = Plot ? FindEntitlement(Plot->EntitlementId) : nullptr;
	if (!Plot || !Entitlement || Plot->State == EPuffersPlotState::Sold)
	{
		return false;
	}
	FPuffersHousingRules::ApplyBillingEvent(*Entitlement, BillingEventId, true, false, Config.BillingPeriodDays, Now());
	const bool bCured = FPuffersHousingRules::Cure(*Plot, *Entitlement);
	if (bCured)
	{
		Notify(FString::Printf(TEXT("Plot %s is current again and off the market."), *PlotId.ToString()));
	}
	SaveNow();
	return bCured;
}

bool UPuffersCareerSubsystem::BuyPlotAsBuilt(FName PlotId, FName BuyerId, const TArray<int32>& ExpectedVersions, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return false;
	}
	FPuffersPlotOwnership* Plot = Save->Housing.Plots.FindByPredicate([PlotId](const FPuffersPlotOwnership& P) { return P.PlotId == PlotId; });
	if (!Plot || !FPuffersHousingRules::CanBuyAsBuilt(*Plot, BuyerId, ExpectedVersions, OutWhy))
	{
		return false;
	}
	const FName SaleTx = FName(*FString::Printf(TEXT("ASBUILT_%s_%d"), *PlotId.ToString(), Plot->ForcedSales));
	const int64 Price = Plot->ListedPrice;
	const FName BuyerAccount = FName(*FString::Printf(TEXT("Player.%s"), *BuyerId.ToString()));
	const FName SellerId = Plot->OwnerId;
	const FDateTime At = Now();

	// A lien is settled against the proceeds first; the owner gets what's left.
	int64 ToLender = 0;
	FName LoanId;
	if (const FName* Lien = Save->Workforce.Liens.Find(PlotId))
	{
		LoanId = *Lien;
		if (FPuffersLoan* Loan = Save->Workforce.Loans.FindByPredicate([&LoanId](const FPuffersLoan& L) { return L.LoanId == LoanId; }))
		{
			ToLender = FMath::Min(Price, Loan->Outstanding());
		}
	}
	TArray<FPuffersLedgerLeg> Legs;
	Legs.Add(FPuffersLedgerLeg::Make(BuyerAccount, -Price, TEXT("Purchase")));
	if (ToLender > 0)
	{
		Legs.Add(FPuffersLedgerLeg::Make(FName(*FString::Printf(TEXT("World.Lender.%s"), *LoanId.ToString())), ToLender, TEXT("Lien payoff")));
	}
	if (Price - ToLender > 0)
	{
		const FName SellerAccount = SellerId == Save->Owner.CareerOwnerId ? FPuffersLedger::Pocket : FName(*FString::Printf(TEXT("Player.%s"), *SellerId.ToString()));
		Legs.Add(FPuffersLedgerLeg::Make(SellerAccount, Price - ToLender, TEXT("Net")));
	}
	const EPuffersLedgerResult Result = PostLedger([&](FPuffersLedger& L) { return L.Post(SaleTx, Legs, FString::Printf(TEXT("As-built sale of %s"), *PlotId.ToString()), At); });
	if (Result != EPuffersLedgerResult::Ok)
	{
		OutWhy = Result == EPuffersLedgerResult::Duplicate ? TEXT("Already sold.") : TEXT("The purchase didn't go through.");
		return false;
	}
	if (ToLender > 0)
	{
		if (FPuffersLoan* Loan = Save->Workforce.Loans.FindByPredicate([&LoanId](const FPuffersLoan& L) { return L.LoanId == LoanId; }))
		{
			FPuffersWorkforceRules::ApplyPayment(*Loan, Save->Workforce.Liens, ToLender, SaleTx);
		}
	}

	Plot = Save->Housing.Plots.FindByPredicate([PlotId](const FPuffersPlotOwnership& P) { return P.PlotId == PlotId; });
	TArray<FName> Movable;
	FPuffersHousingRules::SettleAsBuilt(*Plot, BuyerId, SaleTx, Movable);
	if (AddInventory && SellerId == Save->Owner.CareerOwnerId)
	{
		for (const FName& Item : Movable)
		{
			AddInventory(Item, 1);   // personal items stay with their owner
		}
	}
	Notify(FString::Printf(TEXT("Plot %s sold as built for %lld. %lld went to the lender, %lld to the owner. Tenants keep their leases."),
		*PlotId.ToString(), Price, ToLender, Price - ToLender));
	SaveNow();
	return true;
}
