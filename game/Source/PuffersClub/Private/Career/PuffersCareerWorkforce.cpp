// Secured loans (quotes, liens, repayment, default and repossession) and the workforce (hiring floor, shifts,
// weekly payroll, unpaid wages and what workers do about them).

#include "Career/PuffersCareerSubsystem.h"
#include "Career/PuffersCareerRules.h"
#include "Career/PuffersWorkforceRules.h"
#include "PuffersClub.h"

namespace
{
	FName LenderAccount(FName LoanId)
	{
		return FName(*FString::Printf(TEXT("World.Lender.%s"), *LoanId.ToString()));
	}

	FName WorkerAccount(const FPuffersEmployment& E)
	{
		return FName(*FString::Printf(TEXT("%s.%s"), E.bIsAI ? TEXT("World.NPC") : TEXT("Player"), *E.WorkerId.ToString()));
	}
}

// ---------------------------------------------------------------------------------------------------------- Loans

const FPuffersLenderDefinition* UPuffersCareerSubsystem::FindLender(FName Id) const
{
	return Config.Lenders.FindByPredicate([Id](const FPuffersLenderDefinition& L) { return L.LenderId == Id; });
}

FName UPuffersCareerSubsystem::QuoteLoan(FName LenderId, const FPuffersCollateral& Collateral, int64 Principal, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return NAME_None;
	}
	const FPuffersLenderDefinition* Lender = FindLender(LenderId);
	if (!Lender)
	{
		OutWhy = TEXT("Unknown lender.");
		return NAME_None;
	}
	FPuffersCollateral Checked = Collateral;
	// Home collateral is the plot it stands on: the title must be ours and current.
	if (Checked.Kind == EPuffersCollateralKind::Home)
	{
		const FPuffersPlotOwnership* Plot = Save->Housing.Plots.FindByPredicate([&Checked](const FPuffersPlotOwnership& P) { return P.PlotId == Checked.AssetId; });
		if (!Plot || Plot->OwnerId != Save->Owner.CareerOwnerId)
		{
			Checked.bRented = !Plot;
			if (Plot)
			{
				OutWhy = TEXT("You don't own that home.");
				return NAME_None;
			}
		}
		else if (Plot->State != EPuffersPlotState::Current)
		{
			Checked.bPlotRightsTransferable = false;
		}
	}
	else if (Checked.Kind == EPuffersCollateralKind::Jewelry && GetInventory)
	{
		if (GetInventory().FindRef(Checked.AssetId) <= 0)
		{
			OutWhy = TEXT("You don't have that on you.");
			return NAME_None;
		}
	}
	else if (Checked.Kind == EPuffersCollateralKind::Vehicle && GetOwnedVehicles)
	{
		if (!GetOwnedVehicles().Contains(Checked.AssetId))
		{
			OutWhy = TEXT("That vehicle isn't titled to you.");
			return NAME_None;
		}
	}

	FPuffersLoan Loan;
	const FName LoanId = MakeId(TEXT("LOAN"));
	if (!FPuffersWorkforceRules::QuoteLoan(*Lender, Checked, Save->Workforce.Liens, LoanId, Principal, Now(), Loan, OutWhy))
	{
		return NAME_None;
	}
	Save->Workforce.Loans.Add(Loan);
	Notify(Loan.Disclosure);
	return LoanId;
}

