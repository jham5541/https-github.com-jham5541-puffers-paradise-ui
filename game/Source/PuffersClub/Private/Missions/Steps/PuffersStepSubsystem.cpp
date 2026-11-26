#include "Missions/Steps/PuffersStepSubsystem.h"
#include "Missions/Steps/PuffersStepActor.h"
#include "Missions/PuffersMissionRules.h"
#include "Missions/PuffersMissionSubsystem.h"
#include "Missions/PuffersSaveGame.h"
#include "AI/PuffersNPCCharacter.h"
#include "Characters/PuffersRosterSubsystem.h"
#include "Player/PuffersPlayerCharacter.h"
#include "World/PuffersInteractableComponent.h"
#include "World/PuffersWorldActors.h"
#include "PuffersClub.h"
#include "CanvasItem.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Stats/Stats.h"

const FName UPuffersStepSubsystem::EventStepStarted = TEXT("StepStarted");
const FName UPuffersStepSubsystem::EventStepFailed = TEXT("StepFailed");
const FName UPuffersStepSubsystem::EventStepSucceeded = TEXT("StepSucceeded");
const FName UPuffersStepSubsystem::EventAbilityUsed = TEXT("AbilityUsed");

namespace
{
	const FLinearColor StepGold(1.f, 0.77f, 0.25f);
	const FLinearColor StepText(0.95f, 0.95f, 0.95f);
	const FLinearColor StepMuted(0.65f, 0.65f, 0.65f);
	const FLinearColor StepAlert(1.f, 0.3f, 0.25f);
	const FLinearColor StepGood(0.45f, 0.85f, 0.5f);

	struct FAbilityInfo
	{
		const TCHAR* Name;
		const TCHAR* Label;
		float Duration;
		float Cooldown;
	};

	// Signature abilities that fire on Z. GoViral and RouteMastery are passive (runners read HasAbility).
	const FAbilityInfo ActiveAbilities[] = {
		{ TEXT("MacVision"), TEXT("Mac Vision"), 8.f, 20.f },
		{ TEXT("Trendsetter"), TEXT("Trendsetter"), 8.f, 30.f },
		{ TEXT("WrongTurn"), TEXT("Wrong Turn"), 1.f, 25.f },
		{ TEXT("TalkThatShit"), TEXT("Talk That Shit"), 1.f, 20.f },
	};

	const FAbilityInfo* FindAbility(FName Name)
	{
		for (const FAbilityInfo& Info : ActiveAbilities)
		{
			if (Name == Info.Name)
			{
				return &Info;
			}
		}
		return nullptr;
	}

	void DrawStepBox(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	float DrawStepText(UCanvas* Canvas, const FString& S, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bCentered = false, bool bBackdrop = true)
	{
		UFont* Font = GEngine->GetMediumFont();
		float W = 0.f, H = 0.f;
		Canvas->TextSize(Font, S, W, H, Scale, Scale);
		const float Left = bCentered ? X - W * 0.5f : X;
		if (bBackdrop)
		{
			DrawStepBox(Canvas, Left - 8.f, Y - 2.f, W + 16.f, H + 4.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		}
		Canvas->SetDrawColor(Color.ToFColor(true));
		Canvas->DrawText(Font, S, Left, Y, Scale, Scale);
		return H;
	}

	void DrawStepMeter(UCanvas* Canvas, float X, float Y, float W, float Fill, const FLinearColor& Color, const FString& Label)
	{
		DrawStepBox(Canvas, X, Y, W, 14.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		DrawStepBox(Canvas, X + 2.f, Y + 2.f, (W - 4.f) * FMath::Clamp(Fill, 0.f, 1.f), 10.f, Color);
		if (!Label.IsEmpty())
		{
			DrawStepText(Canvas, Label, X + W + 14.f, Y - 3.f, StepText, 0.9f, false, false);
		}
	}
}

bool UPuffersStepSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

TStatId UPuffersStepSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPuffersStepSubsystem, STATGROUP_Tickables);
}

void UPuffersStepSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_Client)
	{
		return; // story steps run where the player's save lives (standalone or listen server)
	}
	ReloadStepData();
	LoadKnownCharacters();

	if (UPuffersMissionSubsystem* Missions = GetMissions())
	{
		Missions->OnStageChanged.AddUniqueDynamic(this, &UPuffersStepSubsystem::HandleStageChanged);
		Missions->OnMissionStateChanged.AddUniqueDynamic(this, &UPuffersStepSubsystem::HandleMissionStateChanged);
		Missions->OnObjectiveUpdated.AddUniqueDynamic(this, &UPuffersStepSubsystem::HandleObjectiveUpdated);
		Missions->OnGameEvent.AddUniqueDynamic(this, &UPuffersStepSubsystem::HandleGameEvent);
		bBound = true;
	}
	DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UPuffersStepSubsystem::DrawOverlay));

	// A mission already running when the level opens (loaded save) gets its runners now.
	bDirty = true;
	bFullResetPending = true;
}

