#include "Missions/PCMissionComponent.h"

#include "Core/PCPlayerState.h"
#include "Missions/PCMissionDefinition.h"
#include "Net/UnrealNetwork.h"
#include "PuffersClub.h"

UPCMissionComponent::UPCMissionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPCMissionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPCMissionComponent, ActiveMission);
	DOREPLIFETIME(UPCMissionComponent, CompletedObjectives);
}

bool UPCMissionComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

bool UPCMissionComponent::StartMission(UPCMissionDefinition* Mission)
{
	if (!HasAuthority() || !Mission || ActiveMission)
	{
		return false;
	}

	ActiveMission = Mission;
	CompletedObjectives.Reset();
	OnMissionStarted.Broadcast(ActiveMission);
	UE_LOG(LogPuffersClub, Log, TEXT("Mission started: %s"), *Mission->Title.ToString());
	return true;
}

bool UPCMissionComponent::CompleteObjective(FName ObjectiveId)
{
	if (!HasAuthority() || !ActiveMission || CompletedObjectives.Contains(ObjectiveId))
	{
		return false;
	}

	const bool bKnown = ActiveMission->Objectives.ContainsByPredicate(
		[&ObjectiveId](const FPCMissionObjective& O) { return O.Id == ObjectiveId; });
	if (!bKnown)
	{
		return false;
	}

	CompletedObjectives.Add(ObjectiveId);
	OnObjectiveCompleted.Broadcast(ActiveMission, ObjectiveId);
	FinishMissionIfComplete();
	return true;
}

void UPCMissionComponent::FinishMissionIfComplete()
{
	for (const FPCMissionObjective& Objective : ActiveMission->Objectives)
	{
		if (!Objective.bOptional && !CompletedObjectives.Contains(Objective.Id))
		{
			return;
		}
	}

	UPCMissionDefinition* Finished = ActiveMission;
	ActiveMission = nullptr;
	CompletedObjectives.Reset();

	if (APCPlayerState* PS = Cast<APCPlayerState>(GetOwner()))
	{
		PS->AddFunds(Finished->RewardCents, TEXT("mission_reward"));
	}

	OnMissionCompleted.Broadcast(Finished);
	UE_LOG(LogPuffersClub, Log, TEXT("Mission completed: %s"), *Finished->Title.ToString());
}

void UPCMissionComponent::AbandonMission()
{
	if (HasAuthority() && ActiveMission)
	{
		UE_LOG(LogPuffersClub, Log, TEXT("Mission abandoned: %s"), *ActiveMission->Title.ToString());
		ActiveMission = nullptr;
		CompletedObjectives.Reset();
	}
}
