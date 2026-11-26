// Runs story sentences (see PuffersSentenceTypes.h): imposes them when an authored case ends in a sentence,
// keeps the player in the facility, plays each block's routine mission, counts only active unpaused time,
// commits each block once and releases exactly once when the full term is served.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Law/PuffersSentenceTypes.h"
#include "PuffersSentenceSubsystem.generated.h"

UCLASS()
class PUFFERSCLUB_API UPuffersSentenceSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static const FString SaveSlotName;
	static const FName EventSentenceImposed;   // Context: sentence id
	static const FName EventBlockServed;       // Context: committed block id. Businesses run one managed tick per block.
	static const FName EventSentenceReleased;  // Context: sentence id

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override;

	UFUNCTION(BlueprintCallable, Category = "Puffers|Sentence")
	int32 ReloadSentenceDefinitions();

	UFUNCTION(BlueprintPure, Category = "Puffers|Sentence")
	bool IsServing() const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Sentence")
	int32 GetServedBlocks() const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Sentence")
	int32 GetImposedBlocks() const;

	// Active seconds still needed in the current block (the routine mission is also required).
	UFUNCTION(BlueprintPure, Category = "Puffers|Sentence")
	float GetBlockSecondsRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Sentence")
	bool IsBlockRoutineDone() const;

	// One-line status for the HUD, e.g. "Sentence: day 1, block 2 of 3 · 4 of 9 blocks left · 1:12 to go". Empty when free.
	UFUNCTION(BlueprintPure, Category = "Puffers|Sentence")
	FString GetStatusLine() const;

	// Imposes a sentence by id. Normally called from the trigger mission; exposed for tests and debug tools.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Sentence")
	bool ImposeSentence(FName SentenceId);

	// Generic county sentences (Rev 12): the career subsystem builds a definition from the imposed years and the
	// custody scale, and registers it here. Generated definitions survive ReloadSentenceDefinitions and are
	// re-registered from the career save on startup, so a reload resumes the same term.
	void RegisterGeneratedDefinition(const FPuffersSentenceDefinition& Definition);

	bool SaveNow();

private:
	UFUNCTION()
	void OnGameEvent(FName EventName, FName Context);

	const FPuffersSentenceDefinition* GetActiveDefinition() const;
	void TryAdvance();
	void EnsureBlockMission();
	void MovePlayerToFacility(bool bRelease);
	void KeepPlayerInside();

	TMap<FName, FPuffersSentenceDefinition> Definitions;
	TMap<FName, FPuffersSentenceDefinition> GeneratedDefinitions;

	UPROPERTY()
	TObjectPtr<UPuffersSentenceSaveGame> Save;

	TWeakObjectPtr<UWorld> PlacedInWorld;
	float SecondsSinceSave = 0.f;
	float SecondsSinceMissionCheck = 0.f;
	bool bStartingBlockMission = false;
};