void UPuffersStepSubsystem::Deinitialize()
{
	if (DrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
	}
	if (UPuffersMissionSubsystem* Missions = GetMissions())
	{
		Missions->OnStageChanged.RemoveDynamic(this, &UPuffersStepSubsystem::HandleStageChanged);
		Missions->OnMissionStateChanged.RemoveDynamic(this, &UPuffersStepSubsystem::HandleMissionStateChanged);
		Missions->OnObjectiveUpdated.RemoveDynamic(this, &UPuffersStepSubsystem::HandleObjectiveUpdated);
		Missions->OnGameEvent.RemoveDynamic(this, &UPuffersStepSubsystem::HandleGameEvent);
	}
	for (const FName& Ability : TemporaryAbilities.Array())
	{
		RevokeTemporaryAbility(Ability);
	}
	Runners.Reset();
	bBound = false;
	Super::Deinitialize();
}

UPuffersMissionSubsystem* UPuffersStepSubsystem::GetMissions() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UPuffersMissionSubsystem>() : nullptr;
}

bool UPuffersStepSubsystem::ReloadStepData()
{
	const FString Directory = FPaths::ProjectContentDir() / TEXT("Data/Steps");
	FString Json, Error;
	bool bOk = true;
	if (FFileHelper::LoadFileToString(Json, *(Directory / TEXT("StepRules.json"))))
	{
		if (!Catalog.LoadRulesFromString(Json, Error))
		{
			UE_LOG(LogPuffers, Error, TEXT("[steps] %s"), *Error);
			bOk = false;
		}
	}
	else
	{
		UE_LOG(LogPuffers, Error, TEXT("[steps] Missing %s/StepRules.json. Missions fall back to stand-in props."), *Directory);
		bOk = false;
	}
	if (FFileHelper::LoadFileToString(Json, *(Directory / TEXT("Reactions.json"))))
	{
		if (!Catalog.LoadReactionsFromString(Json, Error))
		{
			UE_LOG(LogPuffers, Error, TEXT("[steps] %s"), *Error);
		}
	}
	UE_LOG(LogPuffers, Log, TEXT("[steps] %d type defaults, %d keyword rules, %d overrides, %d reactions"),
		Catalog.Defaults.Num(), Catalog.Rules.Num(), Catalog.Overrides.Num(), Catalog.Reactions.Num());
	bLoaded = true;
	bDirty = true;
	bFullResetPending = true;
	return bOk;
}

void UPuffersStepSubsystem::LoadKnownCharacters()
{
	KnownCharacters.Reset();
	CharacterNames.Reset();
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (const UPuffersRosterSubsystem* Roster = GameInstance ? GameInstance->GetSubsystem<UPuffersRosterSubsystem>() : nullptr)
	{
		for (const FPuffersCharacterDefinition& Character : Roster->GetAllCharacters())
		{
			KnownCharacters.AddUnique(Character.Id);
			CharacterNames.Add(Character.Id, Character.DisplayName.IsEmpty() ? Character.Id.ToString() : Character.DisplayName);
		}
	}
	// People the story names who aren't playable Puffers, so spawned speakers get the right name tag.
	static const TCHAR* Extras[] = { TEXT("Blockie"), TEXT("Doobie"), TEXT("Terp"), TEXT("Dank"), TEXT("Kush"),
		TEXT("Vanessa"), TEXT("Velvet"), TEXT("BigChurch"), TEXT("Haze"), TEXT("Lato"), TEXT("Mac"), TEXT("Bria"),
		TEXT("Brenda"), TEXT("CreamCake"), TEXT("Munchie"), TEXT("Janet"), TEXT("Sowa"), TEXT("Swisher"),
		TEXT("BigAl"), TEXT("Fritz"), TEXT("Saul"), TEXT("JokesUp") };
	for (const TCHAR* Extra : Extras)
	{
		KnownCharacters.AddUnique(Extra);
	}
}

