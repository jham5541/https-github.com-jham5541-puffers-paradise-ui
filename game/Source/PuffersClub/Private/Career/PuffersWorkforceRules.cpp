#include "Career/PuffersWorkforceRules.h"

bool FPuffersWorkforceRules::QuoteLoan(const FPuffersLenderDefinition& Lender, const FPuffersCollateral& Collateral, const TMap<FName, FName>& Liens,
	FName LoanId, int64 RequestedPrincipal, const FDateTime& Now, FPuffersLoan& OutLoan, FString& OutWhy)
{
	if (Collateral.AssetId.IsNone() || Collateral.Value <= 0)
	{
		OutWhy = TEXT("The lender can't value that.");
		return false;
	}
	if (Collateral.bRented)
	{
		OutWhy = TEXT("A rented place has no title to pledge.");
		return false;
	}
	if (Collateral.Kind == EPuffersCollateralKind::Home && !Collateral.bPlotRightsTransferable)
	{
		OutWhy = TEXT("The plot rights under this home can't be transferred, so it can't secure a loan.");
		return false;
	}
	if (Liens.Contains(Collateral.AssetId))
	{
		OutWhy = TEXT("That's already pledged on another loan.");
		return false;
	}

	float Ltv = Lender.VehicleLtv;
	if (Collateral.Kind == EPuffersCollateralKind::Home)
	{
		Ltv = Lender.HomeLtv;
	}
	else if (Collateral.Kind == EPuffersCollateralKind::Jewelry)
	{
		Ltv = Lender.JewelryLtv;
	}
	const int64 MaxPrincipal = static_cast<int64>(FMath::FloorToDouble(Collateral.Value * FMath::Clamp(Collateral.Condition, 0.f, 1.f)
		* FMath::Clamp(Collateral.OwnershipShare, 0.f, 1.f) * Ltv));
	if (MaxPrincipal <= 0)
	{
		OutWhy = TEXT("It isn't worth enough to borrow against.");
		return false;
	}
	const int64 Principal = RequestedPrincipal > 0 ? FMath::Min(RequestedPrincipal, MaxPrincipal) : MaxPrincipal;

	OutLoan = FPuffersLoan();
	OutLoan.LoanId = LoanId;
	OutLoan.LenderId = Lender.LenderId;
	OutLoan.Collateral = Collateral;
	OutLoan.Principal = Principal;
	OutLoan.Fee = static_cast<int64>(FMath::CeilToDouble(Principal * Lender.FeeRate));
	OutLoan.Interest = static_cast<int64>(FMath::CeilToDouble(Principal * Lender.InterestRate));
	OutLoan.OfferedAt = Now;
	OutLoan.State = EPuffersLoanState::Offered;

	const int32 Count = FMath::Max(1, Lender.Installments);
	const int64 Total = OutLoan.TotalDue();
	int64 Allocated = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FPuffersInstallment& Installment = OutLoan.Schedule.AddDefaulted_GetRef();
		Installment.DueAt = Now + FTimespan::FromDays(static_cast<double>(Lender.DaysBetweenInstallments) * (Index + 1));
		Installment.Amount = Index == Count - 1 ? Total - Allocated : Total / Count;
		Allocated += Installment.Amount;
	}
	OutLoan.Disclosure = FString::Printf(TEXT("%s lends %lld against your %s (valued %d). Fee %lld, interest %lld, %d payments of about %lld every %d days. "
		"Miss a payment by more than %d days and they can take the %s."),
		*Lender.Name, Principal, *Collateral.AssetId.ToString(), Collateral.Value, OutLoan.Fee, OutLoan.Interest, Count, Total / Count,
		Lender.DaysBetweenInstallments, Lender.DefaultGraceDays, *Collateral.AssetId.ToString());
	return true;
}

bool FPuffersWorkforceRules::Fund(FPuffersLoan& Loan, TMap<FName, FName>& Liens, FName DisbursementTx, FString& OutWhy)
{
	if (Loan.State != EPuffersLoanState::Offered)
	{
		OutWhy = TEXT("That offer isn't open.");
		return false;
	}
	if (const FName* Existing = Liens.Find(Loan.Collateral.AssetId))
	{
		if (*Existing != Loan.LoanId)
		{
			OutWhy = TEXT("That asset was pledged elsewhere in the meantime.");
			return false;
		}
	}
	Liens.Add(Loan.Collateral.AssetId, Loan.LoanId);
	Loan.State = EPuffersLoanState::Funded;
	Loan.DisbursementTx = DisbursementTx;
	return true;
}

int64 FPuffersWorkforceRules::ApplyPayment(FPuffersLoan& Loan, TMap<FName, FName>& Liens, int64 Amount, FName PaymentTx)
{
	if (Loan.State != EPuffersLoanState::Funded && Loan.State != EPuffersLoanState::Defaulted)
	{
		return 0;
	}
	if (Loan.PaymentTxs.Contains(PaymentTx))
	{
		return 0;
	}
	int64 Left = FMath::Min(Amount, Loan.Outstanding());
	const int64 Applied = Left;
	for (FPuffersInstallment& Installment : Loan.Schedule)
	{
		if (Left <= 0)
		{
			break;
		}
		const int64 Due = Installment.Amount - Installment.Paid;
		const int64 Pay = FMath::Min(Due, Left);
		Installment.Paid += Pay;
		Left -= Pay;
	}
	if (Applied > 0)
	{
		Loan.PaymentTxs.Add(PaymentTx);
	}
	if (Loan.Outstanding() == 0)
	{
		Loan.State = EPuffersLoanState::Repaid;
		Liens.Remove(Loan.Collateral.AssetId);
	}
	return Applied;
}