bool UPuffersCareerSubsystem::AcceptLoan(FName LoanId, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return false;
	}
	FPuffersLoan* Loan = Save->Workforce.Loans.FindByPredicate([LoanId](const FPuffersLoan& L) { return L.LoanId == LoanId; });
	if (!Loan)
	{
		OutWhy = TEXT("No such offer.");
		return false;
	}
	if (Loan->State == EPuffersLoanState::Funded)
	{
		return true;
	}
	const FName TxId = FName(*FString::Printf(TEXT("LOAN_%s"), *LoanId.ToString()));
	// Fund first (lien check), then disburse; undo the lien if the posting fails.
	if (!FPuffersWorkforceRules::Fund(*Loan, Save->Workforce.Liens, TxId, OutWhy))
	{
		return false;
	}
	const int64 Principal = Loan->Principal;
	const FName AssetId = Loan->Collateral.AssetId;
	if (PostWorld(TxId, FPuffersLedger::Pocket, Principal, LenderAccount(LoanId), TEXT("Loan disbursement")) != EPuffersLedgerResult::Ok)
	{
		Loan->State = EPuffersLoanState::Offered;
		Loan->DisbursementTx = NAME_None;
		Save->Workforce.Liens.Remove(AssetId);
		OutWhy = TEXT("The lender couldn't pay out.");
		return false;
	}
	Notify(FString::Printf(TEXT("Loan funded: %lld Puff Cash. %s is pledged until it's paid off."), Principal, *AssetId.ToString()));
	SaveNow();
	return true;
}

int64 UPuffersCareerSubsystem::RepayLoan(FName LoanId, FName TxId, int64 Amount)
{
	if (!Save || Amount <= 0)
	{
		return 0;
	}
	FPuffersLoan* Loan = Save->Workforce.Loans.FindByPredicate([LoanId](const FPuffersLoan& L) { return L.LoanId == LoanId; });
	if (!Loan || Loan->PaymentTxs.Contains(TxId))
	{
		return 0;
	}
	if (Loan->State != EPuffersLoanState::Funded && Loan->State != EPuffersLoanState::Defaulted)
	{
		return 0;
	}
	const int64 Pay = FMath::Min(Amount, Loan->Outstanding());
	if (Pay <= 0 || PostWorld(TxId, FPuffersLedger::Pocket, -Pay, LenderAccount(LoanId), TEXT("Loan payment")) != EPuffersLedgerResult::Ok)
	{
		return 0;
	}
	// The ledger posting may have triggered callbacks; look the loan up again.
	Loan = Save->Workforce.Loans.FindByPredicate([LoanId](const FPuffersLoan& L) { return L.LoanId == LoanId; });
	const int64 Applied = FPuffersWorkforceRules::ApplyPayment(*Loan, Save->Workforce.Liens, Pay, TxId);
	if (Loan->State == EPuffersLoanState::Repaid)
	{
		Notify(FString::Printf(TEXT("Loan paid off. %s is yours free and clear."), *Loan->Collateral.AssetId.ToString()));
	}
	SaveNow();
	return Applied;
}

bool UPuffersCareerSubsystem::IsAssetLiened(FName AssetId) const
{
	return Save && Save->Workforce.Liens.Contains(AssetId);
}

void UPuffersCareerSubsystem::AdvanceLoans()
{
	if (!Save)
	{
		return;
	}
	const FDateTime At = Now();
	bool bChanged = false;
	for (FPuffersLoan& Loan : Save->Workforce.Loans)
	{
		if (Loan.State != EPuffersLoanState::Funded && Loan.State != EPuffersLoanState::Defaulted)
		{
			continue;
		}
		const FPuffersLenderDefinition* Lender = FindLender(Loan.LenderId);
		if (!Lender || !FPuffersWorkforceRules::IsInDefault(Loan, *Lender, At))
		{
			continue;
		}
		Loan.State = EPuffersLoanState::Defaulted;

		// Repossession: the collateral goes to the lender; the remaining balance is settled by it.
		const FName AssetId = Loan.Collateral.AssetId;
		const FName LenderOwner = FName(*FString::Printf(TEXT("Lender.%s"), *Loan.LenderId.ToString()));
		bool bTaken = false;
		switch (Loan.Collateral.Kind)
		{
		case EPuffersCollateralKind::Vehicle:
			bTaken = TransferVehicle ? TransferVehicle(AssetId, LenderOwner) : true;
			break;
		case EPuffersCollateralKind::Jewelry:
			bTaken = RemoveInventory ? RemoveInventory(AssetId, 1) : true;
			break;
		case EPuffersCollateralKind::Home:
			if (FPuffersPlotOwnership* Plot = Save->Housing.Plots.FindByPredicate([AssetId](const FPuffersPlotOwnership& P) { return P.PlotId == AssetId; }))
			{
				// Tenants keep their leases; rent now goes to the lender.
				for (FPuffersPlayerLease& Lease : Plot->Leases)
				{
					Lease.LandlordId = LenderOwner;
				}
				Plot->OwnerId = LenderOwner;
				Plot->State = EPuffersPlotState::Sold;
				Plot->SoldTo = LenderOwner;
				// The plot subscription ends with the title (billing is told through the entitlement removal).
				const FName EntitlementId = Plot->EntitlementId;
				Save->Housing.Entitlements.RemoveAll([EntitlementId](const FPuffersEntitlement& E) { return E.EntitlementId == EntitlementId; });
				Plot->EntitlementId = NAME_None;
			}
			bTaken = true;
			break;
		}
		if (!bTaken)
		{
			continue;
		}
		Loan.State = EPuffersLoanState::Repossessed;
		for (FPuffersInstallment& Installment : Loan.Schedule)
		{
			Installment.Paid = Installment.Amount;
		}
		Save->Workforce.Liens.Remove(AssetId);
		Notify(FString::Printf(TEXT("You missed a loan payment past the grace period. The lender repossessed %s."), *AssetId.ToString()));
		bChanged = true;
	}
	if (bChanged)
	{
		RecomputeStanding();
		SaveNow();
	}
}

