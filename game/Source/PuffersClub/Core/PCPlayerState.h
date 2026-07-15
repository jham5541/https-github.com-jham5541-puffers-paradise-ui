#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "PCPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBalanceChanged, int64, NewBalanceCents);

/**
 * Per-player persistent state mirror. The wallet here is the replicated,
 * server-authoritative in-session copy; the backend ledger
 * (backend/src/routes/economy.ts) is the source of truth between sessions.
 */
UCLASS()
class PUFFERSCLUB_API APCPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Economy")
	int64 GetBalanceCents() const { return BalanceCents; }

	/** Server only. Credits the wallet (mission rewards, job pay, sales). */
	void AddFunds(int64 AmountCents, const FString& Kind);

	/** Server only. Debits the wallet if funds are sufficient. Returns success. */
	bool TrySpend(int64 AmountCents, const FString& Kind);

	UPROPERTY(BlueprintAssignable, Category = "Economy")
	FOnBalanceChanged OnBalanceChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Balance)
	int64 BalanceCents = 0;

	UFUNCTION()
	void OnRep_Balance();
};