bool FPuffersWorkforceRules::IsInDefault(const FPuffersLoan& Loan, const FPuffersLenderDefinition& Lender, const FDateTime& Now)
{
	if (Loan.State != EPuffersLoanState::Funded && Loan.State != EPuffersLoanState::Defaulted)
	{
		return false;
	}
	for (const FPuffersInstallment& Installment : Loan.Schedule)
	{
		if (Installment.Paid < Installment.Amount && Now > Installment.DueAt + FTimespan::FromDays(Lender.DefaultGraceDays))
		{
			return true;
		}
	}
	return false;
}

bool FPuffersWorkforceRules::CanTransferAsset(const TMap<FName, FName>& Liens, FName AssetId, FName& OutLoanId)
{
	if (const FName* Loan = Liens.Find(AssetId))
	{
		OutLoanId = *Loan;
		return false;
	}
	return true;
}

int64 FPuffersWorkforceRules::ProjectedWeeklyPayroll(const TArray<FPuffersEmployment>& Employment, FName BusinessId)
{
	int64 Total = 0;
	for (const FPuffersEmployment& E : Employment)
	{
		if (E.BusinessId == BusinessId && E.State == EPuffersEmploymentState::Active)
		{
			Total += static_cast<int64>(E.WagePerShift) * E.ShiftsPerWeek;
		}
	}
	return Total;
}

int64 FPuffersWorkforceRules::UnresolvedWages(const TArray<FPuffersEmployment>& Employment, FName BusinessId)
{
	int64 Total = 0;
	for (const FPuffersEmployment& E : Employment)
	{
		if (E.BusinessId == BusinessId)
		{
			Total += E.Arrears;
		}
	}
	return Total;
}

bool FPuffersWorkforceRules::CanHire(const TArray<FPuffersEmployment>& Employment, FName BusinessId, int64 AvailableCash, int64 BusinessMinimum, const FPuffersEmployment& NewHire, FString& OutWhy)
{
	const int64 Owed = UnresolvedWages(Employment, BusinessId);
	if (Owed > 0)
	{
		OutWhy = FString::Printf(TEXT("Settle %lld in unpaid wages before hiring."), Owed);
		return false;
	}
	const int64 Projected = ProjectedWeeklyPayroll(Employment, BusinessId) + static_cast<int64>(NewHire.WagePerShift) * NewHire.ShiftsPerWeek;
	const int64 Needed = FMath::Max(BusinessMinimum, Projected);
	if (AvailableCash < Needed)
	{
		OutWhy = FString::Printf(TEXT("You need %lld Puff Cash available to cover a week of payroll (you have %lld)."), Needed, AvailableCash);
		return false;
	}
	return true;
}

bool FPuffersWorkforceRules::CheckIn(FPuffersShift& Shift, int32 Wage, const FDateTime& Now)
{
	if (Shift.State != EPuffersShiftState::Accepted && Shift.State != EPuffersShiftState::Published && Shift.State != EPuffersShiftState::Substituted)
	{
		return false;
	}
	if (Shift.State != EPuffersShiftState::Substituted)
	{
		Shift.State = EPuffersShiftState::CheckedIn;
	}
	Shift.WageLocked = Wage;
	return true;
}

bool FPuffersWorkforceRules::Complete(FPuffersShift& Shift, int32 TasksDone)
{
	if (Shift.State != EPuffersShiftState::CheckedIn && Shift.State != EPuffersShiftState::Substituted)
	{
		return false;
	}
	Shift.State = EPuffersShiftState::Completed;
	Shift.TasksDone = TasksDone;
	return true;
}

int64 FPuffersWorkforceRules::AccrueForRun(const TArray<FPuffersShift>& Shifts, FName BusinessId, const FDateTime& Cutoff)
{
	int64 Total = 0;
	for (const FPuffersShift& Shift : Shifts)
	{
		if (Shift.BusinessId == BusinessId && Shift.State == EPuffersShiftState::Completed && Shift.PaidInRun.IsNone() && Shift.EndsAt <= Cutoff)
		{
			Total += Shift.WageLocked;
		}
	}
	return Total;
}

EPuffersUnpaidChoice FPuffersWorkforceRules::DecideAI(const FPuffersEmployment& Worker)
{
	if (Worker.Trust < 25 && Worker.UnpaidRuns >= 2)
	{
		return EPuffersUnpaidChoice::RobTheBoss;
	}
	if (Worker.Trust < 50 || Worker.UnpaidRuns >= 2)
	{
		return EPuffersUnpaidChoice::Quit;
	}
	return EPuffersUnpaidChoice::Wait;
}

FString FPuffersWorkforceRules::UnpaidAlert(int64 Owed)
{
	return FString::Printf(TEXT("Your boss can't pay you this week. You're owed %lld Puff Cash."), Owed);
}
