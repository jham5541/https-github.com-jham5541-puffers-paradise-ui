// Action steps: Search, phone camera (Photo / Video), Defend, Combat and the Mac Vision sweep (Scan).

#include "Missions/Steps/PuffersStepActor.h"
#include "Missions/Steps/PuffersStepAIController.h"
#include "Missions/Steps/PuffersStepRules.h"
#include "Missions/Steps/PuffersStepSubsystem.h"
#include "Missions/PuffersMissionSubsystem.h"
#include "Player/PuffersPlayerCharacter.h"
#include "Systems/PuffersHeatSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	const FLinearColor Gold(1.f, 0.77f, 0.25f);
	const FLinearColor Red(1.f, 0.3f, 0.25f);
	const FLinearColor Green(0.45f, 0.85f, 0.5f);
	const FLinearColor Grey(0.55f, 0.55f, 0.55f);

	FPuffersStepMarker MakeMarker(const FVector& Location, const FString& Text, const FLinearColor& Color, bool bDistance = false)
	{
		FPuffersStepMarker Marker;
		Marker.Location = Location;
		Marker.Text = Text;
		Marker.Color = Color;
		Marker.bShowDistance = bDistance;
		return Marker;
	}
}

// Search -------------------------------------------------------------------------------------------------------

void APuffersStepSearch::OnBegin()
{
	Spots.Reset();
	Searching = INDEX_NONE;
	SearchProgress = 0.f;
	const int32 Real = GetRemaining();
	const int32 Decoys = Params.Count > 0 ? Params.Count : 2 + FMath::RoundToInt(GetDifficulty() * 2.f);
	const float Radius = Params.Radius > 0.f ? Params.Radius : 1200.f;
	const int32 Total = Real + Decoys;
	const int32 Offset = FPuffersStepRules::StableIndex(Objective.Id, 31 + Generation, FMath::Max(1, Total));
	for (int32 i = 0; i < Total; ++i)
	{
		FSpot Spot;
		Spot.Location = RandomPointAround(Anchor, 250.f, Radius, 300 + i);
		// Spread the real ones through the list so the first spot you try isn't always it.
		Spot.bReal = ((i + Offset) % Total) < Real;
		Spot.Marker = SpawnMarker(Spot.Location, TEXT("Cube"), FVector(0.35f), Grey, TEXT("?"));
		Spots.Add(Spot);
	}
	StatusLine = TEXT("Check the spots: stand next to one and hold E. The meter says how warm you are.");
}

void APuffersStepSearch::OnRunning(float DeltaSeconds)
{
	const FVector Here = PlayerLocation();
	int32 Nearest = INDEX_NONE;
	float NearestDistance = 230.f;
	float WarmDistance = MAX_flt;
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		const FSpot& Spot = Spots[i];
		if (Spot.bChecked)
		{
			continue;
		}
		const float Distance = FVector::Dist2D(Here, Spot.Location);
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = i;
		}
		if (Spot.bReal)
		{
			WarmDistance = FMath::Min(WarmDistance, Distance);
		}
	}
	const float Radius = Params.Radius > 0.f ? Params.Radius : 1200.f;
	Warmth = WarmDistance < MAX_flt ? 1.f - FMath::Clamp(WarmDistance / (Radius * 1.5f), 0.f, 1.f) : 0.f;

	if (Nearest == INDEX_NONE || !InteractDown())
	{
		Searching = INDEX_NONE;
		SearchProgress = 0.f;
		return;
	}
	if (Searching != Nearest)
	{
		Searching = Nearest;
		SearchProgress = 0.f;
	}
	SearchProgress += DeltaSeconds / (IsAbilityActive(TEXT("MacVision")) ? 0.6f : 1.4f);
	if (SearchProgress < 1.f)
	{
		return;
	}
	FSpot& Spot = Spots[Searching];
	Spot.bChecked = true;
	Searching = INDEX_NONE;
	SearchProgress = 0.f;
	if (!Spot.bReal)
	{
		if (APuffersStepMarker* Marker = Spot.Marker.Get())
		{
			Marker->SetColor(Grey);
			Marker->SetText(TEXT("nothing"));
		}
		Toast(TEXT("Nothing here."));
		return;
	}
	if (APuffersStepMarker* Marker = Spot.Marker.Get())
	{
		Marker->SetColor(Green);
		Marker->SetText(TEXT("FOUND"));
	}
	if (GetProgress() + 1 >= GetRequired())
	{
		Succeed(TEXT("Found it."));
	}
	else if (UPuffersMissionSubsystem* Missions = GetMissions())
	{
		Toast(TEXT("Found one. Keep looking."));
		Missions->ReportObjectiveProgress(Objective.Id, 1);
	}
}

