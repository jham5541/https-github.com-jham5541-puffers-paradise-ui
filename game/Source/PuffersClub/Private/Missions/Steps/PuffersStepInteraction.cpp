// Interaction steps: mini-games (Hold, Timing, Mash, Rhythm, Memory), conversations (Conversation, Persuade,
// Negotiate, BusinessMenu) and character switching.

#include "Missions/Steps/PuffersStepActor.h"
#include "Missions/Steps/PuffersStepAIController.h"
#include "Missions/Steps/PuffersStepRules.h"
#include "Missions/Steps/PuffersStepSubsystem.h"
#include "Missions/PuffersMissionRules.h"
#include "Missions/PuffersMissionSubsystem.h"
#include "Missions/PuffersSaveGame.h"
#include "Player/PuffersPlayerCharacter.h"
#include "World/PuffersInteractableComponent.h"
#include "Engine/World.h"

namespace
{
	FString Money(int32 Amount)
	{
		return FString::Printf(TEXT("$%d"), FMath::Abs(Amount));
	}
}

// Mini-games ---------------------------------------------------------------------------------------------------

FString APuffersStepMinigame::GetGameplayLabel() const
{
	if (Params.Gameplay == TEXT("Hold")) return TEXT("HOLD");
	if (Params.Gameplay == TEXT("Timing")) return TEXT("TIMING");
	if (Params.Gameplay == TEXT("Mash")) return TEXT("PUSH");
	if (Params.Gameplay == TEXT("Rhythm")) return TEXT("PERFORM");
	return TEXT("MEMORY");
}

FString APuffersStepMinigame::ArrowName(int32 Arrow)
{
	switch (Arrow)
	{
	case 1: return TEXT("UP");
	case 2: return TEXT("RIGHT");
	case 3: return TEXT("DOWN");
	case 4: return TEXT("LEFT");
	default: return TEXT("");
	}
}

int32 APuffersStepMinigame::ArrowPressed() const
{
	if (KeyPressed(EKeys::Up)) return 1;
	if (KeyPressed(EKeys::Right)) return 2;
	if (KeyPressed(EKeys::Down)) return 3;
	if (KeyPressed(EKeys::Left)) return 4;
	return OptionPressed();
}

void APuffersStepMinigame::OnBegin()
{
	LockMovement(true);
	Meter = 0.f;
	Hits = 0;
	Misses = 0;
	NeedleTime = 0.f;
	Flash = 0.f;
	Round = 0;
	Sequence.Reset();
	const float D = GetDifficulty();
	const FName Style = Params.Gameplay;
	if (Style == TEXT("Timing"))
	{
		Goal = Params.Count > 0 ? Params.Count : 3;
		ZoneWidth = FMath::Lerp(0.26f, 0.12f, D);
		ZoneCenter = 0.3f + 0.4f * FPuffersStepRules::StableIndex(Objective.Id, 9, 100) / 100.f;
		StatusLine = TEXT("Press E when the needle is inside the zone.");
	}
	else if (Style == TEXT("Mash"))
	{
		StatusLine = TEXT("Tap E as fast as you can!");
	}
	else if (Style == TEXT("Rhythm"))
	{
		Beats = Params.Count > 0 ? Params.Count : 8;
		CurrentBeat = 0;
		BeatTimer = 1.2f;
		bBeatOpen = false;
		StatusLine = TEXT("Hit the arrow keys (or 1-4) on the beat.");
	}
	else if (Style == TEXT("Memory"))
	{
		Goal = Params.Count > 0 ? Params.Count : 2;
		NewMemoryRound();
	}
	else
	{
		StatusLine = TEXT("Hold E.");
	}
}

void APuffersStepMinigame::NewMemoryRound()
{
	FRandomStream Random(FPuffersStepRules::StableIndex(Objective.Id, 50 + Round + Misses * 13, 1000003));
	Sequence.Reset();
	const int32 Length = 3 + Round + FMath::RoundToInt(GetDifficulty() * 2.f);
	for (int32 i = 0; i < Length; ++i)
	{
		Sequence.Add(Random.RandRange(1, 4));
	}
	SeqIndex = 0;
	bShowing = true;
	BeatTimer = 0.9f;
	StatusLine = TEXT("Watch the sequence...");
}

