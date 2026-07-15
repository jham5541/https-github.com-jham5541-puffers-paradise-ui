#include "Core/PCPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "PuffersClub.h"

void APCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APCPlayerState, BalanceCents);
}

void APCPlayerState::AddFunds(int64 AmountCents, const FString& Kind)
{
	if (!HasAuthority() || AmountCents <= 0)
	{
		return;
	}

	BalanceCents += AmountCents;
	UE_LOG(LogPuffersClub, Log, TEXT("Wallet credit %lld (%s) -> %lld"), AmountCents, *Kind, BalanceCents);
	// Phase 4: also POST /v1/economy/transfer so the ledger records it.
	OnBalanceChanged.Broadcast(BalanceCents);
}

bool APCPlayerState::TrySpend(int64 AmountCents, const FString& Kind)
{
	if (!HasAuthority() || AmountCents <= 0 || BalanceCents < AmountCents)
	{
		return false;
	}

	BalanceCents -= AmountCents;
	UE_LOG(LogPuffersClub, Log, TEXT("Wallet debit %lld (%s) -> %lld"), AmountCents, *Kind, BalanceCents);
	// Phase 4: also POST /v1/economy/transfer so the ledger records it.
	OnBalanceChanged.Broadcast(BalanceCents);
	return true;
}

void APCPlayerState::OnRep_Balance()
{
	OnBalanceChanged.Broadcast(BalanceCents);
}
