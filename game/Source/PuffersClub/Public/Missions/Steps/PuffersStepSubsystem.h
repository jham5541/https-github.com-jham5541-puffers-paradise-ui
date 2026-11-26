// Turns the active mission stage into gameplay. Listens to the mission subsystem; when a stage starts it spawns one
// step runner per objective (chosen by Content/Data/Steps/StepRules.json), and removes them when the stage ends.
// It also owns the step HUD overlay, the signature abilities on Z (Mac Vision, Trendsetter, Wrong Turn...), the
// choice-flag reactions (Content/Data/Steps/Reactions.json), and auto-switching to the Puffer a step names.
//
// No mission data or mission code changes are needed: the subsystem reads Type and Description from the JSON
// the mission subsystem already loaded.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Missions/PuffersMissionTypes.h"
#include "Missions/Steps/PuffersStepRules.h"
#include "Missions/Steps/PuffersStepTypes.h"
#include "PuffersStepSubsystem.generated.h"

class APlayerController;
class APuffersStepActor;
class UCanvas;
class UInputAction;
class UInputMappingContext;
class UPuffersMissionSubsystem;

UCLASS()
class PUFFERSCLUB_API UPuffersStepSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static const FName EventStepStarted;
	static const FName EventStepFailed;
	static const FName EventStepSucceeded;
	static const FName EventAbilityUsed;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// Re-reads StepRules.json and Reactions.json and rebuilds the current stage's runners.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Steps")
	bool ReloadStepData();

	// The gameplay an objective resolves to (for tools, tests and the debug overlay).
	FPuffersStepParams ResolveObjective(FName MissionId, const FPuffersObjectiveDefinition& Objective) const;

	const FPuffersStepCatalog& GetCatalog() const { return Catalog; }

	// Called by runners.
	UPuffersMissionSubsystem* GetMissions() const;
	bool CanBeginChoice(const APuffersStepActor* Runner) const;
	int32 GetGeneration() const { return Generation; }
	void ShowToast(const FString& Text, bool bAlert = false);
	bool HasAbility(FName Ability) const;
	bool IsAbilityActive(FName Ability) const;
	void GrantTemporaryAbility(FName Ability);
	void RevokeTemporaryAbility(FName Ability);
	// Known Puffer ids from the roster, used to read names out of descriptions.
	const TArray<FName>& GetKnownCharacters() const { return KnownCharacters; }
	FString DisplayNameFor(FName CharacterId) const;
	// Reaction lines that open this objective's conversation.
	TArray<FString> GetConversationReactions(FName MissionId, FName ObjectiveId) const;
	// Tries to play as this Puffer. Falls back to relabelling the pawn when the body isn't imported yet.
	bool SwitchToCharacter(FName CharacterId);

	// The stand-in for an objective in the level: its prop, its NPC (TalkObjectiveIds) or its volume.
	AActor* FindAnchor(FName ObjectiveId, FVector& OutLocation) const;

	// Mac Vision etc. Returns the ability that fired, or None.
	FName ActivateContextAbility();

private:
	UFUNCTION()
	void HandleStageChanged(FName MissionId, int32 Stage);

	UFUNCTION()
	void HandleMissionStateChanged(FName MissionId, EPuffersMissionState NewState);

	UFUNCTION()
	void HandleObjectiveUpdated(FName MissionId, FName ObjectiveId, int32 Progress, int32 Required);

	UFUNCTION()
	void HandleGameEvent(FName EventName, FName Context);

	void Rebuild(bool bFullReset);
	void DestroyRunner(APuffersStepActor* Runner);
	void DestroyAll();
	TSubclassOf<APuffersStepActor> RunnerClassFor(FName Gameplay) const;
	float ReactionDifficulty(FName MissionId) const;
	void ShowMissionStartReactions(FName MissionId);
	void LoadKnownCharacters();
	void UpdateMenuBlock();
	void DrawOverlay(UCanvas* Canvas, APlayerController* PC);
	void DrawPanel(UCanvas* Canvas, const FPuffersStepOverlay& Overlay, float CenterX, float& Bottom);

	FPuffersStepCatalog Catalog;
	bool bLoaded = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<APuffersStepActor>> Runners;

	int32 Generation = 0;
	bool bDirty = false;
	bool bFullResetPending = false;
	bool bBound = false;
	FName LastActiveMission;
	FDelegateHandle DrawHandle;

	TArray<FName> KnownCharacters;
	TMap<FName, FString> CharacterNames;

	// Abilities
	TSet<FName> TemporaryAbilities;
	TMap<FName, double> AbilityActiveUntil;
	TMap<FName, double> AbilityReadyAt;

	// Toasts
	FString Toast;
	bool bToastAlert = false;
	double ToastUntil = 0.0;
	TArray<FString> StoryCard;
	double StoryCardUntil = 0.0;

	// Blocks 1-4 / d-pad from wave, sit and minimap while a menu is open.
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MenuBlockContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MenuBlockAction;

	bool bMenuBlockApplied = false;
};
