// Pure rules for loans (quotes, liens, default) and payroll (hiring floor, shifts, weekly runs, unpaid choices).

#pragma once

#include "CoreMinimal.h"
#include "Career/PuffersWorkforceTypes.h"

struct PUFFERSCLUB_API FPuffersWorkforceRules
{
	// --- Loans ---

	// Builds an offer (state Offered) or explains why the collateral doesn't qualify.
	static bool QuoteLoan(const FPuffersLenderDefinition& Lender, const FPuffersCollateral& Collateral, const TMap<FName, FName>& Liens,
		FName LoanId, int64 RequestedPrincipal, const FDateTime& Now, FPuffersLoan& OutLoan, FString& OutWhy);

	// Marks the loan funded and records the lien. False if already funded or the collateral is now encumbered.
	static bool Fund(FPuffersLoan& Loan, TMap<FName, FName>& Liens, FName DisbursementTx, FString& OutWhy);

	// Applies a payment to the earliest installments. Returns the amount applied (never more than outstanding).
	// Full payoff releases the lien.
	static int64 ApplyPayment(FPuffersLoan& Loan, TMap<FName, FName>& Liens, int64 Amount, FName PaymentTx);

	// True once an installment is past due by more than the lender's grace.
	static bool IsInDefault(const FPuffersLoan& Loan, const FPuffersLenderDefinition& Lender, const FDateTime& Now);

	// Selling, trading or moving title on a liened asset is blocked unless the lien is settled.
	static bool CanTransferAsset(const TMap<FName, FName>& Liens, FName AssetId, FName& OutLoanId);

	// --- Payroll ---

	// Weekly payroll of active employment (plus already accepted offers).
	static int64 ProjectedWeeklyPayroll(const TArray<FPuffersEmployment>& Employment, FName BusinessId);

	static int64 UnresolvedWages(const TArray<FPuffersEmployment>& Employment, FName BusinessId);

	// Hiring floor: available cash >= max(business minimum, projected weekly payroll including the new hire),
	// and no unresolved wages.
	static bool CanHire(const TArray<FPuffersEmployment>& Employment, FName BusinessId, int64 AvailableCash, int64 BusinessMinimum, const FPuffersEmployment& NewHire, FString& OutWhy);

	// Check-in locks the wage for the shift.
	static bool CheckIn(FPuffersShift& Shift, int32 Wage, const FDateTime& Now);

	static bool Complete(FPuffersShift& Shift, int32 TasksDone);

	// Wages accrued from completed (or substituted) shifts at or before the cutoff and not yet in a run.
	static int64 AccrueForRun(const TArray<FPuffersShift>& Shifts, FName BusinessId, const FDateTime& Cutoff);

	// The AI worker's choice after an unpaid run, from trust and arrears.
	static EPuffersUnpaidChoice DecideAI(const FPuffersEmployment& Worker);

	static FString UnpaidAlert(int64 Owed);
};
