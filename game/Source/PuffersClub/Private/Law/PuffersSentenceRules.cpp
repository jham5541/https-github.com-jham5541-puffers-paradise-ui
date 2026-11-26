#include "Law/PuffersSentenceTypes.h"

bool FPuffersSentenceRules::Impose(const FPuffersSentenceDefinition& Definition, FPuffersSentenceState& State, TSet<FName>& ImposedSentences)
{
	if (State.IsServing() || Definition.SentenceId.IsNone() || Definition.BlockMissions.Num() == 0
		|| ImposedSentences.Contains(Definition.SentenceId))
	{
		return false;
	}
	State = FPuffersSentenceState();
	State.SentenceId = Definition.SentenceId;
	State.ImposedBlocks = Definition.BlockMissions.Num();
	ImposedSentences.Add(Definition.SentenceId);
	return true;
}

void FPuffersSentenceRules::AddActiveTime(const FPuffersSentenceDefinition& Definition, FPuffersSentenceState& State, float DeltaSeconds)
{
	if (!State.IsServing() || State.ServedBlocks >= State.ImposedBlocks || DeltaSeconds <= 0.f)
	{
		return;
	}
	const float Step = FMath::Min(DeltaSeconds, MaxStepSeconds);
	State.BlockActiveSeconds = FMath::Min(State.BlockActiveSeconds + Step, FMath::Max(Definition.MinActiveSecondsPerBlock, 0.f));
}

void FPuffersSentenceRules::MarkRoutineDone(FPuffersSentenceState& State)
{
	if (State.IsServing() && State.ServedBlocks < State.ImposedBlocks)
	{
		State.bBlockRoutineDone = true;
	}
}

bool FPuffersSentenceRules::CanCommitBlock(const FPuffersSentenceDefinition& Definition, const FPuffersSentenceState& State)
{
	return State.IsServing() && State.ServedBlocks < State.ImposedBlocks && State.bBlockRoutineDone
		&& State.BlockActiveSeconds >= FMath::Max(Definition.MinActiveSecondsPerBlock, 0.f);
}

FName FPuffersSentenceRules::CommitBlock(const FPuffersSentenceDefinition& Definition, FPuffersSentenceState& State)
{
	if (!CanCommitBlock(Definition, State))
	{
		return NAME_None;
	}
	const FName BlockId = *FString::Printf(TEXT("%s_B%d"), *State.SentenceId.ToString(), State.ServedBlocks + 1);
	if (State.CommittedBlocks.Contains(BlockId))
	{
		return NAME_None;
	}
	State.CommittedBlocks.Add(BlockId);
	State.ServedBlocks = FMath::Clamp(State.ServedBlocks + 1, 0, State.ImposedBlocks);
	State.BlockActiveSeconds = 0.f;
	State.bBlockRoutineDone = false;
	return BlockId;
}

bool FPuffersSentenceRules::IsReleaseDue(const FPuffersSentenceState& State)
{
	return State.IsServing() && State.ImposedBlocks > 0 && State.ServedBlocks >= State.ImposedBlocks
		&& State.CommittedBlocks.Num() >= State.ImposedBlocks;
}

bool FPuffersSentenceRules::Release(FPuffersSentenceState& State)
{
	if (!IsReleaseDue(State))
	{
		return false;
	}
	State.bReleased = true;
	return true;
}

FName FPuffersSentenceRules::GetBlockMission(const FPuffersSentenceDefinition& Definition, const FPuffersSentenceState& State)
{
	if (!State.IsServing() || !Definition.BlockMissions.IsValidIndex(State.ServedBlocks))
	{
		return NAME_None;
	}
	return Definition.BlockMissions[State.ServedBlocks];
}

void FPuffersSentenceRules::GetDayAndBlock(const FPuffersSentenceDefinition& Definition, const FPuffersSentenceState& State, int32& OutDay, int32& OutBlock)
{
	const int32 PerDay = FMath::Max(Definition.BlocksPerDay, 1);
	const int32 Index = FMath::Clamp(State.ServedBlocks, 0, FMath::Max(State.ImposedBlocks - 1, 0));
	OutDay = Index / PerDay + 1;
	OutBlock = Index % PerDay + 1;
}