void APuffersStepSearch::OnEnd()
{
	Spots.Reset();
}

void APuffersStepSearch::FillOverlay(FPuffersStepOverlay& Out) const
{
	Out.Bar = Warmth;
	Out.BarLabel = Warmth > 0.75f ? TEXT("Hot") : Warmth > 0.45f ? TEXT("Warm") : TEXT("Cold");
	Out.BarColor = FLinearColor::LerpUsingHSV(FLinearColor(0.3f, 0.5f, 1.f), Red, Warmth);
	if (Searching != INDEX_NONE)
	{
		Out.Bar2 = SearchProgress;
		Out.Bar2Label = TEXT("Searching...");
		Out.Bar2Color = Gold;
	}
	const bool bVision = IsAbilityActive(TEXT("MacVision"));
	for (const FSpot& Spot : Spots)
	{
		if (!Spot.bChecked)
		{
			Out.Markers.Add(MakeMarker(Spot.Location, bVision && Spot.bReal ? TEXT("!") : TEXT("?"), bVision && Spot.bReal ? Red : Gold));
		}
	}
	Out.Markers.Add(MakeMarker(Anchor, TEXT("SEARCH AREA"), Gold, true));
}

// Phone camera -------------------------------------------------------------------------------------------------

FString APuffersStepCamera::GetGameplayLabel() const
{
	return IsVideo() ? (Params.bLive ? TEXT("LIVESTREAM") : TEXT("FILM")) : TEXT("PHOTO");
}

void APuffersStepCamera::OnBegin()
{
	Subjects.Reset();
	Captured.Reset();
	bRecording = false;
	TakeTime = 0.f;
	OutOfFrame = 0.f;
	Viewers = 0.f;
	LastQuality = -1.f;
	const int32 Count = IsVideo() ? 1 : (Params.Count > 0 ? Params.Count : GetRemaining());
	const bool bSpots = Params.Subject == TEXT("Spot");
	const FString Named = SpeakerName();
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector At = RandomPointAround(Anchor, 500.f, 1300.f, 400 + i);
		if (bSpots)
		{
			Subjects.Add(SpawnMarker(At, TEXT("Cylinder"), FVector(0.8f, 0.8f, 2.5f), Gold, FString::Printf(TEXT("SPOT %d"), i + 1)));
		}
		else if (APuffersNPCCharacter* NPC = SpawnNPC(At, Named.IsEmpty() ? TEXT("Subject") : Named, EPuffersNPCRole::Pedestrian, 110.f, 300.f, false))
		{
			Subjects.Add(NPC);
			if (APuffersStepAIController* Brain = AI(NPC))
			{
				TArray<FVector> Loop;
				for (int32 p = 0; p < 3; ++p)
				{
					Loop.Add(RandomPointAround(At, 200.f, 600.f, 450 + i * 5 + p));
				}
				Brain->SetPatrol(Loop, false);
			}
		}
		Captured.Add(false);
	}
	StatusLine = TEXT("Press P to raise your phone.");
}

void APuffersStepCamera::SetPhone(bool bUp)
{
	if (bUp == bPhoneUp)
	{
		return;
	}
	APuffersPlayerCharacter* Player = GetPlayerCharacter();
	if (!Player)
	{
		bPhoneUp = false;
		return;
	}
	if (bUp)
	{
		SavedFOV = Player->FollowCamera->FieldOfView;
		SavedArm = Player->CameraBoom->TargetArmLength;
		Player->FollowCamera->SetFieldOfView(55.f);
		Player->CameraBoom->TargetArmLength = 60.f;
	}
	else
	{
		Player->FollowCamera->SetFieldOfView(SavedFOV);
		Player->CameraBoom->TargetArmLength = SavedArm;
		bRecording = false;
	}
	bPhoneUp = bUp;
}

