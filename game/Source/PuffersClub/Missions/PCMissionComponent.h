#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCMissionComponent.generated.h"

class UPCMissionDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionStarted, const UPCMissionDefinition*, Mission);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnObjectiveCompleted, const UPCMissionDefinition*, Mission, FName, ObjectiveId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionCompleted, const UPCMissionDefinition*, Mission);

/**
 * Runtime mission progress for one player. Attach to the PlayerState so it
 * survives pawn death/respawn. All mutations are server-side; clients see
 * replicated progress and delegates.
 */
UCLASS(ClassGroup = (PuffersClub), meta = (BlueprintSpawnableComponent))
class PUFFERSCLUB_API UPCMissionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPCMissionComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server only. Fails if a mission is already active. */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	bool StartMission(UPCMissionDefinition* Mission);

	/** Server only. Marks an objective done; completes the mission when all
	 *  required objectives are done and pays the reward to the wallet. */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	bool CompleteObjective(FName ObjectiveId);

	/** Server only. Abandons the active mission without reward. */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	void AbandonMission();

	UFUNCTION(BlueprintPure, Category = "Mission")
	const UPCMissionDefinition* GetActiveMission() const { return ActiveMission; }

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool IsObjectiveCompleted(FName ObjectiveId) const { return CompletedObjectives.Contains(ObjectiveId); }

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionStarted OnMissionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnObjectiveCompleted OnObjectiveCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionCompleted OnMissionCompleted;

protected:
	UPROPERTY(Replicated)
	TObjectPtr<UPCMissionDefinition> ActiveMission;

	UPROPERTY(Replicated)
	TArray<FName> CompletedObjectives;

	bool HasAuthority() const;
	void FinishMissionIfComplete();
};