// ---------------------------------------------------------------------------------------------------------- Hiring

FName UPuffersCareerSubsystem::OfferJob(FName BusinessId, FName WorkerId, bool bIsAI, const FString& Title, int32 WagePerShift, int32 ShiftsPerWeek, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return NAME_None;
	}
	if (!FPuffersCareerRules::FindOperation(Save->Owner, BusinessId))
	{
		OutWhy = TEXT("You can only hire for a business you run.");
		return NAME_None;
	}
	if (WorkerId.IsNone() || WagePerShift <= 0 || ShiftsPerWeek <= 0)
	{
		OutWhy = TEXT("Pick a worker, a wage and shifts per week.");
		return NAME_None;
	}
	if (Save->Workforce.Employment.ContainsByPredicate([&](const FPuffersEmployment& E)
		{ return E.BusinessId == BusinessId && E.WorkerId == WorkerId && (E.State == EPuffersEmploymentState::Active || E.State == EPuffersEmploymentState::Offered); }))
	{
		OutWhy = TEXT("They already work here or have an offer.");
		return NAME_None;
	}
	FPuffersEmployment Offer;
	Offer.EmploymentId = MakeId(TEXT("EMP"));
	Offer.BusinessId = BusinessId;
	Offer.WorkerId = WorkerId;
	Offer.bIsAI = bIsAI;
	Offer.Title = Title;
	Offer.WagePerShift = WagePerShift;
	Offer.ShiftsPerWeek = ShiftsPerWeek;
	Offer.State = EPuffersEmploymentState::Offered;

	// Same floor as acceptance, so the owner hears about it up front.
	const int64 Available = GetAvailable(FPuffersLedger::ReserveAccount(BusinessId)) + GetAvailable(FPuffersLedger::Pocket);
	if (!FPuffersWorkforceRules::CanHire(Save->Workforce.Employment, BusinessId, Available, Config.HiringMinimum, Offer, OutWhy))
	{
		return NAME_None;
	}
	Save->Workforce.Employment.Add(Offer);
	const FName EmploymentId = Offer.EmploymentId;
	// AI NPCs take a fair offer on the spot.
	if (bIsAI && !AcceptJobOffer(EmploymentId, OutWhy))
	{
		Save->Workforce.Employment.RemoveAll([EmploymentId](const FPuffersEmployment& E) { return E.EmploymentId == EmploymentId; });
		return NAME_None;
	}
	SaveNow();
	return EmploymentId;
}