FString UPuffersStepSubsystem::DisplayNameFor(FName CharacterId) const
{
	if (const FString* Name = CharacterNames.Find(CharacterId))
	{
		return *Name;
	}
	// "CreamCake" -> "Cream Cake"
	const FString Id = CharacterId.ToString();
	FString Out;
	for (int32 i = 0; i < Id.Len(); ++i)
	{
		if (i > 0 && FChar::IsUpper(Id[i]) && FChar::IsLower(Id[i - 1]))
		{
			Out.AppendChar(TEXT(' '));
		}
		Out.AppendChar(Id[i]);
	}
	return Out;
}

FPuffersStepParams UPuffersStepSubsystem::ResolveObjective(FName MissionId, const FPuffersObjectiveDefinition& Objective) const
{
	FPuffersStepParams Params = FPuffersStepRules::Resolve(Catalog, MissionId, Objective);
	Params.Difficulty = FMath::Clamp(Params.Difficulty + ReactionDifficulty(MissionId), 0.f, 1.f);
	return Params;
}

float UPuffersStepSubsystem::ReactionDifficulty(FName MissionId) const
{
	const UPuffersMissionSubsystem* Missions = GetMissions();
	float Delta = 0.f;
	for (const FPuffersStepReaction& Reaction : Catalog.Reactions)
	{
		if (Reaction.DifficultyDelta != 0.f && Reaction.Missions.Contains(MissionId) && Missions && Missions->HasWorldFlag(Reaction.Flag))
		{
			Delta += Reaction.DifficultyDelta;
		}
	}
	return Delta;
}

TArray<FString> UPuffersStepSubsystem::GetConversationReactions(FName MissionId, FName ObjectiveId) const
{
	TArray<FString> Out;
	const UPuffersMissionSubsystem* Missions = GetMissions();
	for (const FPuffersStepReaction& Reaction : Catalog.Reactions)
	{
		if (Reaction.Objective == ObjectiveId && !ObjectiveId.IsNone() && Missions && Missions->HasWorldFlag(Reaction.Flag)
			&& (Reaction.Missions.Num() == 0 || Reaction.Missions.Contains(MissionId)))
		{
			Out.Add(Reaction.Speaker.IsEmpty() ? Reaction.Line : FString::Printf(TEXT("%s: %s"), *Reaction.Speaker, *Reaction.Line));
		}
	}
	return Out;
}

void UPuffersStepSubsystem::ShowMissionStartReactions(FName MissionId)
{
	const UPuffersMissionSubsystem* Missions = GetMissions();
	StoryCard.Reset();
	for (const FPuffersStepReaction& Reaction : Catalog.Reactions)
	{
		if (Reaction.Objective.IsNone() && Reaction.Missions.Contains(MissionId) && Missions && Missions->HasWorldFlag(Reaction.Flag))
		{
			StoryCard.Add(Reaction.Speaker.IsEmpty() ? Reaction.Line : FString::Printf(TEXT("%s: %s"), *Reaction.Speaker, *Reaction.Line));
		}
	}
	if (StoryCard.Num() > 0)
	{
		StoryCardUntil = FPlatformTime::Seconds() + 6.0 + 2.0 * StoryCard.Num();
	}
}

void UPuffersStepSubsystem::HandleStageChanged(FName, int32)
{
	// A new stage, or the same stage again after a checkpoint restart: every runner starts fresh.
	bDirty = true;
	bFullResetPending = true;
}

void UPuffersStepSubsystem::HandleMissionStateChanged(FName, EPuffersMissionState)
{
	bDirty = true;
}

void UPuffersStepSubsystem::HandleObjectiveUpdated(FName, FName, int32, int32)
{
	bDirty = true;
}

