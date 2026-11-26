#include "Law/PuffersSentenceSubsystem.h"
#include "Law/PuffersLawFacility.h"
#include "Components/BoxComponent.h"
#include "Missions/PuffersMissionSubsystem.h"
#include "PuffersClub.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

const FString UPuffersSentenceSubsystem::SaveSlotName = TEXT("PuffersSentence0");
const FName UPuffersSentenceSubsystem::EventSentenceImposed = TEXT("SentenceImposed");
const FName UPuffersSentenceSubsystem::EventBlockServed = TEXT("SentenceBlockServed");
const FName UPuffersSentenceSubsystem::EventSentenceReleased = TEXT("SentenceReleased");

void UPuffersSentenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UPuffersMissionSubsystem>();
	Super::Initialize(Collection);

	ReloadSentenceDefinitions();
	Save = Cast<UPuffersSentenceSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	if (!Save)
	{
		Save = Cast<UPuffersSentenceSaveGame>(UGameplayStatics::CreateSaveGameObject(UPuffersSentenceSaveGame::StaticClass()));
	}

	if (UPuffersMissionSubsystem* Missions = GetGameInstance()->GetSubsystem<UPuffersMissionSubsystem>())
	{
		Missions->OnGameEvent.AddDynamic(this, &UPuffersSentenceSubsystem::OnGameEvent);
	}
}

void UPuffersSentenceSubsystem::Deinitialize()
{
	SaveNow();
	Super::Deinitialize();
}

int32 UPuffersSentenceSubsystem::ReloadSentenceDefinitions()
{
	Definitions.Reset();
	FString Json;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Law/Sentences.json");
	TArray<FPuffersSentenceDefinition> Loaded;
	if (FFileHelper::LoadFileToString(Json, *Path) && FJsonObjectConverter::JsonArrayStringToUStruct(Json, &Loaded, 0, 0))
	{
		for (const FPuffersSentenceDefinition& Definition : Loaded)
		{
			if (!Definition.SentenceId.IsNone() && Definition.BlockMissions.Num() > 0)
			{
				Definitions.Add(Definition.SentenceId, Definition);
			}
		}
	}
	for (const TPair<FName, FPuffersSentenceDefinition>& Pair : GeneratedDefinitions)
	{
		Definitions.Add(Pair.Key, Pair.Value);
	}
	UE_LOG(LogPuffers, Log, TEXT("Loaded %d sentence definitions"), Definitions.Num());
	return Definitions.Num();
}

void UPuffersSentenceSubsystem::RegisterGeneratedDefinition(const FPuffersSentenceDefinition& Definition)
{
	if (Definition.SentenceId.IsNone() || Definition.BlockMissions.Num() == 0)
	{
		return;
	}
	GeneratedDefinitions.Add(Definition.SentenceId, Definition);
	Definitions.Add(Definition.SentenceId, Definition);
}

TStatId UPuffersSentenceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPuffersSentenceSubsystem, STATGROUP_Tickables);
}

ETickableTickType UPuffersSentenceSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UPuffersSentenceSubsystem::IsTickable() const
{
	return !IsTemplate() && Save && Save->Active.IsServing();
}

const FPuffersSentenceDefinition* UPuffersSentenceSubsystem::GetActiveDefinition() const
{
	return Save ? Definitions.Find(Save->Active.SentenceId) : nullptr;
}

bool UPuffersSentenceSubsystem::IsServing() const
{
	return Save && Save->Active.IsServing();
}

int32 UPuffersSentenceSubsystem::GetServedBlocks() const
{
	return Save ? Save->Active.ServedBlocks : 0;
}

int32 UPuffersSentenceSubsystem::GetImposedBlocks() const
{
	return Save ? Save->Active.ImposedBlocks : 0;
}

bool UPuffersSentenceSubsystem::IsBlockRoutineDone() const
{
	return Save && Save->Active.bBlockRoutineDone;
}

float UPuffersSentenceSubsystem::GetBlockSecondsRemaining() const
{
	const FPuffersSentenceDefinition* Definition = GetActiveDefinition();
	if (!Definition || !IsServing())
	{
		return 0.f;
	}
	return FMath::Max(0.f, Definition->MinActiveSecondsPerBlock - Save->Active.BlockActiveSeconds);
}