int32 APuffersStepCamera::BestSubject(float& OutAngle, float& OutDistance) const
{
	int32 Best = INDEX_NONE;
	OutAngle = 180.f;
	OutDistance = 0.f;
	const APlayerController* PC = GetPC();
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!PC)
	{
		return INDEX_NONE;
	}
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	for (int32 i = 0; i < Subjects.Num(); ++i)
	{
		const AActor* Subject = Subjects[i].Get();
		if (!Subject || (!IsVideo() && Captured[i]))
		{
			continue;
		}
		const FVector Target = Subject->GetActorLocation() + FVector(0.f, 0.f, 40.f);
		float Angle = 0.f, Distance = 0.f;
		if (CameraAngleTo(Target, Angle, Distance) && Angle < OutAngle && ClearLine(ViewLocation, Target, GetPlayerPawn(), Subject))
		{
			OutAngle = Angle;
			OutDistance = Distance;
			Best = i;
		}
	}
	return Best;
}

void APuffersStepCamera::OnRunning(float DeltaSeconds)
{
	if (KeyPressed(EKeys::P) || KeyPressed(EKeys::Gamepad_RightShoulder))
	{
		SetPhone(!bPhoneUp);
	}
	if (!bPhoneUp)
	{
		StatusLine = TEXT("Press P to raise your phone.");
		return;
	}
	const bool bViral = HasAbility(TEXT("GoViral"));
	float Angle = 180.f, Distance = 0.f;
	const int32 Best = BestSubject(Angle, Distance);

	if (!IsVideo())
	{
		StatusLine = TEXT("Frame the subject and click to shoot.");
		if (FirePressed())
		{
			LastQuality = Best == INDEX_NONE ? 0.f : FPuffersStepRules::PhotoQuality(Angle, Distance, bViral);
			if (LastQuality >= 0.5f)
			{
				Captured[Best] = true;
				if (GetProgress() + 1 >= GetRequired())
				{
					Succeed(TEXT("That's the shot."));
				}
				else if (UPuffersMissionSubsystem* Missions = GetMissions())
				{
					Toast(FString::Printf(TEXT("Nice shot (%d%%)."), FMath::RoundToInt(LastQuality * 100.f)));
					Missions->ReportObjectiveProgress(Objective.Id, 1);
				}
			}
			else
			{
				Toast(Best == INDEX_NONE ? TEXT("Nothing in the shot.") : TEXT("Blurry. Get it centred and closer."), true);
			}
		}
		return;
	}

	// Video
	const float TakeLength = Params.Seconds > 0.f ? Params.Seconds : 8.f;
	const bool bInFrame = Best != INDEX_NONE && Angle < 14.f && Distance < 2600.f;
	if (!bRecording)
	{
		StatusLine = TEXT("Click to start recording. Keep the subject in frame.");
		if (FirePressed())
		{
			bRecording = true;
			TakeTime = 0.f;
			OutOfFrame = 0.f;
		}
		return;
	}
	if (Params.bLive)
	{
		const float Goal = Params.Count > 0 ? Params.Count : 500.f;
		Viewers = FMath::Max(0.f, Viewers + DeltaSeconds * (bInFrame ? 45.f * (bViral ? 1.6f : 1.f) : -70.f));
		StatusLine = bInFrame ? TEXT("Chat's eating this up.") : TEXT("Viewers are dropping. Get back on it!");
		if (Viewers >= Goal)
		{
			Succeed(FString::Printf(TEXT("%d watching. Clip of the day."), FMath::RoundToInt(Viewers)));
		}
		else if (Elapsed() > 90.f)
		{
			Fail(TEXT("The stream died."));
		}
		return;
	}
	if (bInFrame)
	{
		TakeTime += DeltaSeconds;
		OutOfFrame = 0.f;
	}
	else
	{
		OutOfFrame += DeltaSeconds;
		if (OutOfFrame > (bViral ? 2.5f : 1.5f))
		{
			bRecording = false;
			TakeTime = 0.f;
			Toast(TEXT("Lost them. Take ruined."), true);
			return;
		}
	}
	StatusLine = bInFrame ? TEXT("Rolling...") : TEXT("Keep them in frame!");
	if (TakeTime >= TakeLength)
	{
		Succeed(GetRequired() > 1 ? TEXT("Good take. Again.") : TEXT("That's a wrap."));
	}
}