void UPuffersStepSubsystem::HandleGameEvent(FName EventName, FName Context)
{
	// Copy: a runner reacting to an event can end the stage.
	const TArray<TObjectPtr<APuffersStepActor>> Current = Runners;
	for (APuffersStepActor* Runner : Current)
	{
		if (IsValid(Runner) && Runner->GetGeneration() == Generation)
		{
			Runner->OnGameEvent(EventName, Context);
		}
	}
}

TSubclassOf<APuffersStepActor> UPuffersStepSubsystem::RunnerClassFor(FName Gameplay) const
{
	static const TMap<FName, UClass*> Classes = {
		{ TEXT("Reach"), APuffersStepTravel::StaticClass() },
		{ TEXT("Route"), APuffersStepTravel::StaticClass() },
		{ TEXT("EnterVehicle"), APuffersStepTravel::StaticClass() },
		{ TEXT("Delivery"), APuffersStepDelivery::StaticClass() },
		{ TEXT("Chase"), APuffersStepChase::StaticClass() },
		{ TEXT("Escape"), APuffersStepEscape::StaticClass() },
		{ TEXT("Tail"), APuffersStepFollow::StaticClass() },
		{ TEXT("Escort"), APuffersStepFollow::StaticClass() },
		{ TEXT("Stealth"), APuffersStepStealth::StaticClass() },
		{ TEXT("Approach"), APuffersStepStealth::StaticClass() },
		{ TEXT("LoseHeat"), APuffersStepLoseHeat::StaticClass() },
		{ TEXT("Hold"), APuffersStepMinigame::StaticClass() },
		{ TEXT("Timing"), APuffersStepMinigame::StaticClass() },
		{ TEXT("Mash"), APuffersStepMinigame::StaticClass() },
		{ TEXT("Rhythm"), APuffersStepMinigame::StaticClass() },
		{ TEXT("Memory"), APuffersStepMinigame::StaticClass() },
		{ TEXT("Conversation"), APuffersStepDialogue::StaticClass() },
		{ TEXT("Persuade"), APuffersStepDialogue::StaticClass() },
		{ TEXT("Negotiate"), APuffersStepDialogue::StaticClass() },
		{ TEXT("BusinessMenu"), APuffersStepDialogue::StaticClass() },
		{ TEXT("Switch"), APuffersStepSwitch::StaticClass() },
		{ TEXT("Search"), APuffersStepSearch::StaticClass() },
		{ TEXT("Photo"), APuffersStepCamera::StaticClass() },
		{ TEXT("Video"), APuffersStepCamera::StaticClass() },
		{ TEXT("Defend"), APuffersStepDefend::StaticClass() },
		{ TEXT("Combat"), APuffersStepCombat::StaticClass() },
		{ TEXT("Scan"), APuffersStepScan::StaticClass() },
	};
	UClass* const* Found = Classes.Find(Gameplay);
	return Found ? *Found : nullptr;
}

AActor* UPuffersStepSubsystem::FindAnchor(FName ObjectiveId, FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (!World || ObjectiveId.IsNone())
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<UPuffersInteractableComponent>& Entry : UPuffersInteractableComponent::GetRegistered())
	{
		const UPuffersInteractableComponent* Component = Entry.Get();
		if (Component && Component->GetWorld() == World && Component->ObjectiveId == ObjectiveId && Component->GetOwner())
		{
			OutLocation = Component->GetOwner()->GetActorLocation();
			return Component->GetOwner();
		}
	}
	for (TActorIterator<APuffersNPCCharacter> It(World); It; ++It)
	{
		if (It->TalkObjectiveIds.Contains(ObjectiveId))
		{
			OutLocation = It->GetActorLocation();
			return *It;
		}
	}
	for (TActorIterator<APuffersObjectiveVolume> It(World); It; ++It)
	{
		if (It->ObjectiveId == ObjectiveId)
		{
			OutLocation = It->GetActorLocation();
			return *It;
		}
	}
	return nullptr;
}

bool UPuffersStepSubsystem::CanBeginChoice(const APuffersStepActor* Runner) const
{
	if (!Runner || Runner->GetChoiceGroup().IsNone())
	{
		return true;
	}
	for (const APuffersStepActor* Other : Runners)
	{
		if (IsValid(Other) && Other != Runner && Other->GetChoiceGroup() == Runner->GetChoiceGroup()
			&& Other->GetState() == EPuffersStepState::Running)
		{
			return false;
		}
	}
	return true;
}