FString UPuffersSentenceSubsystem::GetStatusLine() const
{
	const FPuffersSentenceDefinition* Definition = GetActiveDefinition();
	if (!Definition || !IsServing())
	{
		return FString();
	}
	int32 Day = 1, Block = 1;
	FPuffersSentenceRules::GetDayAndBlock(*Definition, Save->Active, Day, Block);
	const int32 Left = Save->Active.ImposedBlocks - Save->Active.ServedBlocks;
	const int32 Seconds = FMath::CeilToInt(GetBlockSecondsRemaining());
	return FString::Printf(TEXT("Sentence: day %d, block %d of %d  |  %d of %d blocks left  |  %s"),
		Day, Block, FMath::Max(Definition->BlocksPerDay, 1), Left, Save->Active.ImposedBlocks,
		Seconds > 0 ? *FString::Printf(TEXT("%d:%02d of block time to go"), Seconds / 60, Seconds % 60)
			: (Save->Active.bBlockRoutineDone ? TEXT("block complete") : TEXT("finish today's routine")));
}

bool UPuffersSentenceSubsystem::ImposeSentence(FName SentenceId)
{
	const FPuffersSentenceDefinition* Definition = Definitions.Find(SentenceId);
	if (!Definition || !Save || !FPuffersSentenceRules::Impose(*Definition, Save->Active, Save->ImposedSentences))
	{
		return false;
	}
	SaveNow();
	UE_LOG(LogPuffers, Log, TEXT("Sentence %s imposed: %d blocks"), *SentenceId.ToString(), Save->Active.ImposedBlocks);

	PlacedInWorld.Reset();
	MovePlayerToFacility(false);
	if (UPuffersMissionSubsystem* Missions = GetGameInstance()->GetSubsystem<UPuffersMissionSubsystem>())
	{
		Missions->BroadcastGameEvent(EventSentenceImposed, SentenceId);
	}
	EnsureBlockMission();
	return true;
}

void UPuffersSentenceSubsystem::Tick(float DeltaTime)
{
	const FPuffersSentenceDefinition* Definition = GetActiveDefinition();
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!Definition || !World || !World->IsGameWorld() || UGameplayStatics::IsGamePaused(World)
		|| !UGameplayStatics::GetPlayerPawn(World, 0))
	{
		return;
	}

	// After a load or a map change, put the player back inside before any time counts.
	if (PlacedInWorld.Get() != World)
	{
		MovePlayerToFacility(false);
		return;
	}

	FPuffersSentenceRules::AddActiveTime(*Definition, Save->Active, DeltaTime);

	SecondsSinceMissionCheck += DeltaTime;
	if (SecondsSinceMissionCheck >= 2.f)
	{
		SecondsSinceMissionCheck = 0.f;
		KeepPlayerInside();
		EnsureBlockMission();
	}

	TryAdvance();

	SecondsSinceSave += DeltaTime;
	if (SecondsSinceSave >= 10.f)
	{
		SaveNow();
	}
}

void UPuffersSentenceSubsystem::TryAdvance()
{
	const FPuffersSentenceDefinition* Definition = GetActiveDefinition();
	if (!Definition)
	{
		return;
	}
	UPuffersMissionSubsystem* Missions = GetGameInstance()->GetSubsystem<UPuffersMissionSubsystem>();

	const FName BlockId = FPuffersSentenceRules::CommitBlock(*Definition, Save->Active);
	if (BlockId.IsNone())
	{
		return;
	}
	SaveNow();
	UE_LOG(LogPuffers, Log, TEXT("Sentence block %s served (%d/%d)"), *BlockId.ToString(), Save->Active.ServedBlocks, Save->Active.ImposedBlocks);
	if (Missions)
	{
		Missions->BroadcastGameEvent(EventBlockServed, BlockId);
	}

	if (FPuffersSentenceRules::Release(Save->Active))
	{
		const FName SentenceId = Save->Active.SentenceId;
		if (Missions && !Definition->ReleaseWorldFlag.IsNone())
		{
			Missions->SetWorldFlag(Definition->ReleaseWorldFlag, true);
			Missions->SaveNow();
		}
		SaveNow();
		MovePlayerToFacility(true);
		UE_LOG(LogPuffers, Log, TEXT("Sentence %s served in full. Released."), *SentenceId.ToString());
		if (Missions)
		{
			Missions->BroadcastGameEvent(EventSentenceReleased, SentenceId);
		}
		return;
	}
	EnsureBlockMission();
}