void APuffersStepCamera::OnEnd()
{
	SetPhone(false);
	Subjects.Reset();
}

void APuffersStepCamera::FillOverlay(FPuffersStepOverlay& Out) const
{
	Out.bPhoneFrame = bPhoneUp;
	Out.bRecording = bRecording;
	for (int32 i = 0; i < Subjects.Num(); ++i)
	{
		if (const AActor* Subject = Subjects[i].Get())
		{
			if (!Captured.IsValidIndex(i) || !Captured[i])
			{
				Out.Markers.Add(MakeMarker(Subject->GetActorLocation(), TEXT("SUBJECT"), Gold, !bPhoneUp));
			}
		}
	}
	if (IsVideo())
	{
		if (Params.bLive)
		{
			const float Goal = Params.Count > 0 ? Params.Count : 500.f;
			Out.Bar = Viewers / Goal;
			Out.BarLabel = FString::Printf(TEXT("%d viewers"), FMath::RoundToInt(Viewers));
		}
		else
		{
			Out.Bar = TakeTime / (Params.Seconds > 0.f ? Params.Seconds : 8.f);
			Out.BarLabel = TEXT("Take");
		}
	}
	else if (LastQuality >= 0.f)
	{
		Out.Lines.Add(FString::Printf(TEXT("Last shot %d%%"), FMath::RoundToInt(LastQuality * 100.f)));
	}
}

// Defend -------------------------------------------------------------------------------------------------------

void APuffersStepDefend::OnBegin()
{
	Agitators.Reset();
	Repelled.Reset();
	Integrity = 1.f;
	Wave = 0;
	Violence = 0;
	Duration = Params.Seconds > 0.f ? Params.Seconds : 60.f;
	ZoneRadius = Params.Radius > 0.f ? Params.Radius : 650.f;
	WaveTimer = 1.5f;
	SpawnMarker(Anchor, TEXT("Cylinder"), FVector(ZoneRadius / 50.f, ZoneRadius / 50.f, 0.02f), FLinearColor(1.f, 0.77f, 0.25f, 0.4f), TEXT(""));
	StatusLine = Params.bPeaceful ? TEXT("Talk people down (E) before they push into the zone. No shoving.")
		: TEXT("Keep people out of the zone: shove (F) or talk them down (E).");
}

void APuffersStepDefend::SpawnWave()
{
	++Wave;
	int32 Alive = 0;
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Agitators)
	{
		Alive += Entry.IsValid() && !Repelled.Contains(Entry->NPCId) ? 1 : 0;
	}
	const int32 Size = FMath::Min(2 + Wave / 2 + FMath::RoundToInt(GetDifficulty() * 2.f), 7 - Alive);
	const FString Name = Params.Label.IsEmpty() ? TEXT("Troublemaker") : Params.Label;
	for (int32 i = 0; i < Size; ++i)
	{
		const FVector At = RandomPointAround(Anchor, ZoneRadius + 1200.f, ZoneRadius + 1700.f, 500 + Wave * 10 + i);
		if (APuffersNPCCharacter* NPC = SpawnNPC(At, Name, RoleFromParams(EPuffersNPCRole::Pedestrian), 170.f + 40.f * GetDifficulty(), 360.f, false))
		{
			Agitators.Add(NPC);
			if (APuffersStepAIController* Brain = AI(NPC))
			{
				Brain->GoTo(RandomPointAround(Anchor, 0.f, ZoneRadius * 0.4f, 600 + i), false);
			}
		}
	}
}

void APuffersStepDefend::Repel(APuffersNPCCharacter* NPC)
{
	if (!NPC || Repelled.Contains(NPC->NPCId))
	{
		return;
	}
	Repelled.Add(NPC->NPCId);
	NPC->NameTag->SetText(FText::FromString(TEXT("(leaving)")));
	if (APuffersStepAIController* Brain = AI(NPC))
	{
		const FVector Away = NPC->GetActorLocation() + (NPC->GetActorLocation() - Anchor).GetSafeNormal2D() * 2500.f;
		Brain->GoTo(Away, true);
	}
}

