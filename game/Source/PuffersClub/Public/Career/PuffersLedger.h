// Double-entry Puff Cash wallet (handoff Rev 12, "Wallet and receipts").
//
// * Accounts are named. The player owns Pocket, Bank and one Reserve per business ("Reserve.<BusinessId>").
//   Everything else is a counterparty outside the player's estate: "World.*" (NPC economy), "Player.<Id>"
//   (another player), "Escrow.<Id>" (held deposits) and "Claim.<Id>" (server claim storage).
// * Every transaction has a unique id and posts legs that sum to zero, so transfers between owned accounts
//   never change total owned funds. Posting the same id twice is a no-op that reports Duplicate.
// * Holds reserve money without moving it: available = posted - active holds. A captured hold posts once;
//   a released hold posts nothing.
// * Reversals post their own transaction with a stable link back to the source receipt.
//
// Amounts are integer Puff Cash (the game's minor unit). All of this is pure so it can be tested without a world.

#pragma once

#include "CoreMinimal.h"
#include "PuffersLedger.generated.h"

UENUM(BlueprintType)
enum class EPuffersLedgerResult : uint8
{
	Ok,
	Duplicate,       // this transaction id already posted; nothing changed
	Unbalanced,      // legs don't sum to zero
	Insufficient,    // an owned account would go below zero (or below its holds)
	UnknownHold,
	HoldNotActive,
	UnknownTransaction,
	AlreadyReversed,
	Invalid
};

UENUM(BlueprintType)
enum class EPuffersHoldState : uint8
{
	Active,
	Captured,
	Released
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersLedgerLeg
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	FName Account;

	// Positive credits the account, negative debits it.
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	int64 Amount = 0;

	// Receipt line label ("Gross", "Fee", "Wages", ...).
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	FString Line;

	static FPuffersLedgerLeg Make(FName InAccount, int64 InAmount, const FString& InLine)
	{
		FPuffersLedgerLeg Leg;
		Leg.Account = InAccount;
		Leg.Amount = InAmount;
		Leg.Line = InLine;
		return Leg;
	}
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersLedgerTransaction
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName TxId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime PostedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString Memo;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersLedgerLeg> Legs;

	// Set on a reversal: the transaction it undoes.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ReversalOf;

	// Set on a transaction that has been reversed: the reversal's id.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ReversedBy;

	// Set when a hold was captured into this transaction.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName FromHold;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersLedgerHold
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName HoldId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName Account;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Amount = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersHoldState State = EPuffersHoldState::Active;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString Memo;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime PlacedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime SettledAt;

	// The transaction a capture posted.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName CapturedTx;
};

// What the phone's wallet app shows for one transaction. Gross - Fee = Net, and the account movement matches.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersReceipt
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName TxId;

	UPROPERTY(BlueprintReadOnly)
	FString Memo;

	UPROPERTY(BlueprintReadOnly)
	FDateTime PostedAt;

	UPROPERTY(BlueprintReadOnly)
	int64 Gross = 0;

	UPROPERTY(BlueprintReadOnly)
	int64 Fee = 0;

	UPROPERTY(BlueprintReadOnly)
	int64 Net = 0;

	// Net change across the player's own accounts.
	UPROPERTY(BlueprintReadOnly)
	int64 OwnedMovement = 0;

	UPROPERTY(BlueprintReadOnly)
	TArray<FPuffersLedgerLeg> Lines;

	UPROPERTY(BlueprintReadOnly)
	FName ReversalOf;

	UPROPERTY(BlueprintReadOnly)
	FName ReversedBy;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersLedger
{
	GENERATED_BODY()

	static const FName Pocket;
	static const FName Bank;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TMap<FName, int64> Balances;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersLedgerTransaction> Transactions;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersLedgerHold> Holds;

	// Name of a business's reserve account.
	static FName ReserveAccount(FName BusinessId);

	// Pocket, Bank and Reserve.* belong to the player. Everything else is a counterparty.
	static bool IsOwnedAccount(FName Account);

	// Posts a balanced transaction once. Owned accounts can't go below their active holds.
	EPuffersLedgerResult Post(FName TxId, const TArray<FPuffersLedgerLeg>& Legs, const FString& Memo, const FDateTime& Now);

	// Convenience: move Amount from one account to another as a two-leg transaction.
	EPuffersLedgerResult Transfer(FName TxId, FName From, FName To, int64 Amount, const FString& Memo, const FDateTime& Now);

	// Payment with a fee: Gross leaves From, Fee goes to FeeAccount, the rest to To.
	EPuffersLedgerResult TransferWithFee(FName TxId, FName From, FName To, int64 Gross, FName FeeAccount, int64 Fee, const FString& Memo, const FDateTime& Now);

	EPuffersLedgerResult PlaceHold(FName HoldId, FName Account, int64 Amount, const FString& Memo, const FDateTime& Now);

	// Captures up to the held amount into a transfer to To. The hold closes either way; any remainder is released.
	EPuffersLedgerResult CaptureHold(FName HoldId, FName TxId, FName To, int64 Amount, const FDateTime& Now);

	EPuffersLedgerResult ReleaseHold(FName HoldId, const FDateTime& Now);

	// Posts the mirror of TxId as NewTxId. A transaction can be reversed once.
	EPuffersLedgerResult Reverse(FName TxId, FName NewTxId, const FString& Memo, const FDateTime& Now);

	int64 GetBalance(FName Account) const;
	int64 GetHeld(FName Account) const;
	int64 GetAvailable(FName Account) const { return GetBalance(Account) - GetHeld(Account); }

	// Sum of every owned account.
	int64 GetTotalOwned() const;

	bool HasTransaction(FName TxId) const { return FindTransaction(TxId) != nullptr; }
	const FPuffersLedgerTransaction* FindTransaction(FName TxId) const;
	const FPuffersLedgerHold* FindHold(FName HoldId) const;

	FPuffersReceipt MakeReceipt(FName TxId) const;

	// Owned accounts with a non-zero balance.
	TArray<FName> GetOwnedAccounts() const;

private:
	FPuffersLedgerTransaction* FindTransactionMutable(FName TxId);
	FPuffersLedgerHold* FindHoldMutable(FName HoldId);
};