void APuffersStepMinigame::NextBeat()
{
	FRandomStream Random(FPuffersStepRules::StableIndex(Objective.Id, 70 + CurrentBeat, 1000003));
	Sequence = { Random.RandRange(1, 4) };
	bBeatOpen = true;
	BeatTimer = FMath::Lerp(0.85f, 0.5f, GetDifficulty());
}

void APuffersStepMinigame::OnRunning(float DeltaSeconds)
{
	const FName Style = Params.Gameplay;
	const bool bInputOpen = Elapsed() > 0.25f; // ignore the E press that started the step
	Flash = FMath::Max(0.f, Flash - DeltaSeconds);

	if (Style == TEXT("Hold"))
	{
		const float Seconds = Params.Seconds > 0.f ? Params.Seconds : 1.5f;
		Meter = InteractDown() ? Meter + DeltaSeconds / Seconds : FMath::Max(0.f, Meter - DeltaSeconds * 0.6f);
		if (Meter >= 1.f)
		{
			Succeed();
		}
		else if (!InteractDown() && Meter <= 0.f && Elapsed() > 2.5f)
		{
			Leave(); // walked off the job; press E again to restart
		}
		return;
	}

	if (Style == TEXT("Timing"))
	{
		NeedleTime += DeltaSeconds;
		const float Speed = FMath::Lerp(0.7f, 1.4f, GetDifficulty()) + 0.15f * Hits;
		Meter = FPuffersStepRules::NeedleAt(NeedleTime, Speed);
		if (bInputOpen && InteractPressed())
		{
			if (FPuffersStepRules::IsNeedleInZone(Meter, ZoneCenter, ZoneWidth))
			{
				++Hits;
				FlashText = TEXT("NICE");
				ZoneCenter = 0.15f + 0.7f * FPuffersStepRules::StableIndex(Objective.Id, 9 + Hits, 100) / 100.f;
				if (Hits >= Goal)
				{
					Succeed();
					return;
				}
			}
			else
			{
				++Misses;
				FlashText = TEXT("MISS");
				if (Misses >= 3)
				{
					Fail(TEXT("Botched it."));
					return;
				}
			}
			Flash = 0.6f;
		}
		return;
	}

	if (Style == TEXT("Mash"))
	{
		const float Limit = Params.Seconds > 0.f ? Params.Seconds : 6.f;
		if (bInputOpen && InteractPressed())
		{
			Meter += FMath::Lerp(0.09f, 0.055f, GetDifficulty());
		}
		Meter = FMath::Max(0.f, Meter - DeltaSeconds * 0.2f);
		if (Meter >= 1.f)
		{
			Succeed();
		}
		else if (Elapsed() > Limit)
		{
			Fail(TEXT("Not enough."));
		}
		return;
	}

	if (Style == TEXT("Rhythm"))
	{
		BeatTimer -= DeltaSeconds;
		const int32 Pressed = bInputOpen ? ArrowPressed() : 0;
		if (bBeatOpen)
		{
			if (Pressed != 0)
			{
				const bool bHit = Sequence.Num() > 0 && Pressed == Sequence[0];
				Hits += bHit ? 1 : 0;
				Misses += bHit ? 0 : 1;
				FlashText = bHit ? TEXT("ON BEAT") : TEXT("OFF");
				Flash = 0.4f;
				bBeatOpen = false;
				BeatTimer = 0.35f;
				++CurrentBeat;
			}
			else if (BeatTimer <= 0.f)
			{
				++Misses;
				FlashText = TEXT("LATE");
				Flash = 0.4f;
				bBeatOpen = false;
				BeatTimer = 0.35f;
				++CurrentBeat;
			}
		}
		else if (BeatTimer <= 0.f)
		{
			if (CurrentBeat >= Beats)
			{
				if (Hits >= FMath::CeilToInt(Beats * 0.7f))
				{
					Succeed(FString::Printf(TEXT("%d/%d on beat. The crowd loved it."), Hits, Beats));
				}
				else
				{
					Fail(FString::Printf(TEXT("Only %d/%d on beat."), Hits, Beats));
				}
				return;
			}
			NextBeat();
		}
		Meter = Beats > 0 ? static_cast<float>(CurrentBeat) / Beats : 0.f;
		return;
	}

	// Memory
	if (bShowing)
	{
		BeatTimer -= DeltaSeconds;
		if (BeatTimer <= 0.f)
		{
			++SeqIndex;
			BeatTimer = FMath::Lerp(0.9f, 0.55f, GetDifficulty());
			if (SeqIndex >= Sequence.Num())
			{
				bShowing = false;
				SeqIndex = 0;
				StatusLine = TEXT("Your turn: repeat it with the arrow keys (or 1-4).");
			}
		}
		return;
	}
	const int32 Pressed = bInputOpen ? ArrowPressed() : 0;
	if (Pressed == 0)
	{
		return;
	}
	if (Sequence.IsValidIndex(SeqIndex) && Pressed == Sequence[SeqIndex])
	{
		++SeqIndex;
		if (SeqIndex >= Sequence.Num())
		{
			++Round;
			if (Round >= Goal)
			{
				Succeed();
				return;
			}
			FlashText = TEXT("GOT IT");
			Flash = 0.6f;
			NewMemoryRound();
		}
	}
	else
	{
		++Misses;
		FlashText = TEXT("WRONG");
		Flash = 0.6f;
		if (Misses >= 3)
		{
			Fail(TEXT("Mixed it up."));
			return;
		}
		NewMemoryRound();
	}
}

