#include "Heat/PCHeatComponent.h"
#include "Net/UnrealNetwork.h"
#include "PuffersClub.h"

UPCHeatComponent::UPCHeatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UPCHeatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPCHeatComponent, Heat);
	DOREPLIFETIME(UPCHeatComponent, CurrentTier);
}

void UPCHeatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Decay only runs with authority; clients receive replicated state.
	if (GetOwnerRole() == ROLE_Authority && Heat > 0.f)
	{
		const float Decay = bSeenByPolice ? DecayPerSecondSeen : DecayPerSecondUnseen;
		Heat = FMath::Max(0.f, Heat - Decay * DeltaTime);
		RecomputeTier();
	}
}

void UPCHeatComponent::ReportCrime(float BaseHeat, float WitnessMultiplier)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogPuffersClub, Warning, TEXT("ReportCrime called without authority; ignored."));
		return;
	}

	Heat += FMath::Max(0.f, BaseHeat) * FMath::Max(0.f, WitnessMultiplier);
	RecomputeTier();
}

void UPCHeatComponent::RecomputeTier()
{
	EPCHeatTier NewTier = EPCHeatTier::None;
	for (int32 i = 0; i < TierThresholds.Num(); ++i)
	{
		if (Heat >= TierThresholds[i])
		{
			NewTier = static_cast<EPCHeatTier>(i + 1);
		}
	}

	if (NewTier != CurrentTier)
	{
		const EPCHeatTier OldTier = CurrentTier;
		CurrentTier = NewTier;
		OnHeatTierChanged.Broadcast(OldTier, NewTier);
	}
}

void UPCHeatComponent::OnRep_Heat()
{
	// Client-side: tier replicates separately; nothing to recompute here.
}
