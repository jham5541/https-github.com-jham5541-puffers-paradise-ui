// Secured loans, hiring, shift schedules and weekly payroll (handoff Rev 12, "Secured loans and collateral" and
// "Hiring players and AI NPCs, scheduling, weekly pay"). All Puff Cash; no real credit or real-world assets.
// Every non-player worker is an AI NPC driven by the existing NPC controller; this layer owns the wage records.

#pragma once

#include "CoreMinimal.h"
#include "PuffersWorkforceTypes.generated.h"

UENUM(BlueprintType)
enum class EPuffersCollateralKind : uint8
{
	Vehicle,
	Home,
	Jewelry
};

UENUM(BlueprintType)
enum class EPuffersLoanState : uint8
{
	Offered,
	Funded,
	Repaid,
	Defaulted,
	Repossessed,
	Declined
};

UENUM(BlueprintType)
enum class EPuffersEmploymentState : uint8
{
	Offered,
	Active,
	Quit,
	Fired
};

UENUM(BlueprintType)
enum class EPuffersShiftState : uint8
{
	Published,
	Accepted,
	Declined,
	ChangeRequested,
	CheckedIn,
	Completed,
	NoShow,
	Substituted
};

UENUM(BlueprintType)
enum class EPuffersUnpaidChoice : uint8
{
	Wait,
	Quit,
	RobTheBoss
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersLenderDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName LenderId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Name;

	// Loan-to-value by collateral kind (0..1).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float VehicleLtv = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HomeLtv = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float JewelryLtv = 0.4f;

	// Flat origination fee as a fraction of principal.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float FeeRate = 0.05f;

	// Simple interest over the whole term, as a fraction of principal.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float InterestRate = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Installments = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 DaysBetweenInstallments = 7;

	// Days after a missed installment before the lender can repossess.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 DefaultGraceDays = 3;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCollateral
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	EPuffersCollateralKind Kind = EPuffersCollateralKind::Vehicle;

	// A vehicle, home or jewelry instance id.
	UPROPERTY(SaveGame, BlueprintReadWrite)
	FName AssetId;

	// Lender's game valuation.
	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 Value = 0;

	// 0..1 condition (vehicles wear).
	UPROPERTY(SaveGame, BlueprintReadWrite)
	float Condition = 1.f;

	// The borrower's ownership share (0..1).
	UPROPERTY(SaveGame, BlueprintReadWrite)
	float OwnershipShare = 1.f;

	// A rented apartment has no pledgeable title.
	UPROPERTY(SaveGame, BlueprintReadWrite)
	bool bRented = false;

	// A home on a subscribed plot: both home and plot rights must be transferable.
	UPROPERTY(SaveGame, BlueprintReadWrite)
	bool bPlotRightsTransferable = true;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersInstallment
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime DueAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Amount = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Paid = 0;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersLoan
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LoanId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LenderId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FPuffersCollateral Collateral;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Principal = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Fee = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Interest = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersInstallment> Schedule;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersLoanState State = EPuffersLoanState::Offered;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime OfferedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName DisbursementTx;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> PaymentTxs;

	// What the offer screen explained before acceptance.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString Disclosure;

	int64 TotalDue() const { return Principal + Fee + Interest; }

	int64 Outstanding() const
	{
		int64 Left = 0;
		for (const FPuffersInstallment& I : Schedule)
		{
			Left += FMath::Max<int64>(0, I.Amount - I.Paid);
		}
		return Left;
	}
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersEmployment
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName EmploymentId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName BusinessId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName WorkerId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bIsAI = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString Title;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 WagePerShift = 100;

	// Shifts per week used to project payroll for the hiring floor.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ShiftsPerWeek = 5;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersEmploymentState State = EPuffersEmploymentState::Offered;

	// Wages owed and unpaid (a liability that survives quitting, firing or closing).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Arrears = 0;

	// Unpaid pay runs in a row (AI workers decide from this and trust).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 UnpaidRuns = 0;

	// 0..100.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Trust = 60;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersUnpaidChoice LastChoice = EPuffersUnpaidChoice::Wait;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersShift
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ShiftId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName BusinessId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName EmploymentId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime StartsAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime EndsAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString Role;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> Tasks;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersShiftState State = EPuffersShiftState::Published;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Revision = 1;

	// AI substitute worker when the assigned worker didn't arrive.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SubstituteEmploymentId;

	// Locked at check-in; a later schedule change can't rewrite it.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 WageLocked = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 TasksDone = 0;

	// Included in this pay run (None = not yet).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName PaidInRun;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersPayRun
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName RunId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName BusinessId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime CutoffAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Accrued = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Paid = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bUnpaid = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> Alerts;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersWorkforceState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersLoan> Loans;

	// Asset id -> loan id holding a lien on it.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TMap<FName, FName> Liens;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersEmployment> Employment;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersShift> Shifts;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersPayRun> PayRuns;

	// Player-facing alerts ("Your boss can't pay you this week...").
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FString> Notices;
};