void UPuffersSentenceSubsystem::EnsureBlockMission()
{
	const FPuffersSentenceDefinition* Definition = GetActiveDefinition();
	UPuffersMissionSubsystem* Missions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UPuffersMissionSubsystem>() : nullptr;
	if (!Definition || !Missions || !IsServing() || Save->Active.bBlockRoutineDone)
	{
		return;
	}
	const FName BlockMission = FPuffersSentenceRules::GetBlockMission(*Definition, Save->Active);
	if (BlockMission.IsNone() || Missions->GetActiveMissionId() == BlockMission)
	{
		return;
	}

	// Inside, the only thing to do is the day's routine.
	if (!Missions->GetActiveMissionId().IsNone())
	{
		Missions->AbandonActiveMission();
	}
	FString Reason;
	if (Missions->CanStartMission(BlockMission, Reason))
	{
		bStartingBlockMission = true;
		Missions->StartMission(BlockMission);
		bStartingBlockMission = false;
	}
	else if (Missions->GetMissionState(BlockMission) == EPuffersMissionState::Completed)
	{
		// A one-off block mission finished before a crash could commit its block: count the routine as done.
		FPuffersSentenceRules::MarkRoutineDone(Save->Active);
		SaveNow();
	}
	else
	{
		UE_LOG(LogPuffers, Warning, TEXT("Sentence block mission %s can't start: %s"), *BlockMission.ToString(), *Reason);
	}
}

void UPuffersSentenceSubsystem::OnGameEvent(FName EventName, FName Context)
{
	UPuffersMissionSubsystem* Missions = GetGameInstance()->GetSubsystem<UPuffersMissionSubsystem>();
	if (!Save || !Missions)
	{
		return;
	}

	if (IsServing())
	{
		const FPuffersSentenceDefinition* Definition = GetActiveDefinition();
		const FName BlockMission = Definition ? FPuffersSentenceRules::GetBlockMission(*Definition, Save->Active) : NAME_None;
		if (EventName == UPuffersMissionSubsystem::EventMissionCompleted && Context == BlockMission)
		{
			FPuffersSentenceRules::MarkRoutineDone(Save->Active);
			SaveNow();
			TryAdvance();
		}
		else if (EventName == UPuffersMissionSubsystem::EventMissionStarted && Context != BlockMission && !bStartingBlockMission)
		{
			// No side jobs from inside. The block mission restarts on the next check.
			Missions->AbandonActiveMission();
		}
		return;
	}

	if (EventName == UPuffersMissionSubsystem::EventMissionCompleted)
	{
		for (const TPair<FName, FPuffersSentenceDefinition>& Pair : Definitions)
		{
			const FPuffersSentenceDefinition& Definition = Pair.Value;
			if (Definition.TriggerMissionId == Context
				&& (Definition.RequiresWorldFlag.IsNone() || Missions->HasWorldFlag(Definition.RequiresWorldFlag)))
			{
				ImposeSentence(Definition.SentenceId);
				return;
			}
		}
	}
}

void UPuffersSentenceSubsystem::KeepPlayerInside()
{
	const FPuffersSentenceDefinition* Definition = GetActiveDefinition();
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!Definition || !Player)
	{
		return;
	}
	for (TActorIterator<APuffersLawFacility> It(World); It; ++It)
	{
		if (It->FacilityId == Definition->FacilityId && It->Grounds)
		{
			const FVector Local = It->Grounds->GetComponentTransform().InverseTransformPosition(Player->GetActorLocation());
			const FVector Extent = It->Grounds->GetUnscaledBoxExtent();
			if (FMath::Abs(Local.X) > Extent.X || FMath::Abs(Local.Y) > Extent.Y || FMath::Abs(Local.Z) > Extent.Z)
			{
				UE_LOG(LogPuffers, Log, TEXT("Left the facility mid-sentence; returned to the cell block"));
				MovePlayerToFacility(false);
			}
			return;
		}
	}
}

void UPuffersSentenceSubsystem::MovePlayerToFacility(bool bRelease)
{
	const FPuffersSentenceDefinition* Definition = GetActiveDefinition();
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!Definition || !Player)
	{
		return;
	}
	for (TActorIterator<APuffersLawFacility> It(World); It; ++It)
	{
		if (It->FacilityId == Definition->FacilityId)
		{
			const FTransform Where = bRelease ? It->GetReleasePoint() : It->GetInmateSpawn();
			Player->TeleportTo(Where.GetLocation(), Where.Rotator());
			if (AController* Controller = Player->GetController())
			{
				Controller->SetControlRotation(Where.Rotator());
			}
			break;
		}
	}
	// Counted as placed even if this map has no such facility, so a missing facility never blocks the term.
	PlacedInWorld = World;
}

bool UPuffersSentenceSubsystem::SaveNow()
{
	SecondsSinceSave = 0.f;
	return Save && GetGameInstance() && UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, 0);
}
