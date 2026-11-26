// Story sentences: the full-term custody chapter from the game handoff (Rev 7, "Custody chapter and release rules").
//
// Two tiers of custody exist side by side:
//  * An ordinary open-world bust (UPuffersLawSubsystem): nearest county prison, leave through the work mission or bail.
//  * A story sentence (this file): imposed only by an authored case. The whole term is served in blocks of routine,
//    and nothing (bail, money, entitlements, pausing, quitting, the device clock) shortens it.
//
// The rules below are pure so they can be unit tested without a world.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "PuffersSentenceTypes.generated.h"

// Authored in Content/Data/Law/Sentences.json.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersSentenceDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName SentenceId;

	// The sentence is imposed when this mission completes...
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName TriggerMissionId;

	// ...and this world flag is set (the "sentenced" outcome of the case). None = always.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName RequiresWorldFlag;

	// Facility the term is served in (an APuffersLawFacility FacilityId).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName FacilityId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 BlocksPerDay = 3;

	// Mission to play during each block, in order. Its length is the term in blocks.
	// A mission id used more than once must be replayable.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> BlockMissions;

	// Minimum active (unpaused, in-game) seconds per block. A block advances only when both this and its mission are done.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MinActiveSecondsPerBlock = 100.f;

	// World flag set on release.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ReleaseWorldFlag;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersSentenceState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SentenceId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ImposedBlocks = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 ServedBlocks = 0;

	// Active seconds accrued in the current block (clamped to the block minimum).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	float BlockActiveSeconds = 0.f;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bBlockRoutineDone = false;

	// Committed block ids ("<SentenceId>_B<n>"), so a crash or retry can never apply a block twice.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TSet<FName> CommittedBlocks;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bReleased = false;

	bool IsServing() const { return !SentenceId.IsNone() && !bReleased; }
};

UCLASS()
class PUFFERSCLUB_API UPuffersSentenceSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	FPuffersSentenceState Active;

	// Sentences ever imposed, so the same case can't sentence twice.
	UPROPERTY(SaveGame)
	TSet<FName> ImposedSentences;
};

struct PUFFERSCLUB_API FPuffersSentenceRules
{
	// Starts the term. Fails if a sentence is already being served, the definition is empty, or it was imposed before.
	static bool Impose(const FPuffersSentenceDefinition& Definition, FPuffersSentenceState& State, TSet<FName>& ImposedSentences);

	// Longest single step of active time that counts. Hitches and clock jumps can't fast-forward the term.
	static constexpr float MaxStepSeconds = 0.25f;

	// Adds active time. Only call while unpaused and in the game world.
	static void AddActiveTime(const FPuffersSentenceDefinition& Definition, FPuffersSentenceState& State, float DeltaSeconds);

	static void MarkRoutineDone(FPuffersSentenceState& State);

	static bool CanCommitBlock(const FPuffersSentenceDefinition& Definition, const FPuffersSentenceState& State);

	// Commits the current block once. Returns the committed block id, or None if nothing was committed.
	static FName CommitBlock(const FPuffersSentenceDefinition& Definition, FPuffersSentenceState& State);

	// True when every imposed block is served and release hasn't happened yet.
	static bool IsReleaseDue(const FPuffersSentenceState& State);

	// Marks release once. Returns false if it already happened or isn't due.
	static bool Release(FPuffersSentenceState& State);

	// Mission for the current block, or None.
	static FName GetBlockMission(const FPuffersSentenceDefinition& Definition, const FPuffersSentenceState& State);

	// "Day 2, block 1" style position (both 1-based).
	static void GetDayAndBlock(const FPuffersSentenceDefinition& Definition, const FPuffersSentenceState& State, int32& OutDay, int32& OutBlock);
};
