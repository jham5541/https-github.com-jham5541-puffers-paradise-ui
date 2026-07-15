#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCMissionDefinition.generated.h"

USTRUCT(BlueprintType)
struct FPCMissionObjective
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective")
	FName Id;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective")
	FText Description;

	/** Optional objectives don't block mission completion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective")
	bool bOptional = false;
};

/**
 * Static mission data (main, side, and later dynamic templates).
 * Authored as data assets under Content/Missions/; runtime progress lives in
 * UPCMissionComponent. Player-created missions (Phase 6) compile to this
 * same shape after validation.
 */
UCLASS(BlueprintType)
class PUFFERSCLUB_API UPCMissionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	FText Title;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TArray<FPCMissionObjective> Objectives;

	/** Paid to the wallet on completion; validated against the economy later. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	int64 RewardCents = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission", meta = (ClampMin = "1"))
	int32 RequiredPlayers = 1;
};