void APuffersStepDefend::OnAgitatorTalkedTo(AActor*)
{
	APuffersNPCCharacter* Closest = nullptr;
	float Best = 260.f;
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Agitators)
	{
		APuffersNPCCharacter* NPC = Entry.Get();
		const float Distance = NPC ? PlayerDistance(NPC->GetActorLocation()) : MAX_flt;
		if (NPC && Distance < Best && !Repelled.Contains(NPC->NPCId))
		{
			Best = Distance;
			Closest = NPC;
		}
	}
	Repel(Closest);
}

void APuffersStepDefend::OnRunning(float DeltaSeconds)
{
	WaveTimer -= DeltaSeconds;
	if (WaveTimer <= 0.f && Elapsed() < Duration - 6.f)
	{
		SpawnWave();
		WaveTimer = FMath::Lerp(12.f, 8.f, GetDifficulty());
	}
	if (InteractPressed())
	{
		OnAgitatorTalkedTo(nullptr);
	}

	int32 Inside = 0;
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Agitators)
	{
		const APuffersNPCCharacter* NPC = Entry.Get();
		if (NPC && !Repelled.Contains(NPC->NPCId) && FVector::Dist2D(NPC->GetActorLocation(), Anchor) < ZoneRadius)
		{
			++Inside;
		}
	}
	Integrity = FMath::Clamp(Integrity - Inside * 0.035f * FMath::Lerp(0.8f, 1.3f, GetDifficulty()) * DeltaSeconds, 0.f, 1.f);
	if (Integrity <= 0.f)
	{
		Fail(TEXT("They overran the spot."));
		return;
	}
	if (Elapsed() >= Duration)
	{
		Succeed(TEXT("Held it down."));
	}
}

void APuffersStepDefend::OnGameEvent(FName EventName, FName Context)
{
	if (State != EPuffersStepState::Running || EventName != TEXT("ShovedNPC"))
	{
		return;
	}
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Agitators)
	{
		APuffersNPCCharacter* NPC = Entry.Get();
		if (NPC && NPC->NPCId == Context)
		{
			if (Params.bPeaceful && ++Violence > 2)
			{
				Fail(TEXT("It turned into a brawl."));
				return;
			}
			Repel(NPC);
			return;
		}
	}
}

void APuffersStepDefend::OnAbilityUsed(FName Ability)
{
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Agitators)
	{
		APuffersNPCCharacter* NPC = Entry.Get();
		if (!NPC)
		{
			continue;
		}
		if (Ability == TEXT("TalkThatShit") && PlayerDistance(NPC->GetActorLocation()) < 900.f)
		{
			Repel(NPC);
		}
		else if (Ability == TEXT("Trendsetter"))
		{
			if (APuffersStepAIController* Brain = AI(NPC))
			{
				Brain->Pause(4.f, GetPlayerPawn());
			}
		}
	}
}

void APuffersStepDefend::OnEnd()
{
	Agitators.Reset();
	Repelled.Reset();
}

void APuffersStepDefend::FillOverlay(FPuffersStepOverlay& Out) const
{
	Out.Bar = Integrity;
	Out.BarLabel = TEXT("Holding");
	Out.BarColor = Green;
	Out.Lines.Add(FString::Printf(TEXT("Hold for %ds more   Wave %d"), FMath::Max(0, FMath::CeilToInt(Duration - Elapsed())), Wave));
	if (Params.bPeaceful)
	{
		Out.Lines.Add(FString::Printf(TEXT("Shoves %d/2"), Violence));
	}
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Agitators)
	{
		const APuffersNPCCharacter* NPC = Entry.Get();
		if (NPC && !Repelled.Contains(NPC->NPCId))
		{
			Out.Markers.Add(MakeMarker(NPC->GetActorLocation(), TEXT("!"), Red));
		}
	}
}

// Combat -------------------------------------------------------------------------------------------------------