void UPuffersStepSubsystem::DestroyRunner(APuffersStepActor* Runner)
{
	if (IsValid(Runner))
	{
		Runner->Destroy();
	}
}

void UPuffersStepSubsystem::DestroyAll()
{
	const TArray<TObjectPtr<APuffersStepActor>> Old = Runners;
	Runners.Reset();
	for (APuffersStepActor* Runner : Old)
	{
		DestroyRunner(Runner);
	}
}

void UPuffersStepSubsystem::Rebuild(bool bFullReset)
{
	UPuffersMissionSubsystem* Missions = GetMissions();
	UWorld* World = GetWorld();
	if (!Missions || !World)
	{
		return;
	}
	if (bFullReset)
	{
		DestroyAll();
		++Generation;
	}

	const FName Active = Missions->GetActiveMissionId();
	if (Active.IsNone() || Catalog.ExcludedMissions.Contains(Active))
	{
		DestroyAll();
		LastActiveMission = NAME_None;
		return;
	}
	if (Active != LastActiveMission)
	{
		LastActiveMission = Active;
		ShowMissionStartReactions(Active);
	}

	FPuffersMissionDefinition Mission;
	if (!Missions->GetMissionDefinition(Active, Mission) || !Missions->GetSave())
	{
		return;
	}
	const FPuffersMissionSaveRecord* Record = Missions->GetSave()->Missions.Find(Active);
	if (!Record)
	{
		return;
	}
	const TArray<FPuffersObjectiveDefinition> Objectives = Missions->GetActiveObjectives();
	auto NeedsProgress = [&](const FPuffersObjectiveDefinition& Objective)
	{
		return Missions->GetObjectiveProgress(Objective.Id) < Objective.RequiredCount
			&& FPuffersMissionRules::AcceptsProgress(Mission, Objective, *Record);
	};

	// Drop runners whose objective is finished, no longer current, or lost its choice.
	for (int32 Index = Runners.Num() - 1; Index >= 0; --Index)
	{
		APuffersStepActor* Runner = Runners[Index];
		bool bKeep = IsValid(Runner) && Runner->GetMissionId() == Active && Runner->GetGeneration() == Generation;
		if (bKeep)
		{
			bKeep = Objectives.ContainsByPredicate([&](const FPuffersObjectiveDefinition& Objective)
			{
				return Runner->CoversObjective(Objective.Id) && NeedsProgress(Objective);
			});
		}
		if (!bKeep)
		{
			Runners.RemoveAt(Index);
			DestroyRunner(Runner);
		}
	}

	APawn* PlayerPawn = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	for (const FPuffersObjectiveDefinition& Objective : Objectives)
	{
		if (!NeedsProgress(Objective))
		{
			continue;
		}
		const bool bCovered = Runners.ContainsByPredicate([&](const APuffersStepActor* Runner)
		{
			return IsValid(Runner) && Runner->CoversObjective(Objective.Id);
		});
		if (bCovered)
		{
			continue;
		}
		const FPuffersStepParams Params = ResolveObjective(Active, Objective);
		const TSubclassOf<APuffersStepActor> Class = RunnerClassFor(Params.Gameplay);
		if (!Class)
		{
			continue; // Prop: the placed stand-in handles it
		}
		FVector AnchorLocation = FVector::ZeroVector;
		AActor* AnchorActor = FindAnchor(Objective.Id, AnchorLocation);
		if (!AnchorActor)
		{
			AnchorLocation = PlayerPawn ? PlayerPawn->GetActorLocation() + PlayerPawn->GetActorForwardVector() * 700.f : FVector::ZeroVector;
		}
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APuffersStepActor* Runner = World->SpawnActor<APuffersStepActor>(Class, AnchorLocation, FRotator::ZeroRotator, SpawnParams);
		if (!Runner)
		{
			continue;
		}
		Runners.Add(Runner);
		Runner->Init(this, Active, Objective, Params, Generation, AnchorActor, AnchorLocation);
		UE_LOG(LogPuffers, Verbose, TEXT("[steps] %s %s -> %s"), *Active.ToString(), *Objective.Id.ToString(), *Params.Gameplay.ToString());
	}
}

