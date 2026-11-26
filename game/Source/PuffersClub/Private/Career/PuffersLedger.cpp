#include "Career/PuffersLedger.h"

const FName FPuffersLedger::Pocket = TEXT("Pocket");
const FName FPuffersLedger::Bank = TEXT("Bank");

FName FPuffersLedger::ReserveAccount(FName BusinessId)
{
	return FName(*FString::Printf(TEXT("Reserve.%s"), *BusinessId.ToString()));
}

bool FPuffersLedger::IsOwnedAccount(FName Account)
{
	if (Account == Pocket || Account == Bank)
	{
		return true;
	}
	return Account.ToString().StartsWith(TEXT("Reserve."));
}

const FPuffersLedgerTransaction* FPuffersLedger::FindTransaction(FName TxId) const
{
	return Transactions.FindByPredicate([TxId](const FPuffersLedgerTransaction& T) { return T.TxId == TxId; });
}

FPuffersLedgerTransaction* FPuffersLedger::FindTransactionMutable(FName TxId)
{
	return Transactions.FindByPredicate([TxId](const FPuffersLedgerTransaction& T) { return T.TxId == TxId; });
}

const FPuffersLedgerHold* FPuffersLedger::FindHold(FName HoldId) const
{
	return Holds.FindByPredicate([HoldId](const FPuffersLedgerHold& H) { return H.HoldId == HoldId; });
}

FPuffersLedgerHold* FPuffersLedger::FindHoldMutable(FName HoldId)
{
	return Holds.FindByPredicate([HoldId](const FPuffersLedgerHold& H) { return H.HoldId == HoldId; });
}

int64 FPuffersLedger::GetBalance(FName Account) const
{
	const int64* Found = Balances.Find(Account);
	return Found ? *Found : 0;
}

int64 FPuffersLedger::GetHeld(FName Account) const
{
	int64 Held = 0;
	for (const FPuffersLedgerHold& Hold : Holds)
	{
		if (Hold.Account == Account && Hold.State == EPuffersHoldState::Active)
		{
			Held += Hold.Amount;
		}
	}
	return Held;
}

int64 FPuffersLedger::GetTotalOwned() const
{
	int64 Total = 0;
	for (const TPair<FName, int64>& Pair : Balances)
	{
		if (IsOwnedAccount(Pair.Key))
		{
			Total += Pair.Value;
		}
	}
	return Total;
}

TArray<FName> FPuffersLedger::GetOwnedAccounts() const
{
	TArray<FName> Out;
	for (const TPair<FName, int64>& Pair : Balances)
	{
		if (IsOwnedAccount(Pair.Key) && Pair.Value != 0)
		{
			Out.Add(Pair.Key);
		}
	}
	Out.Sort(FNameLexicalLess());
	return Out;
}

EPuffersLedgerResult FPuffersLedger::Post(FName TxId, const TArray<FPuffersLedgerLeg>& Legs, const FString& Memo, const FDateTime& Now)
{
	if (TxId.IsNone() || Legs.Num() < 2)
	{
		return EPuffersLedgerResult::Invalid;
	}
	if (HasTransaction(TxId))
	{
		return EPuffersLedgerResult::Duplicate;
	}

	int64 Sum = 0;
	TMap<FName, int64> NetByAccount;
	for (const FPuffersLedgerLeg& Leg : Legs)
	{
		if (Leg.Account.IsNone())
		{
			return EPuffersLedgerResult::Invalid;
		}
		Sum += Leg.Amount;
		NetByAccount.FindOrAdd(Leg.Account) += Leg.Amount;
	}
	if (Sum != 0)
	{
		return EPuffersLedgerResult::Unbalanced;
	}

	// Owned accounts can't be overdrawn or spend money that's on hold.
	for (const TPair<FName, int64>& Pair : NetByAccount)
	{
		if (Pair.Value < 0 && IsOwnedAccount(Pair.Key) && GetAvailable(Pair.Key) + Pair.Value < 0)
		{
			return EPuffersLedgerResult::Insufficient;
		}
	}

	for (const TPair<FName, int64>& Pair : NetByAccount)
	{
		Balances.FindOrAdd(Pair.Key) += Pair.Value;
	}

	FPuffersLedgerTransaction& Tx = Transactions.AddDefaulted_GetRef();
	Tx.TxId = TxId;
	Tx.PostedAt = Now;
	Tx.Memo = Memo;
	Tx.Legs = Legs;
	return EPuffersLedgerResult::Ok;
}

EPuffersLedgerResult FPuffersLedger::Transfer(FName TxId, FName From, FName To, int64 Amount, const FString& Memo, const FDateTime& Now)
{
	if (Amount <= 0 || From == To)
	{
		return EPuffersLedgerResult::Invalid;
	}
	TArray<FPuffersLedgerLeg> Legs;
	Legs.Add(FPuffersLedgerLeg::Make(From, -Amount, Memo));
	Legs.Add(FPuffersLedgerLeg::Make(To, Amount, Memo));
	return Post(TxId, Legs, Memo, Now);
}

EPuffersLedgerResult FPuffersLedger::TransferWithFee(FName TxId, FName From, FName To, int64 Gross, FName FeeAccount, int64 Fee, const FString& Memo, const FDateTime& Now)
{
	if (Gross <= 0 || Fee < 0 || Fee > Gross)
	{
		return EPuffersLedgerResult::Invalid;
	}
	TArray<FPuffersLedgerLeg> Legs;
	Legs.Add(FPuffersLedgerLeg::Make(From, -Gross, TEXT("Gross")));
	if (Fee > 0)
	{
		Legs.Add(FPuffersLedgerLeg::Make(FeeAccount, Fee, TEXT("Fee")));
	}
	if (Gross - Fee > 0)
	{
		Legs.Add(FPuffersLedgerLeg::Make(To, Gross - Fee, TEXT("Net")));
	}
	return Post(TxId, Legs, Memo, Now);
}

