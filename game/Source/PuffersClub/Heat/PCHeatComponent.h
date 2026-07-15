#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCHeatComponent.generated.h"

/**
 * Escalation tiers of the original law-enforcement response system.
 * See docs/design/06-combat-and-heat.md.
 */
UENUM(BlueprintType)
enum class EPCHeatTier : uint8
{
	None				UMETA(DisplayName = "None"),
	Suspicion			UMETA(DisplayName = "Suspicion"),
	Detainment			UMETA(DisplayName = "Detainment"),
	ActivePursuit		UMETA(DisplayName = "Active Pursuit"),
	TacticalResponse	UMETA(DisplayName = "Tactical Response"),
	RegionalLockdown	UMETA(DisplayName = "Regional Lockdown"),
	FederalResponse		UMETA(DisplayName = "Federal Response")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHeatTierChanged, EPCHeatTier, OldTier, EPCHeatTier, NewTier);

/**
 * Server-authoritative heat state for one character.
 *
 * Heat is a float score; tiers are thresholds on the score. Crimes add heat
 * (scaled by witnesses, cameras, masks, district, weapon type, org reputation,
 * prior record, and server rules); heat decays over time, faster while unseen.
 * Clients only ever receive the replicated result.
 */
UCLASS(ClassGroup = (PuffersClub), meta = (BlueprintSpawnableComponent))
class PUFFERSCLUB_API UPCHeatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPCHeatComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Server only: report a crime with a base heat value and a witness multiplier. */
	UFUNCTION(BlueprintCallable, Category = "Heat", meta = (BlueprintProtected))
	void ReportCrime(float BaseHeat, float WitnessMultiplier = 1.f);

	/** Server only: whether police currently have line of sight; slows/speeds decay. */
	UFUNCTION(BlueprintCallable, Category = "Heat")
	void SetSeenByPolice(bool bSeen) { bSeenByPolice = bSeen; }

	UFUNCTION(BlueprintPure, Category = "Heat")
	EPCHeatTier GetHeatTier() const { return CurrentTier; }

	UFUNCTION(BlueprintPure, Category = "Heat")
	float GetHeat() const { return Heat; }

	UPROPERTY(BlueprintAssignable, Category = "Heat")
	FOnHeatTierChanged OnHeatTierChanged;

protected:
	/** Heat score thresholds for each tier, ascending. Tunable per server. */
	UPROPERTY(EditDefaultsOnly, Category = "Heat")
	TArray<float> TierThresholds = { 10.f, 30.f, 60.f, 100.f, 160.f, 240.f };

	UPROPERTY(EditDefaultsOnly, Category = "Heat")
	float DecayPerSecondSeen = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Heat")
	float DecayPerSecondUnseen = 1.5f;

	UPROPERTY(ReplicatedUsing = OnRep_Heat)
	float Heat = 0.f;

	UPROPERTY(Replicated)
	EPCHeatTier CurrentTier = EPCHeatTier::None;

	bool bSeenByPolice = false;

	UFUNCTION()
	void OnRep_Heat();

	void RecomputeTier();
};