void APuffersStepMinigame::OnEnd()
{
	LockMovement(false);
}

void APuffersStepMinigame::FillOverlay(FPuffersStepOverlay& Out) const
{
	const FName Style = Params.Gameplay;
	if (Style == TEXT("Hold") || Style == TEXT("Mash"))
	{
		Out.Bar = Meter;
		Out.BarLabel = Style == TEXT("Hold") ? TEXT("Hold E") : TEXT("Tap E");
		if (Style == TEXT("Mash"))
		{
			const float Limit = Params.Seconds > 0.f ? Params.Seconds : 6.f;
			Out.Lines.Add(FString::Printf(TEXT("%.1fs left"), FMath::Max(0.f, Limit - Elapsed())));
		}
	}
	else if (Style == TEXT("Timing"))
	{
		// A text gauge: '=' is the zone, '|' the needle.
		const int32 Cells = 40;
		FString Gauge;
		const int32 Needle = FMath::Clamp(FMath::RoundToInt(Meter * (Cells - 1)), 0, Cells - 1);
		for (int32 i = 0; i < Cells; ++i)
		{
			const float At = i / static_cast<float>(Cells - 1);
			Gauge += i == Needle ? TEXT("|") : (FPuffersStepRules::IsNeedleInZone(At, ZoneCenter, ZoneWidth) ? TEXT("=") : TEXT("-"));
		}
		Out.Lines.Add(FString::Printf(TEXT("[%s]"), *Gauge));
		Out.Lines.Add(FString::Printf(TEXT("Hits %d/%d   Misses %d/3"), Hits, Goal, Misses));
	}
	else if (Style == TEXT("Rhythm"))
	{
		Out.Bar = Meter;
		Out.BarLabel = TEXT("Song");
		Out.Lines.Add(FString::Printf(TEXT("On beat %d   Missed %d"), Hits, Misses));
		if (bBeatOpen && Sequence.Num() > 0)
		{
			Out.Center = ArrowName(Sequence[0]);
		}
	}
	else
	{
		Out.Lines.Add(FString::Printf(TEXT("Round %d/%d   Mistakes %d/3"), Round + 1, Goal, Misses));
		if (bShowing && Sequence.IsValidIndex(SeqIndex))
		{
			Out.Center = ArrowName(Sequence[SeqIndex]);
		}
		else if (!bShowing)
		{
			Out.Lines.Add(FString::Printf(TEXT("%d/%d entered"), SeqIndex, Sequence.Num()));
		}
	}
	if (Flash > 0.f)
	{
		Out.Center = FlashText;
	}
}

// Dialogue -----------------------------------------------------------------------------------------------------

FString APuffersStepDialogue::GetGameplayLabel() const
{
	if (Params.Gameplay == TEXT("Persuade")) return TEXT("PERSUADE");
	if (Params.Gameplay == TEXT("Negotiate")) return TEXT("NEGOTIATE");
	if (Params.Gameplay == TEXT("BusinessMenu")) return TEXT("DECISION");
	return TEXT("TALK");
}

bool APuffersStepDialogue::CoversObjective(FName ObjectiveId) const
{
	return ObjectiveId == Objective.Id || MenuObjectives.Contains(ObjectiveId);
}