bool UPuffersCareerSubsystem::AcceptJobOffer(FName EmploymentId, FString& OutWhy)
{
	if (!Save || !RequireShared(OutWhy))
	{
		return false;
	}
	const int32 Index = Save->Workforce.Employment.IndexOfByPredicate([EmploymentId](const FPuffersEmployment& E) { return E.EmploymentId == EmploymentId; });
	if (Index == INDEX_NONE)
	{
		OutWhy = TEXT("That offer is gone.");
		return false;
	}
	if (Save->Workforce.Employment[Index].State == EPuffersEmploymentState::Active)
	{
		return true;
	}
	if (Save->Workforce.Employment[Index].State != EPuffersEmploymentState::Offered)
	{
		OutWhy = TEXT("That offer is closed.");
		return false;
	}
	// Recheck the floor at the moment the employment becomes real.
	const FPuffersEmployment Hire = Save->Workforce.Employment[Index];
	const int64 Available = GetAvailable(FPuffersLedger::ReserveAccount(Hire.BusinessId)) + GetAvailable(FPuffersLedger::Pocket);
	if (!FPuffersWorkforceRules::CanHire(Save->Workforce.Employment, Hire.BusinessId, Available, Config.HiringMinimum, Hire, OutWhy))
	{
		return false;
	}
	FPuffersEmployment& E = Save->Workforce.Employment[Index];
	E.State = EPuffersEmploymentState::Active;
	Notify(FString::Printf(TEXT("%s joined %s as %s for %d per shift."), *E.WorkerId.ToString(), *E.BusinessId.ToString(), *E.Title, E.WagePerShift));
	SaveNow();
	return true;
}