EPuffersLedgerResult FPuffersLedger::PlaceHold(FName HoldId, FName Account, int64 Amount, const FString& Memo, const FDateTime& Now)
{
	if (HoldId.IsNone() || Amount <= 0 || !IsOwnedAccount(Account))
	{
		return EPuffersLedgerResult::Invalid;
	}
	if (FindHold(HoldId))
	{
		return EPuffersLedgerResult::Duplicate;
	}
	if (GetAvailable(Account) < Amount)
	{
		return EPuffersLedgerResult::Insufficient;
	}
	FPuffersLedgerHold& Hold = Holds.AddDefaulted_GetRef();
	Hold.HoldId = HoldId;
	Hold.Account = Account;
	Hold.Amount = Amount;
	Hold.Memo = Memo;
	Hold.PlacedAt = Now;
	return EPuffersLedgerResult::Ok;
}

EPuffersLedgerResult FPuffersLedger::CaptureHold(FName HoldId, FName TxId, FName To, int64 Amount, const FDateTime& Now)
{
	FPuffersLedgerHold* Hold = FindHoldMutable(HoldId);
	if (!Hold)
	{
		return EPuffersLedgerResult::UnknownHold;
	}
	if (Hold->State == EPuffersHoldState::Captured && Hold->CapturedTx == TxId)
	{
		return EPuffersLedgerResult::Duplicate;
	}
	if (Hold->State != EPuffersHoldState::Active)
	{
		return EPuffersLedgerResult::HoldNotActive;
	}
	if (Amount <= 0 || Amount > Hold->Amount)
	{
		return EPuffersLedgerResult::Invalid;
	}

	// Close the hold first so the captured money counts as available to this posting.
	const FName Account = Hold->Account;
	Hold->State = EPuffersHoldState::Captured;
	const EPuffersLedgerResult Result = Transfer(TxId, Account, To, Amount, Hold->Memo, Now);
	Hold = FindHoldMutable(HoldId);
	if (Result != EPuffersLedgerResult::Ok)
	{
		Hold->State = EPuffersHoldState::Active;
		return Result;
	}
	Hold->CapturedTx = TxId;
	Hold->SettledAt = Now;
	if (FPuffersLedgerTransaction* Tx = FindTransactionMutable(TxId))
	{
		Tx->FromHold = HoldId;
	}
	return EPuffersLedgerResult::Ok;
}

EPuffersLedgerResult FPuffersLedger::ReleaseHold(FName HoldId, const FDateTime& Now)
{
	FPuffersLedgerHold* Hold = FindHoldMutable(HoldId);
	if (!Hold)
	{
		return EPuffersLedgerResult::UnknownHold;
	}
	if (Hold->State == EPuffersHoldState::Released)
	{
		return EPuffersLedgerResult::Duplicate;
	}
	if (Hold->State != EPuffersHoldState::Active)
	{
		return EPuffersLedgerResult::HoldNotActive;
	}
	Hold->State = EPuffersHoldState::Released;
	Hold->SettledAt = Now;
	return EPuffersLedgerResult::Ok;
}

EPuffersLedgerResult FPuffersLedger::Reverse(FName TxId, FName NewTxId, const FString& Memo, const FDateTime& Now)
{
	const FPuffersLedgerTransaction* Source = FindTransaction(TxId);
	if (!Source)
	{
		return EPuffersLedgerResult::UnknownTransaction;
	}
	if (!Source->ReversedBy.IsNone())
	{
		return Source->ReversedBy == NewTxId ? EPuffersLedgerResult::Duplicate : EPuffersLedgerResult::AlreadyReversed;
	}
	if (!Source->ReversalOf.IsNone())
	{
		return EPuffersLedgerResult::Invalid;
	}

	TArray<FPuffersLedgerLeg> Mirror;
	for (const FPuffersLedgerLeg& Leg : Source->Legs)
	{
		Mirror.Add(FPuffersLedgerLeg::Make(Leg.Account, -Leg.Amount, Leg.Line));
	}
	const EPuffersLedgerResult Result = Post(NewTxId, Mirror, Memo, Now);
	if (Result != EPuffersLedgerResult::Ok)
	{
		return Result;
	}
	FindTransactionMutable(NewTxId)->ReversalOf = TxId;
	FindTransactionMutable(TxId)->ReversedBy = NewTxId;
	return EPuffersLedgerResult::Ok;
}

FPuffersReceipt FPuffersLedger::MakeReceipt(FName TxId) const
{
	FPuffersReceipt Receipt;
	const FPuffersLedgerTransaction* Tx = FindTransaction(TxId);
	if (!Tx)
	{
		return Receipt;
	}
	Receipt.TxId = Tx->TxId;
	Receipt.Memo = Tx->Memo;
	Receipt.PostedAt = Tx->PostedAt;
	Receipt.Lines = Tx->Legs;
	Receipt.ReversalOf = Tx->ReversalOf;
	Receipt.ReversedBy = Tx->ReversedBy;

	for (const FPuffersLedgerLeg& Leg : Tx->Legs)
	{
		if (IsOwnedAccount(Leg.Account))
		{
			Receipt.OwnedMovement += Leg.Amount;
		}
		if (Leg.Line == TEXT("Fee"))
		{
			Receipt.Fee += FMath::Abs(Leg.Amount);
		}
		if (Leg.Amount < 0)
		{
			Receipt.Gross += -Leg.Amount;
		}
	}
	Receipt.Net = Receipt.Gross - Receipt.Fee;
	return Receipt;
}