bool APuffersStepDialogue::HasTalkAbility() const
{
	return HasAbility(TEXT("TalkThatShit")) || HasAbility(TEXT("GiftOfGab"));
}

void APuffersStepDialogue::OnPrepare()
{
	Preferred = FPuffersStepRules::StableIndex(Objective.Id, 1, 3);
	Disliked = (Preferred + 1 + FPuffersStepRules::StableIndex(Objective.Id, 2, 2)) % 3;

	if (Params.Gameplay == TEXT("BusinessMenu"))
	{
		MenuObjectives.Reset();
		MenuObjectives.Add(Objective.Id);
		UPuffersMissionSubsystem* Missions = GetMissions();
		UPuffersStepSubsystem* Subsystem = Owner.Get();
		if (Missions && Subsystem && !Objective.ChoiceGroup.IsNone())
		{
			for (const FPuffersObjectiveDefinition& Other : Missions->GetActiveObjectives())
			{
				if (Other.Id != Objective.Id && Other.ChoiceGroup == Objective.ChoiceGroup
					&& Subsystem->ResolveObjective(MissionId, Other).Gameplay == TEXT("BusinessMenu"))
				{
					MenuObjectives.Add(Other.Id);
					HijackObjectiveProp(Other.Id);
				}
			}
		}
		return;
	}

	// Talking needs someone to talk to: put a speaker next to the stand-in prop unless the anchor is a person.
	if (Params.Gameplay != TEXT("Negotiate") || !Cast<APuffersNPCCharacter>(AnchorActor.Get()))
	{
		if (!Cast<APuffersNPCCharacter>(AnchorActor.Get()))
		{
			const FString Name = SpeakerName();
			const FVector At = Anchor + FVector(160.f, 120.f, 0.f);
			if (APuffersNPCCharacter* Speaker = SpawnNPC(At, Name.IsEmpty() ? TEXT("Contact") : Name, EPuffersNPCRole::Pedestrian, 120.f, 300.f, true, true))
			{
				ListenTo(Speaker->Interactable);
				if (APuffersStepAIController* Brain = AI(Speaker))
				{
					Brain->HoldPosition(nullptr);
				}
			}
		}
	}
}

void APuffersStepDialogue::OnBegin()
{
	LockMovement(true);
	Lines.Reset();
	Options.Reset();
	Turn = 0;
	Interest = 0;
	Patience = 3;
	bAbilityUsed = false;
	EndDelay = -1.f;
	bWon = false;

	if (const UPuffersStepSubsystem* Subsystem = Owner.Get())
	{
		Lines.Append(Subsystem->GetConversationReactions(MissionId, Objective.Id));
	}
	const FString Speaker = SpeakerName().IsEmpty() ? TEXT("They") : SpeakerName();

	if (Params.Gameplay == TEXT("BusinessMenu"))
	{
		const UPuffersMissionSubsystem* Missions = GetMissions();
		TArray<FPuffersObjectiveDefinition> Current = Missions ? Missions->GetActiveObjectives() : TArray<FPuffersObjectiveDefinition>();
		for (const FName& Id : MenuObjectives)
		{
			const FPuffersObjectiveDefinition* Option = Current.FindByPredicate([&](const FPuffersObjectiveDefinition& O) { return O.Id == Id; });
			if (!Option)
			{
				continue;
			}
			FString Text = Option->Description;
			if (Option->PuffCashOnDone != 0)
			{
				Text += Option->PuffCashOnDone > 0 ? FString::Printf(TEXT("  (+%s)"), *Money(Option->PuffCashOnDone))
					: FString::Printf(TEXT("  (costs %s)"), *Money(Option->PuffCashOnDone));
			}
			if (Option->WorldFlagsOnDone.Num() > 0)
			{
				Text += FString::Printf(TEXT("  -> %s"), *FPuffersStepRules::PrettyFlag(Option->WorldFlagsOnDone[0]));
			}
			Options.Add(Text);
		}
		Lines.Add(MenuObjectives.Num() > 1 ? TEXT("Pick one. You can't take it back.") : TEXT("Sign off on it?"));
		Options.Add(TEXT("Think it over (leave)"));
		return;
	}
	if (Params.Gameplay == TEXT("Negotiate"))
	{
		Ask = Objective.PuffCashOnDone != 0 ? FMath::Abs(Objective.PuffCashOnDone) * 4 : 500;
		Floor = FMath::Clamp(0.72f + 0.18f * FPuffersStepRules::StableIndex(Objective.Id, 3, 100) / 100.f + 0.05f * GetDifficulty(), 0.6f, 0.95f);
		Offer = 0.6f;
		Lines.Add(FString::Printf(TEXT("%s wants %s. Find the number they'll take without overpaying."), *Speaker, *Money(Ask)));
		return;
	}
	Lines.Add(FString::Printf(TEXT("(%s)"), *Objective.Description));
	if (Params.Gameplay == TEXT("Persuade"))
	{
		Lines.Add(FString::Printf(TEXT("%s isn't sold yet. Read them and pick your approach."), *Speaker));
	}
}