void APuffersStepCombat::OnBegin()
{
	Enemies.Reset();
	Health = 100.f;
	LastHurt = -100.f;
	FireCooldown = 0.f;
	bAiming = false;
	if (UPuffersHeatSubsystem* Heat = GetWorld()->GetSubsystem<UPuffersHeatSubsystem>())
	{
		Heat->RaiseHeat(FMath::Max(3, Params.Heat), TEXT("ShotsFired"));
	}
	const int32 Count = Params.Count > 0 ? Params.Count : (GetDifficulty() > 0.6f ? 4 : 3);
	const FString Name = Params.Label.IsEmpty() ? TEXT("Shooter") : Params.Label;
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector At = RandomPointAround(PlayerLocation(), 1400.f, 2000.f, 700 + i);
		if (APuffersNPCCharacter* NPC = SpawnNPC(At, Name, RoleFromParams(EPuffersNPCRole::Enforcer), 200.f, 380.f, false))
		{
			FEnemy Enemy;
			Enemy.NPC = NPC;
			Enemy.Health = GetDifficulty() > 0.7f ? 4 : 3;
			Enemy.NextShot = 2.f + i * 0.6f;
			Enemies.Add(Enemy);
		}
	}
	StatusLine = TEXT("Right mouse to aim, left mouse to fire. Crouch (C) to take less fire.");
}