void UPuffersStepSubsystem::Tick(float DeltaTime)
{
	if (!bBound)
	{
		return;
	}
	if (bDirty)
	{
		const bool bFull = bFullResetPending;
		bDirty = false;
		bFullResetPending = false;
		Rebuild(bFull);
	}

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC && (PC->WasInputKeyJustPressed(EKeys::Z) || PC->WasInputKeyJustPressed(EKeys::Gamepad_RightThumbstick)))
	{
		ActivateContextAbility();
	}
	UpdateMenuBlock();
}

void UPuffersStepSubsystem::ShowToast(const FString& InText, bool bAlert)
{
	Toast = InText;
	bToastAlert = bAlert;
	ToastUntil = FPlatformTime::Seconds() + 3.5;
}

bool UPuffersStepSubsystem::HasAbility(FName Ability) const
{
	const UPuffersMissionSubsystem* Missions = GetMissions();
	return TemporaryAbilities.Contains(Ability) || (Missions && Missions->HasAbility(Ability));
}

bool UPuffersStepSubsystem::IsAbilityActive(FName Ability) const
{
	const double* Until = AbilityActiveUntil.Find(Ability);
	return Until && FPlatformTime::Seconds() < *Until;
}

void UPuffersStepSubsystem::GrantTemporaryAbility(FName Ability)
{
	if (!Ability.IsNone())
	{
		TemporaryAbilities.Add(Ability);
	}
}

void UPuffersStepSubsystem::RevokeTemporaryAbility(FName Ability)
{
	TemporaryAbilities.Remove(Ability);
}

FName UPuffersStepSubsystem::ActivateContextAbility()
{
	TArray<FName> Candidates;
	for (const APuffersStepActor* Runner : Runners)
	{
		if (IsValid(Runner) && Runner->GetState() == EPuffersStepState::Running)
		{
			Candidates.Append(Runner->GetParams().Abilities);
		}
	}
	for (const FAbilityInfo& Info : ActiveAbilities)
	{
		Candidates.AddUnique(Info.Name);
	}

	const double Now = FPlatformTime::Seconds();
	for (const FName& Ability : Candidates)
	{
		const FAbilityInfo* Info = FindAbility(Ability);
		if (!Info || !HasAbility(Ability))
		{
			continue;
		}
		const double* Ready = AbilityReadyAt.Find(Ability);
		if (Ready && Now < *Ready)
		{
			ShowToast(FString::Printf(TEXT("%s recharging (%ds)"), Info->Label, FMath::CeilToInt(*Ready - Now)));
			return NAME_None;
		}
		AbilityActiveUntil.Add(Ability, Now + Info->Duration);
		AbilityReadyAt.Add(Ability, Now + Info->Cooldown);
		ShowToast(FString::Printf(TEXT("%s!"), Info->Label));
		const TArray<TObjectPtr<APuffersStepActor>> Current = Runners;
		for (APuffersStepActor* Runner : Current)
		{
			if (IsValid(Runner) && Runner->GetState() == EPuffersStepState::Running)
			{
				Runner->OnAbilityUsed(Ability);
			}
		}
		if (UPuffersMissionSubsystem* Missions = GetMissions())
		{
			Missions->BroadcastGameEvent(EventAbilityUsed, Ability);
		}
		return Ability;
	}
	return NAME_None;
}

bool UPuffersStepSubsystem::SwitchToCharacter(FName CharacterId)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APuffersPlayerCharacter* Pawn = PC ? Cast<APuffersPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!Pawn || CharacterId.IsNone())
	{
		return false;
	}
	if (Pawn->ActivePuffer == CharacterId || Pawn->CharacterId == CharacterId)
	{
		return true;
	}
	if (!Pawn->BecomePuffer(CharacterId))
	{
		UPuffersRosterSubsystem* Roster = World->GetGameInstance()->GetSubsystem<UPuffersRosterSubsystem>();
		if (!Roster || !Roster->PlayAsCharacter(Pawn, CharacterId, true))
		{
			UE_LOG(LogPuffers, Log, TEXT("[steps] %s has no body yet; playing on as a stand-in"), *CharacterId.ToString());
		}
	}
	Pawn->CharacterId = CharacterId;
	ShowToast(FString::Printf(TEXT("Now playing as %s"), *DisplayNameFor(CharacterId)));
	return true;
}