void APuffersStepDialogue::Pick(int32 Option)
{
	static const TCHAR* Tones[] = { TEXT("Smooth"), TEXT("Real"), TEXT("Hard") };
	const FString Speaker = SpeakerName().IsEmpty() ? TEXT("They") : SpeakerName();

	if (Params.Gameplay == TEXT("BusinessMenu"))
	{
		if (Option == Options.Num())
		{
			Leave();
			return;
		}
		if (MenuObjectives.IsValidIndex(Option - 1))
		{
			const FName Chosen = MenuObjectives[Option - 1];
			Toast(FString::Printf(TEXT("Decided: %s"), *Options[Option - 1]));
			ReportFor(Chosen);
		}
		return;
	}

	if (Params.Gameplay == TEXT("Negotiate"))
	{
		switch (Option)
		{
		case 1: Offer = FMath::Max(0.3f, Offer - 0.05f); return;
		case 2: Offer = FMath::Min(1.f, Offer + 0.05f); return;
		case 4: Fail(TEXT("You walked away from the table.")); return;
		default: break;
		}
		const int32 Amount = FMath::RoundToInt(Ask * Offer);
		Lines.Add(FString::Printf(TEXT("You: %s."), *Money(Amount)));
		switch (FPuffersStepRules::EvaluateOffer(Offer, Floor))
		{
		case FPuffersStepRules::EHaggle::Deal:
			Lines.Add(FString::Printf(TEXT("%s: Deal."), *Speaker));
			if (UPuffersMissionSubsystem* Missions = GetMissions())
			{
				Missions->AddMissionStat(TEXT("DealSavings"), FMath::RoundToInt(Ask * (1.f - Offer)));
			}
			bWon = true;
			EndDelay = 1.2f;
			return;
		case FPuffersStepRules::EHaggle::Warm:
			Lines.Add(FString::Printf(TEXT("%s: Close. Come up a little."), *Speaker));
			break;
		default:
			Lines.Add(FString::Printf(TEXT("%s: Not even close."), *Speaker));
			break;
		}
		if (--Patience <= 0)
		{
			Fail(TEXT("They're done haggling."));
		}
		return;
	}

	const bool bAbility = Option == 4 && HasTalkAbility() && !bAbilityUsed;
	if (Option < 1 || (Option > 3 && !bAbility))
	{
		return;
	}
	const int32 Tone = FMath::Clamp(Option - 1, 0, 2);
	if (UPuffersMissionSubsystem* Missions = GetMissions())
	{
		Missions->AddMissionStat(*FString::Printf(TEXT("Tone_%s"), bAbility ? TEXT("Ability") : Tones[Tone]), 1);
	}

	if (Params.Gameplay == TEXT("Conversation"))
	{
		static const TCHAR* Replies[3][3] = {
			{ TEXT("They laugh. You've still got it."), TEXT("They relax. Good."), TEXT("They grin like they knew you'd say that.") },
			{ TEXT("They nod slowly. Respect."), TEXT("A long look. Then: 'Alright.'"), TEXT("'Finally, somebody talking straight.'") },
			{ TEXT("They flinch, then square up. Point made."), TEXT("Nobody's laughing now."), TEXT("They back down. For now.") } };
		Lines.Add(FString::Printf(TEXT("You keep it %s."), *FString(Tones[Tone]).ToLower()));
		Lines.Add(Replies[Tone][FPuffersStepRules::StableIndex(Objective.Id, 4, 3)]);
		bWon = true;
		EndDelay = 1.6f;
		return;
	}

	// Persuade
	int32 Gain = FPuffersStepRules::PersuadeGain(Tone, Preferred, Disliked);
	if (bAbility)
	{
		bAbilityUsed = true;
		Gain = 60;
		Lines.Add(TEXT("You run the full pitch. They didn't stand a chance."));
	}
	else if (Tone == Preferred)
	{
		Lines.Add(TEXT("They lean in. That's what they wanted to hear."));
	}
	else if (Tone == Disliked)
	{
		Lines.Add(TEXT("They scowl. Wrong move."));
		--Patience;
	}
	else
	{
		Lines.Add(TEXT("They nod. Keep going."));
	}
	Interest = FMath::Clamp(Interest + Gain, 0, 100);
	++Turn;
	if (Interest >= 100)
	{
		Lines.Add(FString::Printf(TEXT("%s: Alright. You got me."), *Speaker));
		bWon = true;
		EndDelay = 1.4f;
	}
	else if (Patience <= 0)
	{
		Fail(TEXT("They walked off."));
	}
}