void APuffersStepCombat::OnRunning(float DeltaSeconds)
{
	APawn* Player = GetPlayerPawn();
	APuffersPlayerCharacter* Character = GetPlayerCharacter();
	if (!Player)
	{
		return;
	}
	const float Now = Elapsed();
	FireCooldown -= DeltaSeconds;
	HitFlash = FMath::Max(0.f, HitFlash - DeltaSeconds);

	// Aim: tighter camera.
	const bool bWantAim = AimDown();
	if (Character && bWantAim != bAiming)
	{
		if (bWantAim)
		{
			SavedFOV = Character->FollowCamera->FieldOfView;
			SavedArm = Character->CameraBoom->TargetArmLength;
			Character->FollowCamera->SetFieldOfView(62.f);
			Character->CameraBoom->TargetArmLength = 180.f;
		}
		else
		{
			Character->FollowCamera->SetFieldOfView(SavedFOV);
			Character->CameraBoom->TargetArmLength = SavedArm;
		}
		bAiming = bWantAim;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	if (APlayerController* PC = GetPC())
	{
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}

	// Player fire: hitscan from the camera centre.
	if (FirePressed() && FireCooldown <= 0.f)
	{
		FireCooldown = 0.28f;
		int32 Target = INDEX_NONE;
		float BestAngle = MAX_flt;
		for (int32 i = 0; i < Enemies.Num(); ++i)
		{
			const APuffersNPCCharacter* NPC = Enemies[i].NPC.Get();
			if (!NPC || Enemies[i].Health <= 0)
			{
				continue;
			}
			const FVector Body = NPC->GetActorLocation() + FVector(0.f, 0.f, 30.f);
			float Angle = 0.f, Distance = 0.f;
			CameraAngleTo(Body, Angle, Distance);
			const float Tolerance = FMath::RadiansToDegrees(FMath::Atan2(55.f, FMath::Max(Distance, 1.f))) * (bAiming ? 1.f : 1.4f);
			if (Angle < Tolerance && Angle < BestAngle && ClearLine(ViewLocation, Body, Player, NPC))
			{
				BestAngle = Angle;
				Target = i;
			}
		}
#if ENABLE_DRAW_DEBUG
		DrawDebugLine(GetWorld(), Player->GetActorLocation() + FVector(0.f, 0.f, 50.f), ViewLocation + ViewRotation.Vector() * 4000.f,
			FColor(255, 220, 120), false, 0.08f, 0, 1.5f);
#endif
		if (Target != INDEX_NONE)
		{
			FEnemy& Enemy = Enemies[Target];
			--Enemy.Health;
			if (APuffersNPCCharacter* NPC = Enemy.NPC.Get())
			{
				if (Enemy.Health <= 0)
				{
					NPC->NameTag->SetText(FText::FromString(TEXT("DOWN")));
					NPC->GetCharacterMovement()->DisableMovement();
					NPC->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					NPC->SetActorRotation(FRotator(0.f, NPC->GetActorRotation().Yaw, 90.f));
					if (APuffersStepAIController* Brain = AI(NPC))
					{
						Brain->HoldPosition(nullptr);
					}
				}
				else if (APuffersStepAIController* Brain = AI(NPC))
				{
					Brain->Slow(0.6f, 0.2f);
				}
			}
		}
	}

	// Enemies close in and fire back.
	int32 Alive = 0;
	for (FEnemy& Enemy : Enemies)
	{
		APuffersNPCCharacter* NPC = Enemy.NPC.Get();
		if (!NPC || Enemy.Health <= 0)
		{
			continue;
		}
		++Alive;
		APuffersStepAIController* Brain = AI(NPC);
		const float Distance = FVector::Dist2D(NPC->GetActorLocation(), Player->GetActorLocation());
		if (Brain)
		{
			if (Distance > 950.f && Brain->GetMode() != EPuffersStepAIMode::Chase)
			{
				Brain->ChaseActor(Player);
			}
			else if (Distance < 700.f && Brain->GetMode() != EPuffersStepAIMode::Hold)
			{
				Brain->HoldPosition(Player);
			}
		}
		Enemy.NextShot -= DeltaSeconds;
		const FVector Muzzle = NPC->GetActorLocation() + FVector(0.f, 0.f, 50.f);
		if (Enemy.NextShot <= 0.f && Distance < 2400.f && ClearLine(Muzzle, Player->GetActorLocation(), NPC, Player))
		{
			Enemy.NextShot = FMath::FRandRange(1.1f, 1.9f) - 0.3f * GetDifficulty();
			const bool bHit = FMath::FRand() < FPuffersStepRules::EnemyHitChance(Distance, IsPlayerCrouched(), GetDifficulty());
#if ENABLE_DRAW_DEBUG
			const FVector Miss = bHit ? FVector::ZeroVector : FVector(FMath::FRandRange(-120.f, 120.f), FMath::FRandRange(-120.f, 120.f), 60.f);
			DrawDebugLine(GetWorld(), Muzzle, Player->GetActorLocation() + Miss, FColor::Red, false, 0.08f, 0, 1.f);
#endif
			if (bHit)
			{
				Health -= 9.f + 4.f * GetDifficulty();
				LastHurt = Now;
				HitFlash = 0.3f;
			}
		}
	}

	if (Now - LastHurt > 4.f)
	{
		Health = FMath::Min(100.f, Health + 6.f * DeltaSeconds);
	}
	if (Health <= 0.f)
	{
		Fail(TEXT("You got dropped."), true);
		return;
	}
	if (Alive == 0)
	{
		Succeed(TEXT("Threat handled."));
	}
}

void APuffersStepCombat::OnEnd()
{
	if (bAiming)
	{
		if (APuffersPlayerCharacter* Character = GetPlayerCharacter())
		{
			Character->FollowCamera->SetFieldOfView(SavedFOV);
			Character->CameraBoom->TargetArmLength = SavedArm;
		}
		bAiming = false;
	}
	Enemies.Reset();
}

void APuffersStepCombat::FillOverlay(FPuffersStepOverlay& Out) const
{
	Out.bCrosshair = true;
	Out.Bar2 = Health / 100.f;
	Out.Bar2Label = TEXT("Health");
	Out.Bar2Color = Health < 35.f ? Red : Green;
	int32 Alive = 0;
	for (const FEnemy& Enemy : Enemies)
	{
		const APuffersNPCCharacter* NPC = Enemy.NPC.Get();
		if (NPC && Enemy.Health > 0)
		{
			++Alive;
			Out.Markers.Add(MakeMarker(NPC->GetActorLocation(), TEXT("X"), Red));
		}
	}
	Out.Lines.Add(FString::Printf(TEXT("Hostiles left %d"), Alive));
	if (HitFlash > 0.f)
	{
		Out.Center = TEXT("HIT");
	}
}

// Scan (Mac Vision sweep) --------------------------------------------------------------------------------------

void APuffersStepScan::OnBegin()
{
	People.Reset();
	Flagged = 0;
	FalseAlarms = 0;
	Threats = Params.Count > 0 ? Params.Count : 3;
	const int32 Crowd = Threats * 3 + 3;
	const int32 Offset = FPuffersStepRules::StableIndex(Objective.Id, 77 + Generation, Crowd);
	for (int32 i = 0; i < Crowd; ++i)
	{
		const FVector At = RandomPointAround(Anchor, 300.f, 1300.f, 800 + i);
		APuffersNPCCharacter* NPC = SpawnNPC(At, TEXT("Shopper"), EPuffersNPCRole::Pedestrian, 120.f, 300.f, false);
		if (!NPC)
		{
			continue;
		}
		FPerson Person;
		Person.NPC = NPC;
		Person.bThreat = ((i + Offset) % Crowd) < Threats;
		People.Add(Person);
		if (APuffersStepAIController* Brain = AI(NPC))
		{
			TArray<FVector> Loop;
			for (int32 p = 0; p < 3; ++p)
			{
				Loop.Add(RandomPointAround(At, 200.f, 700.f, 900 + i * 5 + p));
			}
			Brain->SetPatrol(Loop, false);
		}
	}
	TwitchTimer = 2.f;
	StatusLine = HasAbility(TEXT("MacVision"))
		? TEXT("Use Mac Vision (Z). Look at a threat and press E to flag it.")
		: TEXT("Watch for whoever keeps stopping to look around. Look at them and press E to flag.");
}

int32 APuffersStepScan::LookedAt(float MaxAngle) const
{
	int32 Best = INDEX_NONE;
	float BestAngle = MaxAngle;
	for (int32 i = 0; i < People.Num(); ++i)
	{
		const APuffersNPCCharacter* NPC = People[i].NPC.Get();
		if (!NPC || People[i].bFlagged)
		{
			continue;
		}
		float Angle = 0.f, Distance = 0.f;
		if (CameraAngleTo(NPC->GetActorLocation() + FVector(0.f, 0.f, 30.f), Angle, Distance) && Distance < 2800.f && Angle < BestAngle)
		{
			BestAngle = Angle;
			Best = i;
		}
	}
	return Best;
}

void APuffersStepScan::OnRunning(float DeltaSeconds)
{
	// The tell: threats keep stopping to check who's watching.
	TwitchTimer -= DeltaSeconds;
	if (TwitchTimer <= 0.f)
	{
		for (const FPerson& Person : People)
		{
			if (Person.bThreat && !Person.bFlagged && FMath::FRand() < 0.6f)
			{
				if (APuffersStepAIController* Brain = AI(Person.NPC.Get()))
				{
					Brain->Pause(1.6f, GetPlayerPawn());
				}
			}
		}
		TwitchTimer = FMath::Lerp(3.f, 5.f, GetDifficulty());
	}

	if (Elapsed() < 0.25f || !InteractPressed())
	{
		return;
	}
	const int32 Index = LookedAt(7.f);
	if (Index == INDEX_NONE)
	{
		return;
	}
	FPerson& Person = People[Index];
	if (!Person.bThreat)
	{
		++FalseAlarms;
		Toast(TEXT("False alarm. That's just a shopper."), true);
		if (FalseAlarms >= 3)
		{
			Fail(TEXT("Too many false alarms."));
		}
		return;
	}
	Person.bFlagged = true;
	++Flagged;
	if (APuffersNPCCharacter* NPC = Person.NPC.Get())
	{
		NPC->NameTag->SetText(FText::FromString(TEXT("FLAGGED")));
		if (APuffersStepAIController* Brain = AI(NPC))
		{
			Brain->HoldPosition(GetPlayerPawn());
		}
	}
	if (Flagged >= Threats)
	{
		Succeed(TEXT("Area secured."));
	}
	else
	{
		Toast(FString::Printf(TEXT("Threat flagged (%d/%d)."), Flagged, Threats));
	}
}

void APuffersStepScan::OnEnd()
{
	People.Reset();
}

void APuffersStepScan::FillOverlay(FPuffersStepOverlay& Out) const
{
	Out.bCrosshair = true;
	Out.Lines.Add(FString::Printf(TEXT("Flagged %d/%d   False alarms %d/3"), Flagged, Threats, FalseAlarms));
	const bool bVision = IsAbilityActive(TEXT("MacVision"));
	for (const FPerson& Person : People)
	{
		const APuffersNPCCharacter* NPC = Person.NPC.Get();
		if (!NPC)
		{
			continue;
		}
		if (Person.bFlagged)
		{
			Out.Markers.Add(MakeMarker(NPC->GetActorLocation(), TEXT("FLAGGED"), Green));
		}
		else if (bVision && Person.bThreat)
		{
			Out.Markers.Add(MakeMarker(NPC->GetActorLocation(), TEXT("THREAT"), Red));
		}
	}
	if (bVision)
	{
		Out.Center = TEXT("MAC VISION");
	}
}