void UPuffersStepSubsystem::UpdateMenuBlock()
{
	bool bWant = false;
	for (const APuffersStepActor* Runner : Runners)
	{
		bWant |= IsValid(Runner) && Runner->GetState() == EPuffersStepState::Running && Runner->WantsMenuInput();
	}
	if (bWant == bMenuBlockApplied)
	{
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = PC && PC->GetLocalPlayer()
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	if (!Input)
	{
		return;
	}
	if (!MenuBlockContext)
	{
		MenuBlockAction = NewObject<UInputAction>(this, TEXT("IA_StepMenuBlock"));
		MenuBlockAction->ValueType = EInputActionValueType::Boolean;
		MenuBlockAction->bConsumeInput = true;
		MenuBlockContext = NewObject<UInputMappingContext>(this, TEXT("IMC_StepMenuBlock"));
		for (const FKey& Key : { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Gamepad_DPad_Up,
			EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left })
		{
			MenuBlockContext->MapKey(MenuBlockAction, Key);
		}
	}
	if (bWant)
	{
		Input->AddMappingContext(MenuBlockContext, 100);
	}
	else
	{
		Input->RemoveMappingContext(MenuBlockContext);
	}
	bMenuBlockApplied = bWant;
}

void UPuffersStepSubsystem::DrawPanel(UCanvas* Canvas, const FPuffersStepOverlay& Overlay, float CenterX, float& Bottom)
{
	// Measure from the bottom up so the panel sits above the screen edge.
	const float Width = 560.f;
	const float Left = CenterX - Width * 0.5f;
	const float LineH = 22.f;
	int32 Rows = 1 + Overlay.Lines.Num() + (Overlay.Bar >= 0.f ? 1 : 0) + (Overlay.Bar2 >= 0.f ? 1 : 0) + Overlay.Options.Num();
	float Y = Bottom - Rows * (LineH + 4.f) - 16.f;
	DrawStepBox(Canvas, Left - 12.f, Y - 8.f, Width + 24.f, Rows * (LineH + 4.f) + 16.f, FLinearColor(0.f, 0.f, 0.f, 0.45f));

	DrawStepText(Canvas, Overlay.Header, Left, Y, StepGold, 1.05f, false, false);
	Y += LineH + 4.f;
	for (const FString& Line : Overlay.Lines)
	{
		DrawStepText(Canvas, Line, Left, Y, StepText, 0.95f, false, false);
		Y += LineH + 4.f;
	}
	if (Overlay.Bar >= 0.f)
	{
		DrawStepMeter(Canvas, Left, Y + 4.f, 360.f, Overlay.Bar, Overlay.BarColor, Overlay.BarLabel);
		Y += LineH + 4.f;
	}
	if (Overlay.Bar2 >= 0.f)
	{
		DrawStepMeter(Canvas, Left, Y + 4.f, 360.f, Overlay.Bar2, Overlay.Bar2Color, Overlay.Bar2Label);
		Y += LineH + 4.f;
	}
	for (int32 i = 0; i < Overlay.Options.Num(); ++i)
	{
		DrawStepText(Canvas, FString::Printf(TEXT("[%d]  %s"), i + 1, *Overlay.Options[i]), Left + 10.f, Y, StepGold, 1.f, false, false);
		Y += LineH + 4.f;
	}
	Bottom = Bottom - Rows * (LineH + 4.f) - 40.f;
}

void UPuffersStepSubsystem::DrawOverlay(UCanvas* Canvas, APlayerController* PC)
{
	UWorld* World = GetWorld();
	if (!Canvas || !World || !PC || PC->GetWorld() != World || PC != World->GetFirstPlayerController())
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const FVector Eye = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;

	bool bPhone = false, bRecording = false, bCrosshair = false;
	FString Center;
	float Bottom = H - 30.f;

	for (const APuffersStepActor* Runner : Runners)
	{
		if (!IsValid(Runner) || Runner->GetState() == EPuffersStepState::Done)
		{
			continue;
		}
		FPuffersStepOverlay Overlay;
		Runner->BuildOverlay(Overlay);

		for (const FPuffersStepMarker& Marker : Overlay.Markers)
		{
			FVector2D Screen;
			if (PC->ProjectWorldLocationToScreen(Marker.Location + FVector(0.f, 0.f, 120.f), Screen))
			{
				FString Label = Marker.Text;
				if (Marker.bShowDistance)
				{
					Label += FString::Printf(TEXT("  %dm"), FMath::RoundToInt(FVector::Dist(Eye, Marker.Location) / 100.f));
				}
				DrawStepText(Canvas, Label, Screen.X, Screen.Y, Marker.Color, 0.9f, true, true);
			}
		}

		if (Runner->GetState() == EPuffersStepState::Running || !Overlay.Lines.IsEmpty() || Overlay.Bar >= 0.f)
		{
			if (!Overlay.Header.IsEmpty())
			{
				DrawPanel(Canvas, Overlay, W * 0.5f, Bottom);
			}
		}
		bPhone |= Overlay.bPhoneFrame;
		bRecording |= Overlay.bRecording;
		bCrosshair |= Overlay.bCrosshair;
		if (!Overlay.Center.IsEmpty())
		{
			Center = Overlay.Center;
		}
	}

	if (bPhone)
	{
		// Phone screen: a portrait frame in the middle of the view.
		const float FH = H * 0.8f, FW = FH * 0.56f;
		const float FX = (W - FW) * 0.5f, FY = (H - FH) * 0.5f;
		Canvas->K2_DrawBox(FVector2D(FX, FY), FVector2D(FW, FH), 3.f, StepText);
		Canvas->K2_DrawLine(FVector2D(W * 0.5f - 18.f, H * 0.5f), FVector2D(W * 0.5f + 18.f, H * 0.5f), 1.5f, StepText);
		Canvas->K2_DrawLine(FVector2D(W * 0.5f, H * 0.5f - 18.f), FVector2D(W * 0.5f, H * 0.5f + 18.f), 1.5f, StepText);
		if (bRecording)
		{
			DrawStepBox(Canvas, FX + 20.f, FY + 20.f, 16.f, 16.f, StepAlert);
			DrawStepText(Canvas, TEXT("REC"), FX + 44.f, FY + 16.f, StepAlert, 1.f, false, false);
		}
	}
	else if (bCrosshair)
	{
		Canvas->K2_DrawLine(FVector2D(W * 0.5f - 10.f, H * 0.5f), FVector2D(W * 0.5f + 10.f, H * 0.5f), 2.f, StepText);
		Canvas->K2_DrawLine(FVector2D(W * 0.5f, H * 0.5f - 10.f), FVector2D(W * 0.5f, H * 0.5f + 10.f), 2.f, StepText);
	}

	if (!Center.IsEmpty())
	{
		DrawStepText(Canvas, Center, W * 0.5f, H * 0.36f, StepGold, 2.f, true, true);
	}

	const double Now = FPlatformTime::Seconds();
	if (Now < ToastUntil && !Toast.IsEmpty())
	{
		DrawStepText(Canvas, Toast, W * 0.5f, H * 0.18f, bToastAlert ? StepAlert : StepGood, 1.3f, true, true);
	}
	if (Now < StoryCardUntil)
	{
		float Y = H * 0.42f;
		for (const FString& Line : StoryCard)
		{
			Y += DrawStepText(Canvas, Line, 40.f, Y, StepText, 1.f, false, true) + 8.f;
		}
	}

	// Abilities on Z, bottom left.
	float AY = H - 60.f;
	for (const FAbilityInfo& Info : ActiveAbilities)
	{
		if (!HasAbility(Info.Name))
		{
			continue;
		}
		const double* Ready = AbilityReadyAt.Find(Info.Name);
		const bool bActive = IsAbilityActive(Info.Name);
		const int32 Wait = Ready ? FMath::Max(0, FMath::CeilToInt(*Ready - Now)) : 0;
		const FString Label = bActive ? FString::Printf(TEXT("Z  %s  ACTIVE"), Info.Label)
			: Wait > 0 ? FString::Printf(TEXT("Z  %s  %ds"), Info.Label, Wait) : FString::Printf(TEXT("Z  %s  ready"), Info.Label);
		DrawStepText(Canvas, Label, 40.f, AY, bActive ? StepGood : (Wait > 0 ? StepMuted : StepGold), 0.9f);
		AY -= 26.f;
	}
}