void APuffersStepDialogue::OnRunning(float DeltaSeconds)
{
	// Keep the last few lines on screen.
	while (Lines.Num() > 5)
	{
		Lines.RemoveAt(0);
	}
	if (EndDelay >= 0.f)
	{
		EndDelay -= DeltaSeconds;
		if (EndDelay < 0.f && bWon)
		{
			Succeed();
		}
		return;
	}
	if (KeyPressed(EKeys::BackSpace))
	{
		Leave();
		return;
	}

	// Rebuild the options for this turn.
	Options.SetNum(Params.Gameplay == TEXT("BusinessMenu") ? Options.Num() : 0);
	if (Params.Gameplay == TEXT("Negotiate"))
	{
		Options = { TEXT("Lower 5%"), TEXT("Raise 5%"), FString::Printf(TEXT("Offer %s"), *Money(FMath::RoundToInt(Ask * Offer))), TEXT("Walk away") };
	}
	else if (Params.Gameplay != TEXT("BusinessMenu"))
	{
		Options = { TEXT("Keep it smooth"), TEXT("Keep it real"), TEXT("Go hard") };
		if (HasTalkAbility() && !bAbilityUsed)
		{
			Options.Add(HasAbility(TEXT("TalkThatShit")) ? TEXT("[Talk That Shit] Run the full pitch") : TEXT("[Gift of Gab] Make a promise"));
		}
	}

	const int32 Option = Elapsed() > 0.2f ? OptionPressed() : 0;
	if (Option > 0 && Option <= Options.Num())
	{
		Pick(Option);
	}
}

void APuffersStepDialogue::OnEnd()
{
	LockMovement(false);
}

void APuffersStepDialogue::FillOverlay(FPuffersStepOverlay& Out) const
{
	Out.Lines = Lines;
	if (EndDelay < 0.f)
	{
		Out.Options = Options;
	}
	if (Params.Gameplay == TEXT("Persuade"))
	{
		Out.Bar = Interest / 100.f;
		Out.BarLabel = TEXT("Interest");
		Out.Bar2 = Patience / 3.f;
		Out.Bar2Label = TEXT("Patience");
	}
	else if (Params.Gameplay == TEXT("Negotiate"))
	{
		Out.Bar2 = Patience / 3.f;
		Out.Bar2Label = TEXT("Patience");
	}
}

// Switch -------------------------------------------------------------------------------------------------------

void APuffersStepSwitch::OnBegin()
{
	const UPuffersStepSubsystem* Subsystem = Owner.Get();
	TargetCharacter = !Params.Target.IsNone() ? Params.Target
		: (Subsystem ? FPuffersStepRules::ParseSwitchTarget(Objective.Description, Subsystem->GetKnownCharacters()) : NAME_None);
	StatusLine = TargetCharacter.IsNone() ? TEXT("Press K to switch.")
		: FString::Printf(TEXT("Press K to switch to %s."), *(Subsystem ? Subsystem->DisplayNameFor(TargetCharacter) : TargetCharacter.ToString()));
}

void APuffersStepSwitch::OnRunning(float)
{
	if (!KeyPressed(EKeys::K))
	{
		return;
	}
	if (UPuffersStepSubsystem* Subsystem = Owner.Get())
	{
		Subsystem->SwitchToCharacter(TargetCharacter);
	}
	Succeed();
}

void APuffersStepSwitch::FillOverlay(FPuffersStepOverlay& Out) const
{
	Out.Center = TEXT("[K] SWITCH");
}