TArray<FPuffersEmployment> UPuffersCareerSubsystem::GetEmployment(FName BusinessId) const
{
	TArray<FPuffersEmployment> Out;
	if (Save)
	{
		for (const FPuffersEmployment& E : Save->Workforce.Employment)
		{
			if (BusinessId.IsNone() || E.BusinessId == BusinessId)
			{
				Out.Add(E);
			}
		}
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------------------- Shifts

FName UPuffersCareerSubsystem::PublishShift(FName BusinessId, FName EmploymentId, FDateTime StartsAt, FDateTime EndsAt, const FString& Role)
{
	FString Why;
	if (!Save || !RequireShared(Why) || EndsAt <= StartsAt)
	{
		return NAME_None;
	}
	const FPuffersEmployment* E = Save->Workforce.Employment.FindByPredicate([EmploymentId](const FPuffersEmployment& X) { return X.EmploymentId == EmploymentId; });
	if (!E || E->BusinessId != BusinessId || E->State != EPuffersEmploymentState::Active)
	{
		return NAME_None;
	}
	FPuffersShift Shift;
	Shift.ShiftId = MakeId(TEXT("SHIFT"));
	Shift.BusinessId = BusinessId;
	Shift.EmploymentId = EmploymentId;
	Shift.StartsAt = StartsAt;
	Shift.EndsAt = EndsAt;
	Shift.Role = Role;
	// AI NPCs accept their schedule; players answer from the phone.
	Shift.State = E->bIsAI ? EPuffersShiftState::Accepted : EPuffersShiftState::Published;
	Save->Workforce.Shifts.Add(Shift);
	if (!E->bIsAI)
	{
		Save->Workforce.Notices.Add(FString::Printf(TEXT("New shift at %s: %s, %s UTC."), *BusinessId.ToString(), *Role, *StartsAt.ToString(TEXT("%a %H:%M"))));
	}
	return Shift.ShiftId;
}

bool UPuffersCareerSubsystem::RespondToShift(FName ShiftId, bool bAccept)
{
	if (!Save)
	{
		return false;
	}
	FPuffersShift* Shift = Save->Workforce.Shifts.FindByPredicate([ShiftId](const FPuffersShift& S) { return S.ShiftId == ShiftId; });
	if (!Shift || (Shift->State != EPuffersShiftState::Published && Shift->State != EPuffersShiftState::ChangeRequested))
	{
		return false;
	}
	Shift->State = bAccept ? EPuffersShiftState::Accepted : EPuffersShiftState::Declined;
	return true;
}

bool UPuffersCareerSubsystem::CheckInShift(FName ShiftId)
{
	if (!Save)
	{
		return false;
	}
	FPuffersShift* Shift = Save->Workforce.Shifts.FindByPredicate([ShiftId](const FPuffersShift& S) { return S.ShiftId == ShiftId; });
	if (!Shift)
	{
		return false;
	}
	const FName Worker = Shift->SubstituteEmploymentId.IsNone() ? Shift->EmploymentId : Shift->SubstituteEmploymentId;
	const FPuffersEmployment* E = Save->Workforce.Employment.FindByPredicate([Worker](const FPuffersEmployment& X) { return X.EmploymentId == Worker; });
	if (!E || E->State != EPuffersEmploymentState::Active)
	{
		return false;
	}
	return FPuffersWorkforceRules::CheckIn(*Shift, E->WagePerShift, Now());
}

bool UPuffersCareerSubsystem::CompleteShift(FName ShiftId, int32 TasksDone)
{
	if (!Save)
	{
		return false;
	}
	FPuffersShift* Shift = Save->Workforce.Shifts.FindByPredicate([ShiftId](const FPuffersShift& S) { return S.ShiftId == ShiftId; });
	if (!Shift || Shift->WageLocked <= 0 || !FPuffersWorkforceRules::Complete(*Shift, TasksDone))
	{
		return false;
	}
	return true;
}

bool UPuffersCareerSubsystem::SubstituteShift(FName ShiftId, FName SubstituteEmploymentId)
{
	if (!Save)
	{
		return false;
	}
	FPuffersShift* Shift = Save->Workforce.Shifts.FindByPredicate([ShiftId](const FPuffersShift& S) { return S.ShiftId == ShiftId; });
	if (!Shift || (Shift->State != EPuffersShiftState::Published && Shift->State != EPuffersShiftState::Accepted && Shift->State != EPuffersShiftState::Declined))
	{
		return false;
	}
	const FPuffersEmployment* Sub = Save->Workforce.Employment.FindByPredicate([SubstituteEmploymentId](const FPuffersEmployment& E) { return E.EmploymentId == SubstituteEmploymentId; });
	if (!Sub || !Sub->bIsAI || Sub->State != EPuffersEmploymentState::Active || Sub->BusinessId != Shift->BusinessId || Sub->EmploymentId == Shift->EmploymentId)
	{
		return false;
	}
	Shift->SubstituteEmploymentId = SubstituteEmploymentId;
	Shift->State = EPuffersShiftState::Substituted;
	Shift->Revision++;
	return true;
}

// ---------------------------------------------------------------------------------------------------------- Payroll

FName UPuffersCareerSubsystem::RunPayroll(FName BusinessId, FDateTime Cutoff)
{
	FString Why;
	if (!Save || !RequireShared(Why))
	{
		return NAME_None;
	}
	const FName RunId = FName(*FString::Printf(TEXT("PAY_%s_%s"), *BusinessId.ToString(), *Cutoff.ToString(TEXT("%Y%m%d%H%M"))));
	if (Save->Workforce.PayRuns.ContainsByPredicate([RunId](const FPuffersPayRun& R) { return R.RunId == RunId; }))
	{
		return RunId;
	}

	FPuffersPayRun Run;
	Run.RunId = RunId;
	Run.BusinessId = BusinessId;
	Run.CutoffAt = Cutoff;
	Run.Accrued = FPuffersWorkforceRules::AccrueForRun(Save->Workforce.Shifts, BusinessId, Cutoff);

	// Wages per worker (the substitute is paid for a substituted shift).
	TMap<FName, int64> Owed;
	for (FPuffersShift& Shift : Save->Workforce.Shifts)
	{
		if (Shift.BusinessId == BusinessId && Shift.State == EPuffersShiftState::Completed && Shift.PaidInRun.IsNone() && Shift.EndsAt <= Cutoff)
		{
			Owed.FindOrAdd(Shift.SubstituteEmploymentId.IsNone() ? Shift.EmploymentId : Shift.SubstituteEmploymentId) += Shift.WageLocked;
			Shift.PaidInRun = RunId;
		}
	}

	const FName Reserve = FPuffersLedger::ReserveAccount(BusinessId);
	const FDateTime At = Now();
	TArray<FName> UnpaidAI;
	for (const TPair<FName, int64>& Pair : Owed)
	{
		const int32 Index = Save->Workforce.Employment.IndexOfByPredicate([&Pair](const FPuffersEmployment& E) { return E.EmploymentId == Pair.Key; });
		if (Index == INDEX_NONE || Pair.Value <= 0)
		{
			continue;
		}
		const FPuffersEmployment Worker = Save->Workforce.Employment[Index];
		const int64 FromReserve = FMath::Min(Pair.Value, FMath::Max<int64>(0, GetAvailable(Reserve)));
		const int64 FromPocket = Pair.Value - FromReserve;
		bool bPaid = false;
		if (FromPocket <= FMath::Max<int64>(0, GetAvailable(FPuffersLedger::Pocket)))
		{
			const FName TxId = FName(*FString::Printf(TEXT("%s_%s"), *RunId.ToString(), *Worker.EmploymentId.ToString()));
			TArray<FPuffersLedgerLeg> Legs;
			if (FromReserve > 0)
			{
				Legs.Add(FPuffersLedgerLeg::Make(Reserve, -FromReserve, TEXT("Wages")));
			}
			if (FromPocket > 0)
			{
				Legs.Add(FPuffersLedgerLeg::Make(FPuffersLedger::Pocket, -FromPocket, TEXT("Wages")));
			}
			Legs.Add(FPuffersLedgerLeg::Make(WorkerAccount(Worker), Pair.Value, Worker.Title));
			bPaid = PostLedger([&](FPuffersLedger& L) { return L.Post(TxId, Legs, FString::Printf(TEXT("Pay run %s"), *RunId.ToString()), At); }) == EPuffersLedgerResult::Ok;
		}

		FPuffersEmployment& E = Save->Workforce.Employment[Index];
		if (bPaid)
		{
			Run.Paid += Pair.Value;
			E.UnpaidRuns = 0;
			E.Trust = FMath::Min(100, E.Trust + 2);
			if (FPuffersOperation* Operation = FPuffersCareerRules::FindOperation(Save->Owner, BusinessId))
			{
				Operation->NetProfit -= Pair.Value;
			}
		}
		else
		{
			// Unpaid wages become a liability that survives quitting, firing and closing.
			E.Arrears += Pair.Value;
			E.UnpaidRuns++;
			E.Trust = FMath::Max(0, E.Trust - 15);
			Run.bUnpaid = true;
			Run.Alerts.Add(E.EmploymentId);
			Save->Workforce.Notices.Add(FString::Printf(TEXT("%s: %s"), *E.WorkerId.ToString(), *FPuffersWorkforceRules::UnpaidAlert(E.Arrears)));
			if (E.bIsAI)
			{
				UnpaidAI.Add(E.EmploymentId);
			}
		}
	}

	Save->Workforce.PayRuns.Add(Run);
	if (Run.bUnpaid)
	{
		Notify(FString::Printf(TEXT("Payroll at %s came up short: %lld of %lld paid. Unpaid wages are owed until you settle them, and you can't hire until then."),
			*BusinessId.ToString(), Run.Paid, Run.Accrued));
		Publish(EventPayrollUnpaid, BusinessId);
		for (const FName& EmploymentId : UnpaidAI)
		{
			const FPuffersEmployment* E = Save->Workforce.Employment.FindByPredicate([EmploymentId](const FPuffersEmployment& X) { return X.EmploymentId == EmploymentId; });
			if (E)
			{
				ChooseAfterUnpaid(EmploymentId, FPuffersWorkforceRules::DecideAI(*E));
			}
		}
	}
	else if (Run.Paid > 0)
	{
		Notify(FString::Printf(TEXT("Pay day at %s: %lld paid."), *BusinessId.ToString(), Run.Paid));
	}
	SaveNow();
	return RunId;
}

bool UPuffersCareerSubsystem::ChooseAfterUnpaid(FName EmploymentId, EPuffersUnpaidChoice Choice)
{
	if (!Save)
	{
		return false;
	}
	FPuffersEmployment* E = Save->Workforce.Employment.FindByPredicate([EmploymentId](const FPuffersEmployment& X) { return X.EmploymentId == EmploymentId; });
	if (!E || E->Arrears <= 0)
	{
		return false;
	}
	E->LastChoice = Choice;
	switch (Choice)
	{
	case EPuffersUnpaidChoice::Wait:
		Notify(FString::Printf(TEXT("%s is waiting to be paid (%lld owed)."), *E->WorkerId.ToString(), E->Arrears));
		break;
	case EPuffersUnpaidChoice::Quit:
		E->State = EPuffersEmploymentState::Quit;
		Notify(FString::Printf(TEXT("%s quit over unpaid wages. You still owe %lld."), *E->WorkerId.ToString(), E->Arrears));
		break;
	case EPuffersUnpaidChoice::RobTheBoss:
		// The worker walks and the "rob the boss" street mission opens for them (or their AI controller acts it out).
		E->State = EPuffersEmploymentState::Quit;
		Notify(FString::Printf(TEXT("%s quit and is coming for what they're owed. Watch your shop."), *E->WorkerId.ToString()));
		Publish(EventRobTheBossOffered, EmploymentId);
		break;
	}
	SaveNow();
	return true;
}

int64 UPuffersCareerSubsystem::SettleArrears(FName BusinessId)
{
	if (!Save)
	{
		return 0;
	}
	const FName Reserve = FPuffersLedger::ReserveAccount(BusinessId);
	const FDateTime At = Now();
	int64 Settled = 0;
	for (int32 Index = 0; Index < Save->Workforce.Employment.Num(); ++Index)
	{
		const FPuffersEmployment Worker = Save->Workforce.Employment[Index];
		if (Worker.BusinessId != BusinessId || Worker.Arrears <= 0)
		{
			continue;
		}
		const int64 FromReserve = FMath::Min(Worker.Arrears, FMath::Max<int64>(0, GetAvailable(Reserve)));
		const int64 FromPocket = Worker.Arrears - FromReserve;
		if (FromPocket > FMath::Max<int64>(0, GetAvailable(FPuffersLedger::Pocket)))
		{
			continue;
		}
		const FName TxId = MakeId(TEXT("ARREARS"));
		TArray<FPuffersLedgerLeg> Legs;
		if (FromReserve > 0)
		{
			Legs.Add(FPuffersLedgerLeg::Make(Reserve, -FromReserve, TEXT("Back wages")));
		}
		if (FromPocket > 0)
		{
			Legs.Add(FPuffersLedgerLeg::Make(FPuffersLedger::Pocket, -FromPocket, TEXT("Back wages")));
		}
		Legs.Add(FPuffersLedgerLeg::Make(WorkerAccount(Worker), Worker.Arrears, TEXT("Back wages")));
		if (PostLedger([&](FPuffersLedger& L) { return L.Post(TxId, Legs, TEXT("Back wages"), At); }) != EPuffersLedgerResult::Ok)
		{
			continue;
		}
		FPuffersEmployment& E = Save->Workforce.Employment[Index];
		Settled += E.Arrears;
		if (FPuffersOperation* Operation = FPuffersCareerRules::FindOperation(Save->Owner, BusinessId))
		{
			Operation->NetProfit -= E.Arrears;
		}
		E.Arrears = 0;
		E.UnpaidRuns = 0;
		E.Trust = FMath::Min(100, E.Trust + 5);
	}
	if (Settled > 0)
	{
		Notify(FString::Printf(TEXT("Back wages settled at %s: %lld."), *BusinessId.ToString(), Settled));
		SaveNow();
	}
	return Settled;
}
